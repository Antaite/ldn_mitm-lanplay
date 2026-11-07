/* SPDX-License-Identifier: GPL-2.0-only */
#include "lanplay_service.hpp"
#include "debug.hpp"
#include "ldnmitm_config.hpp"
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <algorithm>

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
        constexpr const char ConfigTmp[]  = "sdmc:" LP_CONFIG_PATH ".tmp";
        constexpr size_t     ConfigMax    = 16 * 1024;

        constinit os::SdkMutex g_lock;
        lp_config_t            g_config;
        constinit u32          g_mode = LP_MODE_LAN;
        constinit bool         g_version_ok = true;
        constinit bool         g_vnet_ready = false;
        constinit u16          g_ams_running[3];
        constinit char         g_file_buf[ConfigMax];
        constinit std::atomic<bool> g_session_active{false};
        constinit std::atomic<bool> g_enabled{false};      /* internet mode usable (mode, AMS version, server) */
        constinit std::atomic<u64>  g_relaunch_tid{0};
        constinit bool         g_config_loaded = false;

        /* Standby: the tunnel only runs while someone needs it. */
        constexpr u64 GameGraceMs  = 30 * 1000;   /* after the last game left local play (HOME and back keeps it) */
        constexpr u64 PanelGraceMs = 60 * 1000;   /* after the overlay last asked for status */
        constinit int  g_games = 0;               /* LDN sessions in internet mode */
        constinit u64  g_game_until = 0;
        constinit u64  g_panel_until = 0;
        os::EventType  g_wake;
        os::ThreadType g_supervisor;
        alignas(os::ThreadStackAlignment) constinit u8 g_supervisor_stack[0x2000];

        u64 NowMs() {
            return static_cast<u64>(os::GetSystemTick().ToTimeSpan().GetMilliSeconds());
        }

        /* While a room is open or joined, give the other side a moment to move first. */
        constexpr u32 SessionGraceMs = 5000;

        int VnetOutput(const uint8_t *pkt, size_t len) {
            return lp_tunnel_send_ipv4(pkt, len);
        }

        bool ReadFrom(const char *path, lp_config_t *cfg) {
            fs::FileHandle f;
            if (R_FAILED(fs::OpenFile(std::addressof(f), path, fs::OpenMode_Read))) {
                return false;
            }
            ON_SCOPE_EXIT { fs::CloseFile(f); };
            s64 size = 0;
            if (R_FAILED(fs::GetFileSize(std::addressof(size), f)) || size <= 0) {
                return false;
            }
            if (size > static_cast<s64>(ConfigMax)) {
                size = ConfigMax;
            }
            if (R_FAILED(fs::ReadFile(f, 0, g_file_buf, static_cast<size_t>(size)))) {
                return false;
            }
            lp_config_parse(cfg, g_file_buf, static_cast<size_t>(size));
            return true;
        }

        /* A failed read keeps the settings already in memory instead of falling back to defaults. */
        bool ReadConfigFile(lp_config_t *cfg) {
            static lp_config_t tmp;
            if (ReadFrom(ConfigPath, std::addressof(tmp)) || ReadFrom(ConfigTmp, std::addressof(tmp))) {
                *cfg = tmp;
                g_config_loaded = true;
                return true;
            }
            if (!g_config_loaded) {
                lp_config_default(cfg);
            }
            return false;
        }

        /* Write a temporary file first: a failure half way never loses the old settings. */
        Result WriteConfigFile(const lp_config_t *cfg) {
            const size_t len = lp_config_format(cfg, g_file_buf, sizeof(g_file_buf));
            if (len == 0) {
                return MAKERESULT(0xFD, 91);
            }
            static_cast<void>(fs::CreateDirectory(ConfigDir0));
            static_cast<void>(fs::CreateDirectory(ConfigDir1));
            static_cast<void>(fs::DeleteFile(ConfigTmp));
            R_TRY(fs::CreateFile(ConfigTmp, static_cast<s64>(len)));
            {
                fs::FileHandle f;
                R_TRY(fs::OpenFile(std::addressof(f), ConfigTmp, fs::OpenMode_Write));
                ON_SCOPE_EXIT { fs::CloseFile(f); };
                R_TRY(fs::WriteFile(f, 0, g_file_buf, len, fs::WriteOption::Flush));
            }
            static_cast<void>(fs::DeleteFile(ConfigPath));
            R_TRY(fs::RenameFile(ConfigTmp, ConfigPath));
            g_config_loaded = true;
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

        void Wake() {
            os::SignalEvent(std::addressof(g_wake));
        }

        /* g_lock must be held for everything below. */
        bool EnabledLocked() {
            return g_mode == LP_MODE_INTERNET && g_version_ok && lp_config_current(std::addressof(g_config)) != nullptr;
        }

        bool WantedLocked(u64 now) {
            return EnabledLocked() && (g_games > 0 || now < g_game_until || now < g_panel_until);
        }

        void StartTunnelLocked() {
            const lp_server_t *srv = lp_config_current(std::addressof(g_config));
            if (srv == nullptr) {
                return;
            }
            lp_tunnel_params_t p;
            std::memset(std::addressof(p), 0, sizeof(p));
            std::strncpy(p.host, srv->host, sizeof(p.host) - 1);
            std::strncpy(p.user, srv->user, sizeof(p.user) - 1);
            std::strncpy(p.pass, srv->pass, sizeof(p.pass) - 1);
            p.port    = srv->port;
            p.pmtu    = g_config.pmtu;
            p.virt_ip = g_config.virt_ip;
            lp_tunnel_start(std::addressof(p));
        }

        void UpdateLocked() {
            const bool want = WantedLocked(NowMs());
            if (want && !lp_tunnel_running()) {
                LogFormat("lanplay: wake up (games %d)", g_games);
                StartTunnelLocked();
            } else if (!want && lp_tunnel_running()) {
                LogFormat("lanplay: standby");
                lp_tunnel_stop();
            }
        }

        void ApplyLocked() {
            g_mode = g_config.mode;
            LdnConfig::SetEnabledInternal(g_mode != LP_MODE_OFF);

            if (lp_tunnel_running()) {
                lp_tunnel_stop();           /* settings may have changed: restart below if still needed */
            }
            if (!EnabledLocked()) {
                g_enabled = false;
                if (g_vnet_ready) {
                    vnet_configure(vnet_get_ip(), LP_SUBNET_MASK, 0);
                }
                return;
            }

            if (!g_vnet_ready) {
                vnet_init(VnetOutput);
                lp_tunnel_set_input(vnet_input);
                lp_tunnel_set_conflict_handler(OnIpConflict);
                g_vnet_ready = true;
            }
            /* The virtual interface stays up in standby, so game sockets can bind at any time. */
            vnet_configure(g_config.virt_ip, LP_SUBNET_MASK, 1);
            g_enabled = true;
            UpdateLocked();
        }

        void Supervisor(void *) {
            for (;;) {
                s64 wait_ms = -1;
                {
                    std::scoped_lock lk(g_lock);
                    UpdateLocked();
                    if (lp_tunnel_running() && g_games == 0) {
                        const u64 now = NowMs();
                        const u64 until = std::max(g_game_until, g_panel_until);
                        wait_ms = until > now ? static_cast<s64>(until - now) + 50 : 50;
                    }
                }
                if (wait_ms < 0) {
                    os::WaitEvent(std::addressof(g_wake));
                } else {
                    os::TimedWaitEvent(std::addressof(g_wake), TimeSpan::FromMilliSeconds(wait_ms));
                }
            }
        }

        /* Wait (without the lock) until the tunnel reaches the server, or give up. */
        void WaitConnected(u64 timeout_ms) {
            const u64 end = NowMs() + timeout_ms;
            while (NowMs() < end) {
                lp_status_t st;
                lp_tunnel_get_status(std::addressof(st));
                if (!lp_tunnel_running() || st.state == LP_STATE_CONNECTED || st.state == LP_STATE_AUTH_FAILED ||
                    st.state == LP_STATE_AUTH_REQUIRED || st.state == LP_STATE_RESOLVE_FAILED) {
                    return;
                }
                os::SleepThread(TimeSpan::FromMilliSeconds(50));
            }
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

        os::InitializeEvent(std::addressof(g_wake), false, os::EventClearMode_AutoClear);
        {
            std::scoped_lock lk(g_lock);
            LoadLocked();
            ApplyLocked();
        }
        if (R_SUCCEEDED(os::CreateThread(std::addressof(g_supervisor), Supervisor, nullptr, g_supervisor_stack,
                                         sizeof(g_supervisor_stack), 0x1F, 3))) {
            os::SetThreadNamePointer(std::addressof(g_supervisor), "lanplay::Standby");
            os::StartThread(std::addressof(g_supervisor));
        }
    }

    Result Reload() {
        {
            std::scoped_lock lk(g_lock);
            g_panel_until = NowMs() + PanelGraceMs;    /* the overlay asked: it wants to see the result */
            LoadLocked();
            ApplyLocked();
        }
        Wake();
        R_SUCCEED();
    }

    bool AcquireGame() {
        {
            std::scoped_lock lk(g_lock);
            if (!EnabledLocked()) {
                return false;
            }
            g_games++;
            UpdateLocked();
        }
        Wake();
        WaitConnected(2500);
        return true;
    }

    void ReleaseGame() {
        {
            std::scoped_lock lk(g_lock);
            if (g_games > 0) {
                g_games--;
            }
            if (g_games == 0) {
                g_game_until = NowMs() + GameGraceMs;
            }
        }
        Wake();
    }

    void TouchPanel(bool wait) {
        bool running;
        {
            std::scoped_lock lk(g_lock);
            g_panel_until = NowMs() + PanelGraceMs;
            UpdateLocked();
            running = lp_tunnel_running();
        }
        Wake();
        if (wait && running) {
            WaitConnected(3000);
        }
    }

    bool InternetEnabled() {
        return g_enabled.load(std::memory_order_relaxed);
    }

    void SetRelaunchNeeded(u64 program_id) {
        g_relaunch_tid.store(program_id, std::memory_order_relaxed);
    }

    u64 GetRelaunchNeeded() {
        return g_relaunch_tid.load(std::memory_order_relaxed);
    }

    Result SetMode(u32 mode) {
        std::scoped_lock lk(g_lock);
        LoadLocked();
        g_config.mode = (mode > static_cast<u32>(LP_MODE_INTERNET)) ? static_cast<u32>(LP_MODE_LAN) : mode;
        R_TRY(WriteConfigFile(std::addressof(g_config)));
        g_panel_until = NowMs() + PanelGraceMs;
        ApplyLocked();
        Wake();
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
        return InternetEnabled() && lp_tunnel_running();
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
        } else if (!lp_tunnel_running()) {
            out->state = LP_STATE_STANDBY;
        }
        out->relaunch_tid = g_relaunch_tid.load(std::memory_order_relaxed);
        if (out->state != LP_STATE_CONNECTED && out->state != LP_STATE_AUTH_FAILED && out->state != LP_STATE_AUTH_REQUIRED) {
            out->ping_ms = -1;
        }
        if (out->state == LP_STATE_OFF || out->state == LP_STATE_VERSION_MISMATCH || out->state == LP_STATE_NO_SERVER || out->state == LP_STATE_STANDBY) {
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
