/*
 * Room browser: broadcasts an ldn_mitm "Scan" on the active network
 * (lan-play virtual network or the real LAN) and collects the answers of
 * every console that currently has a room open.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stratosphere.hpp>
#include "lanplay/lp_types.h"

namespace ams::mitm::ldn::rooms {

    /* Blocks for about 1.5 s. */
    Result Scan(lp_room_t *out, size_t max_rooms, u32 *out_count);

}
