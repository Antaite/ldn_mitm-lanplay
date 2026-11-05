/* Built-in public lan-play servers.
 * SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;      /* shown in the overlay */
    const char *region;    /* short region label */
    const char *host;
    uint16_t    port;
    const char *note;      /* e.g. game the server is dedicated to, may be "" */
} lp_preset_t;

extern const lp_preset_t lp_presets[];
extern const int         lp_preset_count;

/* index or -1 */
int lp_preset_find(const char *host, uint16_t port);

#ifdef __cplusplus
}
#endif
