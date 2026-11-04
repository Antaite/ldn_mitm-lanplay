/* lwIP options for the lan-play virtual network.
 * SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_CORE_LOCKING_INPUT   0

#define MEM_ALIGNMENT                   8
#define MEM_SIZE                        (128 * 1024)
#define MEMP_NUM_PBUF                   64
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_TCP_PCB                16
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_TCP_SEG                128
#define MEMP_NUM_NETBUF                 32
#define MEMP_NUM_NETCONN                24
#define MEMP_NUM_TCPIP_MSG_INPKT        64
#define MEMP_NUM_SYS_TIMEOUT            (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 4)
#define PBUF_POOL_SIZE                  64
#define PBUF_POOL_BUFSIZE               1536

#define LWIP_ARP                        0
#define LWIP_ETHERNET                   0
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ICMP                       1
#define LWIP_RAW                        0
#define LWIP_DHCP                       0
#define LWIP_AUTOIP                     0
#define LWIP_IGMP                       0
#define LWIP_DNS                        0
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define IP_FORWARD                      0
#define IP_REASSEMBLY                   1
#define IP_FRAG                         1
#define IP_SOF_BROADCAST                0
#define IP_SOF_BROADCAST_RECV           0

#define TCP_MSS                         1360
#define TCP_WND                         (8 * TCP_MSS)
#define TCP_SND_BUF                     (8 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define TCP_QUEUE_OOSEQ                 1
#define LWIP_TCP_KEEPALIVE              1
/* game traffic is latency sensitive: faster timers / retransmission */
#define TCP_TMR_INTERVAL                100
#define LWIP_TCP_RTO_TIME               1000

#define LWIP_NETCONN                    1
#define LWIP_SOCKET                     1
#define LWIP_COMPAT_SOCKETS             0
#define LWIP_POSIX_SOCKETS_IO_NAMES     0
#define LWIP_SOCKET_OFFSET              0x300   /* must equal VNET_FD_OFFSET; needs FD_SETSIZE >= 0x300+MEMP_NUM_NETCONN */
#define LWIP_SOCKET_POLL                1
#define LWIP_SOCKET_SELECT              0
#define LWIP_SO_RCVTIMEO                1
#define LWIP_SO_SNDTIMEO                1
#define LWIP_SO_RCVBUF                  1
#define SO_REUSE                        1
#define LWIP_TCP_TIMESTAMPS             0
#define LWIP_NETIF_API                  0
#define LWIP_NETIF_LOOPBACK             1
#define LWIP_HAVE_LOOPIF                0
#define LWIP_NETIF_STATUS_CALLBACK      0
#define LWIP_STATS                      0
#define LWIP_ERRNO_STDINCLUDE           1
#define LWIP_TIMEVAL_PRIVATE            0
#define LWIP_SOCKET_EXTERNAL_HEADERS    0

#define TCPIP_THREAD_NAME               "lanplay_tcpip"
#define TCPIP_THREAD_STACKSIZE          0x4000
#define TCPIP_THREAD_PRIO               0x15
#define TCPIP_MBOX_SIZE                 128
#define DEFAULT_RAW_RECVMBOX_SIZE       32
#define DEFAULT_UDP_RECVMBOX_SIZE       32
#define DEFAULT_TCP_RECVMBOX_SIZE       64
#define DEFAULT_ACCEPTMBOX_SIZE         8
#define DEFAULT_THREAD_STACKSIZE        0x4000

#define LWIP_NETCONN_SEM_PER_THREAD     0
#define LWIP_CHECKSUM_ON_COPY           1
