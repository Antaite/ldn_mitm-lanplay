/* SPDX-License-Identifier: GPL-2.0-only
 * Tiny platform layer implemented by port/switch (ams::os) and port/host (pthreads). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { LP_THREAD_TUNNEL = 0, LP_THREAD_INFO = 1, LP_THREAD_BRIDGE = 2, LP_THREAD_COUNT = 3 };

int      lp_thread_start(int slot, void (*fn)(void *), void *arg);   /* 0 on success */
void     lp_thread_join(int slot);
uint64_t lp_now_ms(void);
void     lp_sleep_ms(uint32_t ms);
void     lp_random(void *buf, size_t len);
uint32_t lp_platform_dns_server(void);   /* host order, 0 = unknown */
uint32_t lp_platform_local_ip(void);     /* console's own LAN address (any order), 0 = offline/unknown */

/* One wake-up signal for the bridge pump, so it can sleep for good while idle. */
#define LP_WAIT_FOREVER 0xFFFFFFFFu
void     lp_wait_signal(uint32_t ms);
void     lp_signal(void);
void     lp_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Simple lock used for status snapshots. */
void     lp_lock(void);
void     lp_unlock(void);

#ifdef __cplusplus
}
#endif
