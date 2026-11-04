/*
 * vnet - a user-space TCP/IP stack (lwIP) bound to the virtual lan-play
 * network.  This header deliberately uses no system socket types so it can
 * be included next to the real BSD socket headers.
 *
 * All addresses/ports are in HOST byte order.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VNET_FD_OFFSET  0x300       /* every vnet fd is >= this (== LWIP_SOCKET_OFFSET) */

#define VNET_POLLIN     0x01
#define VNET_POLLOUT    0x02
#define VNET_POLLERR    0x04
#define VNET_POLLHUP    0x08
#define VNET_POLLNVAL   0x10

enum { VNET_SOCK_STREAM = 1, VNET_SOCK_DGRAM = 2 };
enum { VNET_OPT_BROADCAST = 1, VNET_OPT_REUSEADDR = 2, VNET_OPT_NODELAY = 3 };

struct vnet_pollfd { int fd; short events; short revents; };

typedef int (*vnet_output_fn)(const uint8_t *pkt, size_t len);

int  vnet_init(vnet_output_fn out);            /* idempotent, 0 on success */
void vnet_configure(uint32_t ip, uint32_t mask, int up);
uint32_t vnet_get_ip(void);
uint32_t vnet_get_mask(void);
void vnet_input(const uint8_t *pkt, size_t len);

static inline int vnet_is_fd(int fd) { return fd >= VNET_FD_OFFSET; }

int     vnet_socket(int type);
int     vnet_close(int fd);
int     vnet_bind(int fd, uint32_t ip, uint16_t port);
int     vnet_listen(int fd, int backlog);
int     vnet_accept(int fd, uint32_t *ip, uint16_t *port);
int     vnet_connect(int fd, uint32_t ip, uint16_t port);
long    vnet_send(int fd, const void *buf, size_t len, int dontwait);
long    vnet_recv(int fd, void *buf, size_t len, int dontwait);
long    vnet_sendto(int fd, const void *buf, size_t len, uint32_t ip, uint16_t port);
long    vnet_recvfrom(int fd, void *buf, size_t len, uint32_t *ip, uint16_t *port, int dontwait);
int     vnet_setopt(int fd, int opt, int value);
int     vnet_poll(struct vnet_pollfd *fds, unsigned n, int timeout_ms);

#ifdef __cplusplus
}
#endif
