#include <cstring>
#include "ldnmitm_config.hpp"
#include "lanplay_service.hpp"
#include "room_scan.hpp"
extern "C" {
#include "lanplay/lp_probe.h"
}
#include "debug.hpp"

namespace ams::mitm::ldn {
    std::atomic_bool LdnConfig::LdnEnabled = true;

    Result LdnConfig::GetVersion(sf::Out<LdnMitmVersion> version) {
        std::strcpy(version.GetPointer()->raw, GITDESCVER);
        
        R_SUCCEED();
    }
    Result LdnConfig::GetLogging(sf::Out<u32> enabled) {
        return ::GetLogging(enabled.GetPointer());
    }
    Result LdnConfig::SetLogging(u32 enabled) {
        return ::SetLogging(enabled);
    }
    Result LdnConfig::GetEnabled(sf::Out<u32> enabled) {
        enabled.SetValue(LdnEnabled);
        
        R_SUCCEED();
    }
    Result LdnConfig::SetEnabled(u32 enabled) {
        /* Kept for the original ldnmitm_config.nro: off <-> previous/LAN mode. */
        if (!enabled) {
            return lanplay::SetMode(LP_MODE_OFF);
        }
        if (lanplay::GetMode() == LP_MODE_OFF) {
            return lanplay::SetMode(LP_MODE_LAN);
        }
        R_SUCCEED();
    }
    Result LdnConfig::GetLanPlayStatus(const sf::OutBuffer &status) {
        /* 0x100 bytes do not fit in the IPC message as raw data, so this goes through a buffer. */
        static_assert(sizeof(LanPlayStatusRaw) == sizeof(lp_status_t));
        R_UNLESS(status.GetSize() >= sizeof(lp_status_t), MAKERESULT(0xFD, 95));
        lp_status_t st;
        std::memset(std::addressof(st), 0, sizeof(st));
        lanplay::GetStatus(std::addressof(st));
        std::memcpy(status.GetPointer(), std::addressof(st), sizeof(st));
        R_SUCCEED();
    }
    Result LdnConfig::ReloadLanPlayConfig() {
        return lanplay::Reload();
    }
    Result LdnConfig::GetMode(sf::Out<u32> mode) {
        mode.SetValue(lanplay::GetMode());
        R_SUCCEED();
    }
    Result LdnConfig::SetMode(u32 mode) {
        return lanplay::SetMode(mode);
    }
    Result LdnConfig::ScanRooms(sf::Out<u32> count, const sf::OutBuffer &rooms) {
        const size_t max = std::min<size_t>(rooms.GetSize() / sizeof(lp_room_t), LP_ROOM_MAX);
        u32 n = 0;
        Result rc = rooms::Scan(reinterpret_cast<lp_room_t *>(rooms.GetPointer()), max, std::addressof(n));
        count.SetValue(n);
        return rc;
    }
    Result LdnConfig::ProbeServers(const sf::InBuffer &targets, const sf::OutBuffer &results) {
        static constinit os::SdkMutex s_lock;
        std::scoped_lock lk(s_lock);
        const size_t n = std::min<size_t>(std::min<size_t>(targets.GetSize() / sizeof(lp_probe_target_t), results.GetSize() / sizeof(lp_probe_result_t)), LP_PROBE_MAX);
        static lp_probe_target_t t[LP_PROBE_MAX];
        std::memcpy(t, targets.GetPointer(), n * sizeof(lp_probe_target_t));
        for (size_t i = 0; i < n; i++) {
            t[i].host[sizeof(t[i].host) - 1] = 0;
        }
        lp_probe(t, static_cast<int>(n), reinterpret_cast<lp_probe_result_t *>(results.GetPointer()));
        R_SUCCEED();
    }
}
