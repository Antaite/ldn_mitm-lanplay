/* SPDX-License-Identifier: GPL-2.0-only
 * lan-play client: tunnels raw IPv4 packets of the virtual 10.13.0.0/16
 * network to a switch-lan-play relay server over UDP. */
#pragma once
#include "lp_types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     host[128];
    uint16_t port;
    char     user[32];
    char     pass[64];
    uint32_t pmtu;       /* 0 = never fragment */
    uint32_t virt_ip;    /* host order, for status only */
} lp_tunnel_params_t;

/* Called (from the tunnel thread) for every IPv4 packet received from the server. */
typedef void (*lp_ipv4_input_fn)(const uint8_t *pkt, size_t len);

/* Called (from the tunnel thread) while another client on the server is using our
 * virtual IP. elapsed_ms = time since the conflict was first seen. Return 1 and set
 * *new_ip (host order) to switch to a new address, or 0 to be asked again later. */
typedef int (*lp_ip_conflict_fn)(uint32_t elapsed_ms, uint32_t *new_ip);

void lp_tunnel_set_input(lp_ipv4_input_fn fn);
void lp_tunnel_set_conflict_handler(lp_ip_conflict_fn fn);
int  lp_tunnel_start(const lp_tunnel_params_t *params);
void lp_tunnel_stop(void);
int  lp_tunnel_running(void);
/* Thread safe; drops the packet if not connected. Returns 0 / -1. */
int  lp_tunnel_send_ipv4(const uint8_t *pkt, size_t len);
void lp_tunnel_get_status(lp_status_t *out);

#ifdef __cplusplus
}
#endif
