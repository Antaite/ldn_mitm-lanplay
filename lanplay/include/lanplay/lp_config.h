/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "lp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void   lp_config_default(lp_config_t *cfg);
/* Parse an ini buffer (need not be NUL terminated). Unknown keys are ignored. */
void   lp_config_parse(lp_config_t *cfg, const char *buf, size_t len);
/* Returns bytes written (excluding NUL) or 0 if cap too small. */
size_t lp_config_format(const lp_config_t *cfg, char *buf, size_t cap);

int    lp_config_add_server(lp_config_t *cfg, const lp_server_t *srv);   /* index or -1 */
void   lp_config_remove_server(lp_config_t *cfg, int index);
const lp_server_t *lp_config_current(const lp_config_t *cfg);

/* Random 10.13.x.y avoiding .0/.255 and the lan-play gateway range. */
uint32_t lp_generate_virtual_ip(const uint8_t random_bytes[2]);
int      lp_ip_is_valid_virtual(uint32_t ip);
void     lp_ip_to_str(uint32_t ip_host, char out[16]);
int      lp_str_to_ip(const char *s, uint32_t *ip_host);

const char *lp_mode_name(uint32_t mode);       /* "off" / "lan" / "internet" */

#ifdef __cplusplus
}
#endif
