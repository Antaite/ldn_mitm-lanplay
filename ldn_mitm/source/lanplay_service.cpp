/* SPDX-License-Identifier: GPL-2.0-only */
#include "lanplay_service.hpp"
#include "debug.hpp"
#include "ldnmitm_config.hpp"
#include <cstring>
#include <cstdlib>
#include <atomic>

extern "C" {
#include "lanplay/lp_config.h"
#include "lanplay/lp_tunnel.h"
#include "lanplay/lp_platform.h"
#include "lanplay/vnet.h"
}

#ifndef LANPLAY_VERSION
#define LANPLAY_VERSION "dev"
#endif

namespace ams::mitm::ldn::lanplay {

    namespace {

        constexpr const char ConfigDir0[] = "sdmc:/config";
        constexpr const char ConfigDir1[] = "sdmc:/config/ldn_mitm";
        constexpr const char ConfigPath[] = "sdmc:" LP_CONFIG_PATH;
        constexpr size_t     ConfigMax    = 16 * 1024;

        constinit os::SdkMutex g_lock;
        lp_config_t            g_config;
        constinit u32          g_mode = LP_MODE_LAN;
        constinit bool         g_version_ok = true;
        constinit bool         g_vnet_ready = false;
        constinit u16          g_ams_running[3];
        constinit char         g_file_buf[ConfigMax];
        constinit std::atomic<bool> g_session_active{false};

        /* While a room is open or joined, give the other side a moment to move first. */
        constexpr u32 SessionGraceMs = 5000;

        int VnetOutput(const uint8_t *pkt, size_t len) {
            return lp_tunnel_send_ipv4(pkt, len);
        }

        bool ReadConfigFile(lp_config_t *cfg) {
            fs::FileHandle f;
            if (R_FAILED(fs::OpenFile(std::addressof(f), ConfigPath, fs::OpenMode_Read))) {
                lp_config_default(cfg);
                return false;
            }
            ON_SCOPE_EXIT { fs::CloseFile(f); };
            s64 size = 0;
            if (R_FAILED(fs::GetFileSize(std::addressof(size), f))) {
                lp_config_default(cfg);
                return false;
            }
            if (size > static_cast<s64>(ConfigMax)) {
                size = ConfigMax;
            }
            if (R_FAILED(fs::ReadFile(f, 0, g_file_buf, static_cast<size_t>(size)))) {
                lp_config_default(cfg);
                return false;
            }
            lp_config_parse(cfg, g_file_buf, static_cast<size_t>(size));
            return true;
        }

        Result WriteConfigFile(const lp_config_t *cfg) {
            const size_t len = lp_config_format(cfg, g_file_buf, sizeof(g_file_buf));
            if (len == 0) {
                return MAKERESULT(0xFD, 91);
            }
            static_cast<void>(fs::CreateDirectory(ConfigDir0));
            static_cast<void>(fs::CreateDirectory(ConfigDir1));
            static_cast<void>(fs::DeleteFile(ConfigPath));
            R_TRY(fs::CreateFile(ConfigPath, static_cast<s64>(len)));
            fs::FileHandle f;
            R_TRY(fs::OpenFile(std::addressof(f), ConfigPath, fs::OpenMode_Write));
            ON_SCOPE_EXIT { fs::CloseFile(f); };
            R_TRY(fs::WriteFile(f, 0, g_file_buf, len, fs::WriteOption::Flush));
            R_SUCCEED();
        }

        /* Tunnel thread: someone else on the server has our virtual IP -> pick a new one and save it. */
        int OnIpConflict(uint32_t elapsed_ms, uint32_t *new_ip) {
            if (g_session_active.load(std::memory_order_relaxed) && elapsed_ms < SessionGraceMs) {
                return 0;
            }
            /* Never block here: Reload() holds the lock while it joins this thread. */
            if (!g_lock.TryLock()) {
                return 0;
            }
            ON_SCOPE_EXIT { g_lock.Unlock(); };
            const u32 old_ip = g_config.virt_ip;
            u32 ip = old_ip;
            for (int i = 0; i < 8 && ip == old_ip; i++) {
                u8 r[2];
                os::GenerateRandomBytes(r, sizeof(r));
                ip = lp_generate_virtual_ip(r);
            }
            g_config.virt_ip = ip;
            if (R_FAILED(WriteConfigFile(std::addressof(g_config)))) {
                LogFormat("lanplay: failed to save new ip");
            }
            vnet_configure(ip, LP_SUBNET_MASK, 1);
            *new_ip = ip;
            return 1;
        }

        void StopTunnel() {
            if (lp_tunnel_running()) {
                lp_tunnel_stop();
            }
            if (g_vnet_ready) {
                vnet_configure(vnet_get_ip(), LP_SUBNET_MASK, 0);
            }
        }

