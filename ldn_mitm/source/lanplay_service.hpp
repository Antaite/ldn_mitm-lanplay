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
    /* True when the virtual (internet) network should be used and the tunnel is up. */
    bool UseVirtualNetwork();
    /* Internet mode is selected and usable (tunnel may still be in standby). */
    bool InternetEnabled();

    /* Standby: the tunnel runs only while a game uses local play, plus a grace period. */
    bool AcquireGame();          /* false = not in internet mode; waits briefly for the server */
    void ReleaseGame();
    void TouchPanel(bool wait);  /* the overlay is looking: stay connected for a minute */

    /* A game used local play but its sockets were not intercepted: it must be restarted once. */
    void SetRelaunchNeeded(u64 program_id);
    u64  GetRelaunchNeeded();
    /* LDN state hook: true while a room is open (host) or joined (station). */
    void SetSessionActive(bool active);
    bool IsSessionActive();
    /* Persisted override of the mode, used by the old SetEnabled IPC. */
    Result SetMode(u32 mode);

}
