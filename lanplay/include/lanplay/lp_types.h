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
#define LP_MAX_SERVERS      32
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
    LP_STATE_STANDBY        = 11,   /* internet mode, no game using local play: not connected */
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
    uint64_t relaunch_tid;     /* game that must be restarted once before it can play online, 0 = none */
    /* traffic counters (since the current room started) */
    uint32_t up_fail;          /* datagrams to the server the network refused */
    uint32_t game_tx;          /* game -> peer datagrams sent through the bridge */
    uint32_t game_tx_fail;     /* ... that could not be sent */
    uint32_t game_rx;          /* peer -> game datagrams handed to the game's socket */
    uint32_t game_rx_fail;     /* ... that could not be handed over */
    uint32_t game_delivered;   /* ... that the game actually read */
    uint32_t game_leaked;      /* game sent to a 10.13.x.x peer outside the bridge (lost) */
    uint32_t rebuilds;         /* times the server socket was rebuilt since the tunnel started */
    uint8_t  dup_active;       /* sending every packet twice (loss seen while in a room) */
    uint8_t  loss_pct;         /* ping loss to the server over the last ~20 s in a room */
    uint8_t  _pad2[2];
    uint8_t  _reserved[0x100 - 96 - 32 - 4*4 - 2 - 12 - 2 - 8 - 16 - 8 - 8 - 7*4 - 8];
} lp_status_t;

/* Server probe (overlay server list). */
#define LP_PROBE_MAX        48
typedef struct {
    char     host[128];
    uint16_t port;
    uint16_t _pad[3];
} lp_probe_target_t;              /* 0x88 bytes */

enum { LP_PROBE_OK = 0, LP_PROBE_DNS_FAILED = 1, LP_PROBE_NO_RESPONSE = 2 };
typedef struct {
    int32_t  status;               /* LP_PROBE_* */
    int32_t  ping_ms;              /* -1 unless OK */
} lp_probe_result_t;

/* One open room found by a scan (host in "create room" state). 0x120 bytes. */
#define LP_ROOM_MAX         16
#define LP_ROOM_PLAYERS     8
typedef struct {
    uint64_t local_comm_id;    /* usually the game's application (title) ID */
    uint32_t host_ip;          /* host order */
    uint16_t scene_id;
    int16_t  comm_version;     /* host's localCommunicationVersion */
    uint8_t  node_count;
    uint8_t  node_max;
    uint8_t  has_password;
    uint8_t  is_own;           /* the room this console is hosting */
    char     players[LP_ROOM_PLAYERS][33];   /* players[0] = host */
    int16_t  own_version;      /* this console's localCommunicationVersion for the game */
    uint8_t  own_known;        /* own_version is valid (hosted/joined this game before) */
    uint8_t  _reserved[0x120 - 20 - LP_ROOM_PLAYERS * 33 - 3];
} lp_room_t;

#ifdef __cplusplus
}
#endif
