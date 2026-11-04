#include <cstring>
#include "ldnmitm_config.hpp"
#include "lanplay_service.hpp"
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
    Result LdnConfig::GetLanPlayStatus(sf::Out<LanPlayStatusRaw> status) {
        static_assert(sizeof(LanPlayStatusRaw) == sizeof(lp_status_t));
        lp_status_t st;
        std::memset(std::addressof(st), 0, sizeof(st));
        lanplay::GetStatus(std::addressof(st));
        std::memcpy(status.GetPointer()->raw, std::addressof(st), sizeof(st));
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
}
