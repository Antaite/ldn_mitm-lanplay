/* SPDX-License-Identifier: GPL-2.0-only */
#include "lanplay/lp_bridge.h"
#include "lanplay/lp_platform.h"
#include "lanplay/vnet.h"

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define POLL_MS 20
#define IDLE_MS 200        /* nothing bridged: wake rarely */

typedef struct {
    lp_bridge_sock_t s;
    int used;
} slot_t;

static slot_t   g_slots[LP_BRIDGE_MAX];
static int32_t  g_close_q[LP_BRIDGE_MAX];   /* twins to close on the pump thread */
static int      g_close_n;
static int      g_started;
static int      g_inject_fd = -1;
static uint16_t g_inject_port;

static const uint8_t k_magic[4] = { 'L', 'P', 'B', '1' };

int lp_bridge_is_virtual(uint32_t ip, int session_active) {
    if ((ip & 0xFFFF0000u) == 0x0A0D0000u) return 1;
    return session_active && ip == 0xFFFFFFFFu;
}

uint16_t lp_bridge_inject_port(void) { return g_inject_port; }

int lp_bridge_parse(const uint8_t *buf, size_t len, uint32_t *ip, uint16_t *port) {
    if (len < LP_BRIDGE_HDR || memcmp(buf, k_magic, 4) != 0) return 0;
    *ip   = (uint32_t)buf[4] << 24 | (uint32_t)buf[5] << 16 | (uint32_t)buf[6] << 8 | buf[7];
    *port = (uint16_t)(buf[8] << 8 | buf[9]);
    return 1;
}

static slot_t *find(uint64_t owner, int fd) {
    for (int i = 0; i < LP_BRIDGE_MAX; i++) {
        if (g_slots[i].used && g_slots[i].s.owner == owner && g_slots[i].s.fd == fd) return &g_slots[i];
    }
    return NULL;
}

static void drop(slot_t *sl) {           /* lp_lock held */
    if (sl->s.twin >= 0 && g_close_n < LP_BRIDGE_MAX) g_close_q[g_close_n++] = sl->s.twin;
    memset(sl, 0, sizeof *sl);
}

int lp_bridge_open(uint64_t owner, int fd, uint16_t port, uint32_t inject_ip, int virt_bound) {
    if (!g_started || port == 0) return -1;
    lp_lock();
    slot_t *sl = find(owner, fd);
    if (sl) {                            /* re-bind of a tracked socket */
        sl->s.virt_bound = (uint8_t)virt_bound;
        sl->s.inject_ip  = inject_ip;
        int ok = sl->s.port == port ? 0 : -1;
        lp_unlock();
        return ok;
    }
    for (int i = 0; i < LP_BRIDGE_MAX && !sl; i++) if (!g_slots[i].used) sl = &g_slots[i];
    if (!sl) { lp_unlock(); lp_log("bridge: table full"); return -1; }
    sl->used = 1;                        /* reserve before the (slow) vnet calls */
    sl->s.owner = owner; sl->s.fd = fd; sl->s.twin = -1;
    lp_unlock();

    int t = vnet_socket(VNET_SOCK_DGRAM);
    if (t >= 0) {
        vnet_setopt(t, VNET_OPT_REUSEADDR, 1);
        vnet_setopt(t, VNET_OPT_BROADCAST, 1);
        if (vnet_bind(t, 0, port) != 0) { vnet_close(t); t = -1; }
    }

    lp_lock();
    if (t < 0) {
        memset(sl, 0, sizeof *sl);
        lp_unlock();
        lp_log("bridge: no twin for port %u", (unsigned)port);
        return -1;
    }
    sl->s.twin = t;
    sl->s.port = port;
    sl->s.inject_ip = inject_ip;
    sl->s.virt_bound = (uint8_t)virt_bound;
    lp_unlock();
    lp_log("bridge: port %u bridged (owner %llu fd %d)", (unsigned)port, (unsigned long long)owner, fd);
    return 0;
}

int lp_bridge_get(uint64_t owner, int fd, lp_bridge_sock_t *out) {
    lp_lock();
    slot_t *sl = find(owner, fd);
    if (sl && out) *out = sl->s;
    lp_unlock();
    return sl != NULL;
}

int lp_bridge_set_peer(uint64_t owner, int fd, int has_peer, uint32_t ip, uint16_t port) {
    lp_lock();
    slot_t *sl = find(owner, fd);
    if (sl) { sl->s.has_peer = (uint8_t)has_peer; sl->s.peer_ip = ip; sl->s.peer_port = port; }
    lp_unlock();
    return sl ? 0 : -1;
}

