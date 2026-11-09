/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

#define VS_TEST_GAME_ID   0x0100000000005653ull   /* shows as an unknown game: "VS" */
#define VS_PING_MAX       4000
#define VS_PING_COUNT_MAX 64

typedef struct {
    uint64_t game_id;
    uint32_t host_ip;
    int16_t  comm_version;
    uint8_t  players, players_max;
    char     host_name[33];
} vs_room_t;

typedef struct {
    int size, sent, send_fail, received;
    int min_ms, avg_ms, max_ms;
} vs_ping_result_t;

/* Broadcast an ldn_mitm scan on the virtual network; returns rooms found or -1. */
int vs_scan_rooms(vs_room_t *out, int max, int ms);

/* Answer scans with a test room; cb is called with the scanner's virtual IP. */
typedef void (*vs_scan_cb)(uint32_t ip);
int  vs_host_start(vs_scan_cb cb);
void vs_host_stop(void);

/* ICMP echo through the server: count packets of `size` bytes, interval_ms apart. */
int vs_ping_run(uint32_t ip, int size, int count, int interval_ms, int timeout_ms, vs_ping_result_t *res);
