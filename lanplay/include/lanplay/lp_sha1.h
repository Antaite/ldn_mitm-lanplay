/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t n; } lp_sha1_ctx;
void lp_sha1_init(lp_sha1_ctx *c);
void lp_sha1_update(lp_sha1_ctx *c, const void *data, size_t len);
void lp_sha1_final(lp_sha1_ctx *c, uint8_t out[20]);
void lp_sha1(const void *data, size_t len, uint8_t out[20]);
#ifdef __cplusplus
}
#endif
