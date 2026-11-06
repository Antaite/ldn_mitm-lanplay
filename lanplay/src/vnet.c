/* SPDX-License-Identifier: GPL-2.0-only */
#include "lanplay/vnet.h"

#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/ip4.h"
#include "lwip/sockets.h"
#include <string.h>

static struct netif   g_netif;
static vnet_output_fn g_out;
static volatile int   g_inited;
static volatile int   g_up;
static uint32_t       g_ip, g_mask;

static err_t vnet_netif_output(struct netif *nif, struct pbuf *p, const ip4_addr_t *dst) {
    (void)nif; (void)dst;
    uint8_t buf[1600];
    if (!g_out || !g_up || p->tot_len > sizeof(buf)) return ERR_IF;
    u16_t n = pbuf_copy_partial(p, buf, p->tot_len, 0);
    g_out(buf, n);
    return ERR_OK;
}

static err_t vnet_netif_init(struct netif *nif) {
    nif->name[0] = 'l'; nif->name[1] = 'p';
    nif->mtu = 1500;
    nif->output = vnet_netif_output;
    nif->flags = NETIF_FLAG_BROADCAST;      /* raw IP, no ARP */
    return ERR_OK;
}

static void init_done(void *arg) { *(volatile int *)arg = 1; }

int vnet_init(vnet_output_fn out) {
    g_out = out;
    if (g_inited) return 0;
    volatile int done = 0;
    tcpip_init(init_done, (void *)&done);
    while (!done) sys_msleep(1);

    LOCK_TCPIP_CORE();
    ip4_addr_t ip, mask, gw;
    ip4_addr_set_zero(&ip); ip4_addr_set_zero(&mask); ip4_addr_set_zero(&gw);
    netif_add(&g_netif, &ip, &mask, &gw, NULL, vnet_netif_init, tcpip_input);
    netif_set_default(&g_netif);
    netif_set_link_up(&g_netif);
    UNLOCK_TCPIP_CORE();
    g_inited = 1;
    return 0;
}

void vnet_configure(uint32_t ip_h, uint32_t mask_h, int up) {
    if (!g_inited) return;
    LOCK_TCPIP_CORE();
    ip4_addr_t ip, mask, gw;
    ip4_addr_set_u32(&ip, lwip_htonl(ip_h));
    ip4_addr_set_u32(&mask, lwip_htonl(mask_h));
    ip4_addr_set_u32(&gw, lwip_htonl((ip_h & mask_h) | 0x2501u));   /* 10.13.37.1 */
    netif_set_addr(&g_netif, &ip, &mask, &gw);
    if (up) netif_set_up(&g_netif); else netif_set_down(&g_netif);
    UNLOCK_TCPIP_CORE();
    g_ip = ip_h; g_mask = mask_h; g_up = up;
}

uint32_t vnet_get_ip(void)   { return g_ip; }
uint32_t vnet_get_mask(void) { return g_mask; }

void vnet_input(const uint8_t *pkt, size_t len) {
    if (!g_inited || !g_up || len == 0 || len > 0xFFFF) return;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (!p) return;
    pbuf_take(p, pkt, (u16_t)len);
    if (g_netif.input(p, &g_netif) != ERR_OK) pbuf_free(p);
}

/* ------------------------------------------------------------- sockets */

static void mk_addr(struct sockaddr_in *a, uint32_t ip, uint16_t port) {
    memset(a, 0, sizeof(*a));
    a->sin_len = sizeof(*a);
    a->sin_family = AF_INET;
    a->sin_port = lwip_htons(port);
    a->sin_addr.s_addr = lwip_htonl(ip);
}

int vnet_socket(int type) {
    return lwip_socket(AF_INET, type == VNET_SOCK_STREAM ? SOCK_STREAM : SOCK_DGRAM, 0);
}
int vnet_close(int fd) { return lwip_close(fd); }

