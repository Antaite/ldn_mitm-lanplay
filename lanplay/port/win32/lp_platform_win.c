/* Windows implementation of lp_platform.h, used by the PC test tool.
 * SPDX-License-Identifier: GPL-2.0-only */
#include "lanplay/lp_platform.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static HANDLE g_threads[LP_THREAD_COUNT];
typedef struct { void (*fn)(void *); void *arg; } tramp_t;
static tramp_t g_tramp[LP_THREAD_COUNT];
static CRITICAL_SECTION g_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_lock(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_lock);   /* re-entrant, like the other ports */
    return TRUE;
}

static DWORD WINAPI tramp(LPVOID p) { tramp_t *t = p; t->fn(t->arg); return 0; }

int lp_thread_start(int slot, void (*fn)(void *), void *arg) {
    g_tramp[slot].fn = fn; g_tramp[slot].arg = arg;
    g_threads[slot] = CreateThread(NULL, 0, tramp, &g_tramp[slot], 0, NULL);
    return g_threads[slot] ? 0 : -1;
}

void lp_thread_join(int slot) {
    if (g_threads[slot]) {
        WaitForSingleObject(g_threads[slot], INFINITE);
        CloseHandle(g_threads[slot]);
        g_threads[slot] = NULL;
    }
}

uint64_t lp_now_ms(void) { return (uint64_t)GetTickCount64(); }
void lp_sleep_ms(uint32_t ms) { Sleep(ms); }
void lp_random(void *buf, size_t len) { for (size_t i = 0; i < len; i++) ((uint8_t *)buf)[i] = (uint8_t)rand(); }
uint32_t lp_platform_dns_server(void) { return 0; }   /* falls back to 1.1.1.1 / 8.8.8.8 / system resolver */
uint32_t lp_platform_local_ip(void) { return 0; }     /* no network watch on the PC tool */

static HANDLE g_sig;
static INIT_ONCE g_sig_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK init_sig(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o; (void)p; (void)c;
    g_sig = CreateEventA(NULL, FALSE, FALSE, NULL);   /* auto-reset */
    return TRUE;
}
void lp_wait_signal(uint32_t ms) {
    InitOnceExecuteOnce(&g_sig_once, init_sig, NULL, NULL);
    WaitForSingleObject(g_sig, ms == LP_WAIT_FOREVER ? INFINITE : ms);
}
void lp_signal(void) {
    InitOnceExecuteOnce(&g_sig_once, init_sig, NULL, NULL);
    SetEvent(g_sig);
}

void lp_lock(void) { InitOnceExecuteOnce(&g_once, init_lock, NULL, NULL); EnterCriticalSection(&g_lock); }
void lp_unlock(void) { LeaveCriticalSection(&g_lock); }

void lp_log(const char *fmt, ...) {
    if (getenv("LP_QUIET") && getenv("LP_QUIET")[0]) return;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[lp] "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

