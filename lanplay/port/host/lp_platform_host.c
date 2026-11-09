/* Host (Linux) implementation of lp_platform.h used by the test harness. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "lanplay/lp_platform.h"
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

static pthread_t g_threads[LP_THREAD_COUNT];
static int g_started[LP_THREAD_COUNT];
static pthread_mutex_t g_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
typedef struct { void (*fn)(void *); void *arg; } tramp_t;
static tramp_t g_tramp[LP_THREAD_COUNT];

static void *tramp(void *p) { tramp_t *t = p; t->fn(t->arg); return NULL; }

int lp_thread_start(int slot, void (*fn)(void *), void *arg) {
    g_tramp[slot].fn = fn; g_tramp[slot].arg = arg;
    if (pthread_create(&g_threads[slot], NULL, tramp, &g_tramp[slot]) != 0) return -1;
    g_started[slot] = 1;
    return 0;
}
void lp_thread_join(int slot) {
    if (g_started[slot]) { pthread_join(g_threads[slot], NULL); g_started[slot] = 0; }
}
uint64_t lp_now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
void lp_sleep_ms(uint32_t ms) { usleep(ms * 1000); }
void lp_random(void *buf, size_t len) { for (size_t i = 0; i < len; i++) ((uint8_t *)buf)[i] = (uint8_t)rand(); }
uint32_t lp_platform_dns_server(void) { return 0; }
uint32_t lp_platform_local_ip(void) { return 0; }   /* no network watch on the PC tool */

static pthread_mutex_t g_sig_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_sig_c = PTHREAD_COND_INITIALIZER;
static int             g_sig;
void lp_wait_signal(uint32_t ms) {
    pthread_mutex_lock(&g_sig_m);
    if (!g_sig) {
        if (ms == LP_WAIT_FOREVER) {
            while (!g_sig) pthread_cond_wait(&g_sig_c, &g_sig_m);
        } else {
            struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&g_sig_c, &g_sig_m, &ts);
        }
    }
    g_sig = 0;
    pthread_mutex_unlock(&g_sig_m);
}
void lp_signal(void) {
    pthread_mutex_lock(&g_sig_m);
    g_sig = 1;
    pthread_cond_signal(&g_sig_c);
    pthread_mutex_unlock(&g_sig_m);
}
void lp_lock(void) { pthread_mutex_lock(&g_lock); }
void lp_unlock(void) { pthread_mutex_unlock(&g_lock); }
void lp_log(const char *fmt, ...) {
    if (getenv("LP_QUIET")) return;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[lp] "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}
