/*
 * Game socket bridge: carries a game's own UDP traffic between its real
 * (Horizon) socket and the virtual lan-play network.
 *
 *   game -> peer : the bsd:u mitm sees sendto(10.13.x.x) and sends the payload
 *                  from a "twin" vnet socket bound to the same port.
 *   peer -> game : the pump thread reads the twin and re-sends the datagram to
 *                  the game's real socket over loopback, prefixed with a small
 *                  header carrying the real (virtual) sender; the mitm strips
 *                  it again in recv/recvfrom.
 *
 * The game keeps a real socket the whole time, so poll/select/blocking calls
 * keep working without being emulated. All addresses/ports in HOST order.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define LP_BRIDGE_MAX       32
#define LP_BRIDGE_HDR       12          /* "LPB1" + ip(4) + port(2) + pad(2) */
#define LP_BRIDGE_MAX_DGRAM 8192        /* lwIP/tunnel fragment anything above the MTU */

typedef struct {
    uint64_t owner;        /* process id of the game */
    int32_t  fd;           /* game's real fd */
    int32_t  twin;         /* vnet fd, -1 = none */
    uint16_t port;         /* local port (same on both sides) */
    uint8_t  virt_bound;   /* game bound to its virtual IP */
    uint8_t  has_peer;     /* connect()ed to a virtual peer */
    uint32_t inject_ip;    /* where the pump delivers (127.0.0.1 or the bound IP) */
    uint32_t peer_ip;
    uint16_t peer_port;
} lp_bridge_sock_t;

/* True for 10.13.0.0/16; 255.255.255.255 counts only while a room is active. */
int lp_bridge_is_virtual(uint32_t ip, int session_active);

/* Start the pump thread and its loopback sender (idempotent). 0 = ok. */
int lp_bridge_start(void);
uint16_t lp_bridge_inject_port(void);

/* Create the twin for a bound game socket. 0 = ok (or already exists). */
int  lp_bridge_open(uint64_t owner, int fd, uint16_t port, uint32_t inject_ip, int virt_bound);
int  lp_bridge_get(uint64_t owner, int fd, lp_bridge_sock_t *out);   /* 1 = found */
int  lp_bridge_set_peer(uint64_t owner, int fd, int has_peer, uint32_t ip, uint16_t port);
void lp_bridge_close(uint64_t owner, int fd);
void lp_bridge_close_owner(uint64_t owner);

/* Send from the twin. Returns bytes sent or -1. */
long lp_bridge_sendto(uint64_t owner, int fd, const void *buf, size_t len, uint32_t ip, uint16_t port);

/* Traffic counters, for the overlay and the log. */
typedef struct {
    uint32_t tx, tx_fail;       /* game -> peers */
    uint32_t rx, rx_fail;       /* peers -> game socket (loopback inject) */
    uint32_t delivered;         /* injected datagrams the game read */
    uint32_t leaked;            /* game sent to a virtual peer outside the bridge */
} lp_bridge_stats_t;
enum { LP_BRIDGE_NOTE_DELIVERED, LP_BRIDGE_NOTE_LEAKED, LP_BRIDGE_NOTE_TX_FAIL };
void lp_bridge_note(int what);
void lp_bridge_get_stats(lp_bridge_stats_t *out);
void lp_bridge_reset_stats(void);

/* Loopback datagram from the pump? Fills the real sender and returns 1. */
int lp_bridge_parse(const uint8_t *buf, size_t len, uint32_t *ip, uint16_t *port);

#ifdef __cplusplus
}
#endif
