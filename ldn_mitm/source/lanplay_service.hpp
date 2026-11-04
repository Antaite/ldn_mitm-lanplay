/*
 * Glue between ldn_mitm and the lan-play tunnel: config file, mode switching,
 * AMS version check and status reporting.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stratosphere.hpp>
#include "lanplay/lp_types.h"

namespace ams::mitm::ldn::lanplay {

    struct LanPlayStatus {
        lp_status_t raw;
    };
    static_assert(sizeof(LanPlayStatus) == 0x100);

    /* Called once at boot after sockets/nifm/fs are up. */
    void Initialize();
    /* Re-read sdmc:/config/ldn_mitm/lanplay.ini and apply it. */
    Result Reload();
    void   GetStatus(lp_status_t *out);

    /* Effective mode (lp_mode_t). */
    u32  GetMode();
    /* True when the virtual (internet) network should be used for new sessions. */
    bool UseVirtualNetwork();
    /* Persisted override of the mode, used by the old SetEnabled IPC. */
    Result SetMode(u32 mode);

}
