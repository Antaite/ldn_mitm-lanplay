#pragma once

#include <switch.h>
#include <stratosphere.hpp>
#include "interfaces/iconfig.hpp"

namespace ams::mitm::ldn {

    class LdnConfig {
        public:
            static bool getEnabled() {
                return LdnEnabled;
            }
            static void SetEnabledInternal(bool enabled) {
                LdnEnabled = enabled;
            }
        protected:
            static std::atomic_bool LdnEnabled;
        public:

            Result GetVersion(sf::Out<LdnMitmVersion> version);
            Result GetLogging(sf::Out<u32> enabled);
            Result SetLogging(u32 enabled);
            Result GetEnabled(sf::Out<u32> enabled);
            Result SetEnabled(u32 enabled);
            /* lan-play extensions */
            Result GetLanPlayStatus(sf::Out<LanPlayStatusRaw> status);
            Result ReloadLanPlayConfig();
            Result GetMode(sf::Out<u32> mode);
            Result SetMode(u32 mode);
            Result ScanRooms(sf::Out<u32> count, const sf::OutBuffer &rooms);
    };
    static_assert(ams::mitm::ldn::IsILdnConfig<LdnConfig>);

}
