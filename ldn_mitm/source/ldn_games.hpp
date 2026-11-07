/*
 * Which games use local play (ldn). Only these get their sockets intercepted
 * in internet mode, so every other game and app keeps its network untouched.
 *
 * Built-in list of well-known local play games, plus every game seen calling
 * ldn while in internet mode (sdmc:/config/ldn_mitm/ldngames.bin). A game that
 * is not known yet works after one restart.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stratosphere.hpp>

namespace ams::mitm::ldn::games {

    bool IsKnown(u64 program_id);
    /* Remember a game that used local play. Returns true when it was new. */
    bool Learn(u64 program_id);

}