        /* g_lock must be held. */
        void ApplyLocked() {
            g_mode = g_config.mode;
            LdnConfig::SetEnabledInternal(g_mode != LP_MODE_OFF);

            if (g_mode != LP_MODE_INTERNET || !g_version_ok) {
                StopTunnel();
                return;
            }

            const lp_server_t *srv = lp_config_current(std::addressof(g_config));
            if (srv == nullptr) {
                StopTunnel();
                return;
            }

            if (!g_vnet_ready) {
                vnet_init(VnetOutput);
                lp_tunnel_set_input(vnet_input);
                lp_tunnel_set_conflict_handler(OnIpConflict);
                g_vnet_ready = true;
            }

            lp_tunnel_params_t p;
            std::memset(std::addressof(p), 0, sizeof(p));
            std::strncpy(p.host, srv->host, sizeof(p.host) - 1);
            std::strncpy(p.user, srv->user, sizeof(p.user) - 1);
            std::strncpy(p.pass, srv->pass, sizeof(p.pass) - 1);
            p.port    = srv->port;
            p.pmtu    = g_config.pmtu;
            p.virt_ip = g_config.virt_ip;

            vnet_configure(g_config.virt_ip, LP_SUBNET_MASK, 1);
            lp_tunnel_start(std::addressof(p));
        }

        void LoadLocked() {
            ReadConfigFile(std::addressof(g_config));
            if (!lp_ip_is_valid_virtual(g_config.virt_ip)) {
                u8 r[2];
                os::GenerateRandomBytes(r, sizeof(r));
                g_config.virt_ip = lp_generate_virtual_ip(r);
                if (R_FAILED(WriteConfigFile(std::addressof(g_config)))) {
                    LogFormat("lanplay: failed to save generated ip");
                }
            }
        }

    }

    void Initialize() {
        const auto api = exosphere::GetApiInfo();
        g_ams_running[0] = static_cast<u16>(api.GetMajorVersion());
        g_ams_running[1] = static_cast<u16>(api.GetMinorVersion());
        g_ams_running[2] = static_cast<u16>(api.GetMicroVersion());
        /* Same major.minor is required; a different micro version only produces a warning in the overlay. */
        g_version_ok = g_ams_running[0] == ATMOSPHERE_RELEASE_VERSION_MAJOR && g_ams_running[1] == ATMOSPHERE_RELEASE_VERSION_MINOR;
        LogFormat("lanplay: AMS running %u.%u.%u built %u.%u.%u", g_ams_running[0], g_ams_running[1], g_ams_running[2],
                  ATMOSPHERE_RELEASE_VERSION_MAJOR, ATMOSPHERE_RELEASE_VERSION_MINOR, ATMOSPHERE_RELEASE_VERSION_MICRO);

        std::scoped_lock lk(g_lock);
        LoadLocked();
        ApplyLocked();
    }

    Result Reload() {
        std::scoped_lock lk(g_lock);
        LoadLocked();
        ApplyLocked();
        R_SUCCEED();
    }

    Result SetMode(u32 mode) {
        std::scoped_lock lk(g_lock);
        LoadLocked();
        g_config.mode = (mode > static_cast<u32>(LP_MODE_INTERNET)) ? static_cast<u32>(LP_MODE_LAN) : mode;
        R_TRY(WriteConfigFile(std::addressof(g_config)));
        ApplyLocked();
        R_SUCCEED();
    }

    void SetSessionActive(bool active) {
        g_session_active.store(active, std::memory_order_relaxed);
    }

    bool IsSessionActive() {
        return g_session_active.load(std::memory_order_relaxed);
    }

    u32 GetMode() {
        return g_mode;
    }

    bool UseVirtualNetwork() {
        return g_mode == LP_MODE_INTERNET && g_version_ok && lp_tunnel_running();
    }

    void GetStatus(lp_status_t *out) {
        std::scoped_lock lk(g_lock);
        lp_tunnel_get_status(out);
        out->mode = g_mode;
        out->virt_ip = g_config.virt_ip;
        if (g_mode != LP_MODE_INTERNET) {
            out->state = LP_STATE_OFF;
        } else if (!g_version_ok) {
            out->state = LP_STATE_VERSION_MISMATCH;
        } else if (lp_config_current(std::addressof(g_config)) == nullptr) {
            out->state = LP_STATE_NO_SERVER;
        }
        if (out->state != LP_STATE_CONNECTED && out->state != LP_STATE_AUTH_FAILED && out->state != LP_STATE_AUTH_REQUIRED) {
            out->ping_ms = -1;
        }
        if (out->state == LP_STATE_OFF || out->state == LP_STATE_VERSION_MISMATCH || out->state == LP_STATE_NO_SERVER) {
            out->online = -1;
        }
        out->ams_built[0] = ATMOSPHERE_RELEASE_VERSION_MAJOR;
        out->ams_built[1] = ATMOSPHERE_RELEASE_VERSION_MINOR;
        out->ams_built[2] = ATMOSPHERE_RELEASE_VERSION_MICRO;
        std::memcpy(out->ams_running, g_ams_running, sizeof(g_ams_running));
        std::strncpy(out->version, LANPLAY_VERSION, sizeof(out->version) - 1);
        out->version[sizeof(out->version) - 1] = 0;
    }

}
