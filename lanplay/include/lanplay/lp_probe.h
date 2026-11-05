/* Ping many lan-play servers at once (parallel DNS + lan-play PING).
 * SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "lp_types.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Blocks for at most ~3 s. n <= LP_PROBE_MAX. */
void lp_probe(const lp_probe_target_t *targets, int n, lp_probe_result_t *results);
/* Test hook: use only this DNS server (host order ip, port). */
void lp_probe_set_dns(uint32_t ip, uint16_t port);
#ifdef __cplusplus
}
#endif
