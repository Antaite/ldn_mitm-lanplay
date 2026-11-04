/*
 * Socket backend switch for ldn_mitm:
 *   - System : the console's real network (original ldn_mitm, same LAN)
 *   - Virtual: lwIP stack on the lan-play 10.13.0.0/16 network (internet mode)
 * Virtual fds are >= VNET_FD_OFFSET, so every call after socket creation
 * dispatches on the fd alone.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stratosphere.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

namespace ams::mitm::ldn::net {

    int     Socket(bool virt, int type);
    int     Close(int fd);
    int     Bind(int fd, const struct sockaddr_in *addr);
    int     Listen(int fd, int backlog);
    int     Accept(int fd, struct sockaddr_in *addr);
    int     Connect(int fd, const struct sockaddr_in *addr);
    ssize_t Send(int fd, const void *buf, size_t len);
    ssize_t Recv(int fd, void *buf, size_t len);
    ssize_t SendTo(int fd, const void *buf, size_t len, const struct sockaddr_in *addr);
    ssize_t RecvFrom(int fd, void *buf, size_t len, struct sockaddr_in *addr);
    int     SetBoolOpt(int fd, int level, int name, int value);
    int     Poll(struct pollfd *fds, nfds_t nfds, int timeout);

    /* Host byte order. */
    ams::Result GetIpConfig(bool virt, u32 *ip, u32 *mask);

}