void lp_bridge_close(uint64_t owner, int fd) {
    lp_lock();
    slot_t *sl = find(owner, fd);
    if (sl) drop(sl);
    lp_unlock();
}

void lp_bridge_close_owner(uint64_t owner) {
    lp_lock();
    for (int i = 0; i < LP_BRIDGE_MAX; i++) if (g_slots[i].used && g_slots[i].s.owner == owner) drop(&g_slots[i]);
    lp_unlock();
}

long lp_bridge_sendto(uint64_t owner, int fd, const void *buf, size_t len, uint32_t ip, uint16_t port) {
    lp_bridge_sock_t s;
    if (!lp_bridge_get(owner, fd, &s) || s.twin < 0) return -1;
    if (ip == 0xFFFFFFFFu) ip = (vnet_get_ip() & vnet_get_mask()) | ~vnet_get_mask();   /* 10.13.255.255 */
    return vnet_sendto(s.twin, buf, len, ip, port);
}

/* ------------------------------------------------------------ pump */

static void pump(void *arg) {
    (void)arg;
    static uint8_t buf[LP_BRIDGE_HDR + LP_BRIDGE_MAX_DGRAM];
    struct vnet_pollfd pf[LP_BRIDGE_MAX];
    uint32_t to_ip[LP_BRIDGE_MAX];
    uint16_t to_port[LP_BRIDGE_MAX];
    memcpy(buf, k_magic, 4);

    for (;;) {
        int32_t closing[LP_BRIDGE_MAX];
        int nc, n = 0;
        lp_lock();
        nc = g_close_n;
        memcpy(closing, g_close_q, sizeof(int32_t) * (size_t)nc);
        g_close_n = 0;
        for (int i = 0; i < LP_BRIDGE_MAX; i++) {
            if (!g_slots[i].used || g_slots[i].s.twin < 0) continue;
            pf[n].fd = g_slots[i].s.twin; pf[n].events = VNET_POLLIN; pf[n].revents = 0;
            to_ip[n] = g_slots[i].s.inject_ip; to_port[n] = g_slots[i].s.port;
            n++;
        }
        lp_unlock();
        for (int i = 0; i < nc; i++) vnet_close(closing[i]);

        if (n == 0) { lp_sleep_ms(IDLE_MS); continue; }
        if (vnet_poll(pf, (unsigned)n, POLL_MS) <= 0) continue;

        for (int i = 0; i < n; i++) {
            if (!(pf[i].revents & VNET_POLLIN)) continue;
            for (int k = 0; k < 16; k++) {             /* drain, bounded */
                uint32_t ip; uint16_t port;
                long r = vnet_recvfrom(pf[i].fd, buf + LP_BRIDGE_HDR, LP_BRIDGE_MAX_DGRAM, &ip, &port, 1);
                if (r < 0) break;
                buf[4] = (uint8_t)(ip >> 24); buf[5] = (uint8_t)(ip >> 16); buf[6] = (uint8_t)(ip >> 8); buf[7] = (uint8_t)ip;
                buf[8] = (uint8_t)(port >> 8); buf[9] = (uint8_t)port; buf[10] = buf[11] = 0;
                struct sockaddr_in to;
                memset(&to, 0, sizeof to);
#ifdef __SWITCH__
                to.sin_len = sizeof to;
#endif
                to.sin_family = AF_INET;
                to.sin_port = htons(to_port[i]);
                to.sin_addr.s_addr = htonl(to_ip[i]);
                sendto(g_inject_fd, buf, (size_t)r + LP_BRIDGE_HDR, 0, (struct sockaddr *)&to, sizeof to);
            }
        }
    }
}

int lp_bridge_start(void) {
    if (g_started) return 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
#ifdef __SWITCH__
    a.sin_len = sizeof a;
#endif
    a.sin_family = AF_INET;
    socklen_t l = sizeof a;
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || getsockname(fd, (struct sockaddr *)&a, &l) != 0) {
        close(fd);
        return -1;
    }
    g_inject_fd = fd;
    g_inject_port = ntohs(a.sin_port);
    g_started = 1;
    if (lp_thread_start(LP_THREAD_BRIDGE, pump, NULL) != 0) {
        g_started = 0;
        close(fd);
        g_inject_fd = -1;
        return -1;
    }
    lp_log("bridge: ready (loopback port %u)", (unsigned)g_inject_port);
    return 0;
}
