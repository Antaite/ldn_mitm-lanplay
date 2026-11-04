#pragma once
#include <stratosphere.hpp>

namespace ams::mitm::ldn {

    struct LdnMitmVersion {
        char raw[32];
    };

    /* Same layout as lp_status_t (lanplay/include/lanplay/lp_types.h). */
    struct LanPlayStatusRaw {
        u8 raw[0x100];
    };
    static_assert(sizeof(LanPlayStatusRaw) == 0x100);

}

#define AMS_LDN_CONFIG(C, H)                                                                									\
    AMS_SF_METHOD_INFO(C, H, 65001, Result, GetVersion, 	(ams::sf::Out<ams::mitm::ldn::LdnMitmVersion> version), (version))  \
    AMS_SF_METHOD_INFO(C, H, 65002, Result, GetLogging, 	(ams::sf::Out<u32> enabled), 							(enabled))	\
    AMS_SF_METHOD_INFO(C, H, 65003, Result, SetLogging, 	(u32 enabled), 											(enabled))	\
    AMS_SF_METHOD_INFO(C, H, 65004, Result, GetEnabled, 	(ams::sf::Out<u32> enabled), 							(enabled))	\
    AMS_SF_METHOD_INFO(C, H, 65005, Result, SetEnabled, 	(u32 enabled), 											(enabled))	\
    AMS_SF_METHOD_INFO(C, H, 65100, Result, GetLanPlayStatus,    (ams::sf::Out<ams::mitm::ldn::LanPlayStatusRaw> status), (status)) \
    AMS_SF_METHOD_INFO(C, H, 65101, Result, ReloadLanPlayConfig, (),                                                     ())       \
    AMS_SF_METHOD_INFO(C, H, 65102, Result, GetMode,             (ams::sf::Out<u32> mode),                               (mode))   \
    AMS_SF_METHOD_INFO(C, H, 65103, Result, SetMode,             (u32 mode),                                             (mode))   \
    AMS_SF_METHOD_INFO(C, H, 65104, Result, ScanRooms,           (ams::sf::Out<u32> count, const ams::sf::OutBuffer &rooms), (count, rooms))

    AMS_SF_DEFINE_INTERFACE(ams::mitm::ldn, ILdnConfig, AMS_LDN_CONFIG, 0x14c8af2c)
