/* Minimal DNS A-record query builder/parser (no sockets).
 * SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Returns query length, 0 on bad host name. */
size_t lp_dns_build(const char *host, uint16_t id, uint8_t *out, size_t cap);
/* Returns 0 and fills id and ip (host order) when the reply holds an A record. */
int    lp_dns_parse(const uint8_t *r, size_t n, uint16_t *id, uint32_t *ip);
#ifdef __cplusplus
}
#endif