int vnet_bind(int fd, uint32_t ip, uint16_t port) {
    struct sockaddr_in a; mk_addr(&a, ip, port);
    return lwip_bind(fd, (struct sockaddr *)&a, sizeof a);
}
int vnet_listen(int fd, int backlog) { return lwip_listen(fd, backlog); }

int vnet_accept(int fd, uint32_t *ip, uint16_t *port) {
    struct sockaddr_in a; socklen_t l = sizeof a;
    int r = lwip_accept(fd, (struct sockaddr *)&a, &l);
    if (r >= 0) {
        if (ip) *ip = lwip_ntohl(a.sin_addr.s_addr);
        if (port) *port = lwip_ntohs(a.sin_port);
    }
    return r;
}
int vnet_connect(int fd, uint32_t ip, uint16_t port) {
    struct sockaddr_in a; mk_addr(&a, ip, port);
    return lwip_connect(fd, (struct sockaddr *)&a, sizeof a);
}
long vnet_send(int fd, const void *buf, size_t len, int dontwait) {
    return lwip_send(fd, buf, len, dontwait ? MSG_DONTWAIT : 0);
}
long vnet_recv(int fd, void *buf, size_t len, int dontwait) {
    return lwip_recv(fd, buf, len, dontwait ? MSG_DONTWAIT : 0);
}
long vnet_sendto(int fd, const void *buf, size_t len, uint32_t ip, uint16_t port) {
    struct sockaddr_in a; mk_addr(&a, ip, port);
    return lwip_sendto(fd, buf, len, 0, (struct sockaddr *)&a, sizeof a);
}
long vnet_recvfrom(int fd, void *buf, size_t len, uint32_t *ip, uint16_t *port, int dontwait) {
    struct sockaddr_in a; socklen_t l = sizeof a;
    memset(&a, 0, sizeof a);
    long r = lwip_recvfrom(fd, buf, len, dontwait ? MSG_DONTWAIT : 0, (struct sockaddr *)&a, &l);
    if (r >= 0) {
        if (ip) *ip = lwip_ntohl(a.sin_addr.s_addr);
        if (port) *port = lwip_ntohs(a.sin_port);
    }
    return r;
}

int vnet_setopt(int fd, int opt, int value) {
    switch (opt) {
        case VNET_OPT_BROADCAST: {
            /* Horizon accepts SO_BROADCAST on any socket; lwIP only on UDP (ENOPROTOOPT
             * otherwise), which made CreateNetwork fail on the TCP listen socket. */
            int type = 0; socklen_t tl = sizeof type;
            if (lwip_getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tl) == 0 && type != SOCK_DGRAM) return 0;
            return lwip_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &value, sizeof value);
        }
        case VNET_OPT_REUSEADDR: return lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof value);
        case VNET_OPT_NODELAY:   return lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof value);
        default: return -1;
    }
}

int vnet_poll(struct vnet_pollfd *fds, unsigned n, int timeout_ms) {
    struct pollfd pf[16];
    if (n > 16) n = 16;
    for (unsigned i = 0; i < n; i++) {
        pf[i].fd = fds[i].fd;
        pf[i].events = (short)(((fds[i].events & VNET_POLLIN) ? POLLIN : 0) | ((fds[i].events & VNET_POLLOUT) ? POLLOUT : 0));
        pf[i].revents = 0;
    }
    int r = lwip_poll(pf, n, timeout_ms);
    for (unsigned i = 0; i < n; i++) {
        short rv = pf[i].revents, o = 0;
        if (rv & (POLLIN | POLLRDNORM)) o |= VNET_POLLIN;
        if (rv & (POLLOUT | POLLWRNORM)) o |= VNET_POLLOUT;
        if (rv & POLLERR) o |= VNET_POLLERR;
        if (rv & POLLHUP) o |= VNET_POLLHUP;
        if (rv & POLLNVAL) o |= VNET_POLLNVAL;
        fds[i].revents = o;
    }
    return r;
}
