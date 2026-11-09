/* OS socket compatibility for the lanplay core: POSIX / libnx, and Winsock for the PC test tool.
 * SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sys/types.h>
#define poll(fds, n, ms)            WSAPoll((fds), (n), (ms))
#define close(fd)                   closesocket(fd)
#define MSG_DONTWAIT                0       /* sockets that need it are made non-blocking instead */
#define LP_SOCK_SET_NONBLOCK(fd)    do { u_long one_ = 1; ioctlsocket((fd), FIONBIO, &one_); } while (0)
#define LP_SOCK_UDP_NONBLOCK(fd)    LP_SOCK_SET_NONBLOCK(fd)
#define LP_SOCK_IN_PROGRESS()       (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define LP_SOCK_SET_NONBLOCK(fd)    do { int fl_ = fcntl((fd), F_GETFL, 0); fcntl((fd), F_SETFL, fl_ | O_NONBLOCK); } while (0)
#define LP_SOCK_UDP_NONBLOCK(fd)    do { } while (0)
#define LP_SOCK_IN_PROGRESS()       (errno == EINPROGRESS)
#endif
