/* lwIP compiler/platform glue for Horizon (devkitA64 + libnx/newlib).
 * SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#ifndef SSIZE_MAX
#define SSIZE_MAX LONG_MAX   /* newlib provides ssize_t */
#endif
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif
void     lp_lwip_assert(const char *msg, const char *file, int line);
uint32_t lp_lwip_rand(void);
#ifdef __cplusplus
}
#endif

#define LWIP_PLATFORM_DIAG(x)       do { } while (0)
#define LWIP_PLATFORM_ASSERT(x)     lp_lwip_assert((x), __FILE__, __LINE__)
#define LWIP_RAND()                 (lp_lwip_rand())
