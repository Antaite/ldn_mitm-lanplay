/*
 * lanplay core - shared types between the ldn_mitm sysmodule, the Tesla
 * overlay and the host test harness.
 *
 * Copyright (c) 2026 Antaite
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LP_CONFIG_PATH      "/config/ldn_mitm/lanplay.ini"
#define LP_MAX_SERVERS      16
#define LP_DEFAULT_PORT     11451
#define LP_SUBNET_NET       0x0A0D0000u   /* 10.13.0.0   (host order) */
#define LP_SUBNET_MASK      0xFFFF0000u   /* 255.255.0.0 (host order) */
#define LP_GATEWAY_IP       0x0A0D2501u   /* 10.13.37.1  reserved by lan-play */

typedef enum {
    LP_MODE_OFF      = 0,   /* ldn_mitm disabled: games use real local wireless */
    LP_MODE_LAN      = 1,   /* same LAN (original ldn_mitm behaviour)          */
    LP_MODE_INTERNET = 2,   /* lan-play tunnel over the internet                */
} lp_mode_t;

typedef struct {
    char     name[32];
    char     host[128];
    uint16_t port;
    char     user[32];
    char     pass[64];
} lp_server_t;

typedef struct {
    uint32_t    mode;          /* lp_mode_t */
    int32_t     current;       /* index into servers, -1 = none */
    int32_t     count;
    uint32_t    virt_ip;       /* host order, 0 = generate */
    uint32_t    pmtu;          /* 0 = no lan-play fragmentation */
    lp_server_t servers[LP_MAX_SERVERS];
} lp_config_t;

typedef enum {
    LP_STATE_OFF            = 0,
    LP_STATE_RESOLVING      = 1,
    LP_STATE_CONNECTING     = 2,
    LP_STATE_CONNECTED      = 3,
    LP_STATE_RESOLVE_FAILED = 4,
    LP_STATE_NO_RESPONSE    = 5,
    LP_STATE_AUTH_REQUIRED  = 6,
    LP_STATE_AUTH_FAILED    = 7,
    LP_STATE_NO_NETWORK     = 8,
    LP_STATE_VERSION_MISMATCH = 9,
    LP_STATE_NO_SERVER      = 10,
} lp_state_t;

/* Fixed 0x100 byte POD, sent as-is over IPC. */
typedef struct {
    uint32_t state;            /* lp_state_t */
    uint32_t mode;             /* lp_mode_t (effective) */
    uint32_t virt_ip;          /* host order */
    uint32_t server_ip;        /* host order */
    uint16_t server_port;
    uint16_t ams_built[3];     /* AMS version the sysmodule was built for */
    uint16_t ams_running[3];   /* AMS version running on the console */
    uint16_t _pad0;
    int32_t  ping_ms;          /* -1 unknown */
    int32_t  online;           /* -1 unknown */
    uint64_t up_bytes;
    uint64_t down_bytes;
    uint32_t up_packets;
    uint32_t down_packets;
    char     server_msg[96];
    char     version[32];
    uint8_t  _reserved[0x100 - 96 - 32 - 4*4 - 2 - 12 - 2 - 8 - 16 - 8];
} lp_status_t;

#ifdef __cplusplus
}
#endif
