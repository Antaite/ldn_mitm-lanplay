/* SPDX-License-Identifier: GPL-2.0-only */
#include "net_backend.hpp"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <stratosphere.hpp>
#include "lanplay/vnet.h"

namespace ams::mitm::ldn::net {

    namespace {

        constexpr Result ResultNoVirtualIp = MAKERESULT(0xFD, 90);

        inline u32  Ip(const struct sockaddr_in *a)   { return ntohl(a->sin_addr.s_addr); }
        inline u16  Port(const struct sockaddr_in *a) { return ntohs(a->sin_port); }

        inline void Fill(struct sockaddr_in *a, u32 ip, u16 port) {
            if (a == nullptr) {
                return;
            }
            std::memset(a, 0, sizeof(*a));
            a->sin_len         = sizeof(*a);
            a->sin_family      = AF_INET;
            a->sin_port        = htons(port);
            a->sin_addr.s_addr = htonl(ip);
        }

    }

    int Socket(bool virt, int type) {
        if (virt) {
            return vnet_socket(type == SOCK_STREAM ? VNET_SOCK_STREAM : VNET_SOCK_DGRAM);
        }
        return ::socket(AF_INET, type, 0);
    }

    int Close(int fd) {
        return vnet_is_fd(fd) ? vnet_close(fd) : ::close(fd);
    }

    int Bind(int fd, const struct sockaddr_in *addr) {
        if (vnet_is_fd(fd)) {
            return vnet_bind(fd, Ip(addr), Port(addr));
        }
        return ::bind(fd, reinterpret_cast<const struct sockaddr *>(addr), sizeof(*addr));
    }

    int Listen(int fd, int backlog) {
        return vnet_is_fd(fd) ? vnet_listen(fd, backlog) : ::listen(fd, backlog);
    }

    int Accept(int fd, struct sockaddr_in *addr) {
        if (vnet_is_fd(fd)) {
            u32 ip = 0; u16 port = 0;
            int r = vnet_accept(fd, &ip, &port);
            if (r >= 0) {
                Fill(addr, ip, port);
            }
            return r;
        }
        socklen_t len = sizeof(*addr);
        return ::accept(fd, reinterpret_cast<struct sockaddr *>(addr), &len);
    }

    int Connect(int fd, const struct sockaddr_in *addr) {
        if (vnet_is_fd(fd)) {
            return vnet_connect(fd, Ip(addr), Port(addr));
        }
        return ::connect(fd, reinterpret_cast<const struct sockaddr *>(addr), sizeof(*addr));
    }

    ssize_t Send(int fd, const void *buf, size_t len) {
        if (vnet_is_fd(fd)) {
            return vnet_send(fd, buf, len, 0);
        }
        return ::send(fd, buf, len, 0);
    }

    ssize_t Recv(int fd, void *buf, size_t len) {
        if (vnet_is_fd(fd)) {
            return vnet_recv(fd, buf, len, 0);
        }
        return ::recv(fd, buf, len, 0);
    }

    ssize_t SendTo(int fd, const void *buf, size_t len, const struct sockaddr_in *addr) {
        if (vnet_is_fd(fd)) {
            return vnet_sendto(fd, buf, len, Ip(addr), Port(addr));
        }
        return ::sendto(fd, buf, len, 0, reinterpret_cast<const struct sockaddr *>(addr), sizeof(*addr));
    }

    ssize_t RecvFrom(int fd, void *buf, size_t len, struct sockaddr_in *addr) {
        if (vnet_is_fd(fd)) {
            u32 ip = 0; u16 port = 0;
            long r = vnet_recvfrom(fd, buf, len, &ip, &port, 0);
            if (r >= 0) {
                Fill(addr, ip, port);
            }
            return r;
        }
        socklen_t alen = sizeof(*addr);
        return ::recvfrom(fd, buf, len, 0, reinterpret_cast<struct sockaddr *>(addr), &alen);
    }

    int SetBoolOpt(int fd, int level, int name, int value) {
        if (vnet_is_fd(fd)) {
            if (level == SOL_SOCKET && name == SO_BROADCAST) return vnet_setopt(fd, VNET_OPT_BROADCAST, value);
            if (level == SOL_SOCKET && name == SO_REUSEADDR) return vnet_setopt(fd, VNET_OPT_REUSEADDR, value);
            if (level == IPPROTO_TCP && name == TCP_NODELAY) return vnet_setopt(fd, VNET_OPT_NODELAY, value);
            return -1;
        }
        return ::setsockopt(fd, level, name, &value, sizeof(value));
    }

    int Poll(struct pollfd *fds, nfds_t nfds, int timeout) {
        bool any_virtual = false;
        for (nfds_t i = 0; i < nfds; i++) {
            if (fds[i].fd >= 0 && vnet_is_fd(fds[i].fd)) {
                any_virtual = true;
                break;
            }
        }
        if (!any_virtual) {
            return ::poll(fds, nfds, timeout);
        }

        /* Virtual mode: every socket of a LANDiscovery lives on the same backend. */
        struct vnet_pollfd vfds[16];
        nfds_t map[16];
        unsigned n = 0;
        for (nfds_t i = 0; i < nfds && n < 16; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0 || !vnet_is_fd(fds[i].fd)) {
                continue;
            }
            vfds[n].fd      = fds[i].fd;
            vfds[n].events  = static_cast<short>(((fds[i].events & (POLLIN | POLLPRI)) ? VNET_POLLIN : 0) | ((fds[i].events & POLLOUT) ? VNET_POLLOUT : 0));
            vfds[n].revents = 0;
            map[n++] = i;
        }
        int rc = vnet_poll(vfds, n, timeout);
        if (rc <= 0) {
            return rc;
        }
        for (unsigned k = 0; k < n; k++) {
            short r = 0;
            if (vfds[k].revents & VNET_POLLIN)   r |= POLLIN;
            if (vfds[k].revents & VNET_POLLOUT)  r |= POLLOUT;
            if (vfds[k].revents & VNET_POLLERR)  r |= POLLERR;
            if (vfds[k].revents & VNET_POLLHUP)  r |= POLLHUP;
            if (vfds[k].revents & VNET_POLLNVAL) r |= POLLNVAL;
            fds[map[k]].revents = r;
        }
        return rc;
    }

    Result GetIpConfig(bool virt, u32 *ip, u32 *mask) {
        if (virt) {
            const u32 v = vnet_get_ip();
            if (v == 0) {
                return ResultNoVirtualIp;
            }
            *ip   = v;
            *mask = vnet_get_mask();
            R_SUCCEED();
        }
        u32 address, netmask, gateway, primary_dns, secondary_dns;
        R_TRY(nifmGetCurrentIpConfigInfo(&address, &netmask, &gateway, &primary_dns, &secondary_dns));
        *ip   = ntohl(address);
        *mask = ntohl(netmask);
        R_SUCCEED();
    }

}
