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
void lp_lock(void) { pthread_mutex_lock(&g_lock); }
void lp_unlock(void) { pthread_mutex_unlock(&g_lock); }
void lp_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[lp] "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}
