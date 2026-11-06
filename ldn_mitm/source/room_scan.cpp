/* SPDX-License-Identifier: GPL-2.0-only */
#include "room_scan.hpp"
#include "net_backend.hpp"
#include "lanplay_service.hpp"
#include "lan_protocol.hpp"
#include "ldn_types.hpp"
#include "debug.hpp"
#include <arpa/inet.h>
#include <cstring>

namespace ams::mitm::ldn::rooms {

    namespace {

        constexpr u32  LanMagic       = 0x11451400;
        constexpr u16  LdnPort        = 11452;     /* LANDiscovery::DefaultPort */
        constexpr s64  ScanTimeMs     = 1500;
        constexpr s64  ResendAfterMs  = 500;

        constexpr Result ResultModeOff      = MAKERESULT(0xFD, 93);
        constexpr Result ResultNotConnected = MAKERESULT(0xFD, 92);
        constexpr Result ResultSocket       = MAKERESULT(0xFD, 94);

        static_assert(sizeof(LANPacketHeader) == 12);

        constinit os::SdkMutex g_scan_lock;

        constinit os::SdkMutex g_local_lock;
        constinit bool g_local_valid = false;
        alignas(8) constinit u8 g_local_info[sizeof(NetworkInfo)] = {};

        /* Same zero run-length scheme as LanSocket::decompress. */
        bool Decompress(const u8 *in, size_t in_size, u8 *out, size_t out_cap, size_t *out_size) {
            const u8 *in_end = in + in_size;
            u8 *o = out, *o_end = out + out_cap;
            while (in < in_end && o < o_end) {
                const u8 c = *in++;
                *o++ = c;
                if (c == 0) {
                    if (in == in_end) {
                        return false;
                    }
                    u8 count = *in++;
                    while (count-- > 0 && o < o_end) {
                        *o++ = 0;
                    }
                }
            }
            *out_size = static_cast<size_t>(o - out);
            return in == in_end;
        }

        s64 NowMs() {
            return os::GetSystemTick().ToTimeSpan().GetMilliSeconds();
        }

        void SendScan(int fd, u32 broadcast) {
            LANPacketHeader h;
            std::memset(std::addressof(h), 0, sizeof(h));
            h.magic = LanMagic;
            h.type  = LANPacketType::Scan;
            struct sockaddr_in to;
            std::memset(std::addressof(to), 0, sizeof(to));
            to.sin_len         = sizeof(to);
            to.sin_family      = AF_INET;
            to.sin_port        = htons(LdnPort);
            to.sin_addr.s_addr = htonl(broadcast);
            net::SendTo(fd, std::addressof(h), sizeof(h), std::addressof(to));
        }

        bool AddRoom(const NetworkInfo *info, lp_room_t *out, size_t max_rooms, u32 *count, bool own);

        /* Returns true when a new room was added. */
        bool HandleReply(const u8 *buf, size_t len, lp_room_t *out, size_t max_rooms, u32 *count) {
            if (len < sizeof(LANPacketHeader) || *count >= max_rooms) {
                return false;
            }
            LANPacketHeader h;
            std::memcpy(std::addressof(h), buf, sizeof(h));
            if (h.magic != LanMagic || h.type != LANPacketType::ScanResp || sizeof(h) + h.length > len) {
                return false;
            }

            alignas(8) static u8 info_buf[sizeof(NetworkInfo)];
            const u8 *body = buf + sizeof(h);
            size_t size = h.length;
            if (h.compressed) {
                if (!Decompress(body, h.length, info_buf, sizeof(info_buf), std::addressof(size)) || size != h.decompress_length) {
                    return false;
                }
            } else {
                if (size > sizeof(info_buf)) {
                    return false;
                }
                std::memcpy(info_buf, body, size);
            }
            if (size != sizeof(NetworkInfo)) {
                return false;
            }
            return AddRoom(reinterpret_cast<const NetworkInfo *>(info_buf), out, max_rooms, count, false);
        }

        bool AddRoom(const NetworkInfo *info, lp_room_t *out, size_t max_rooms, u32 *count, bool own) {
            if (*count >= max_rooms) {
                return false;
            }
            /* De-duplicate (we broadcast twice). */
            const u32 host_ip = info->ldn.nodes[0].ipv4Address;
            for (u32 i = 0; i < *count; i++) {
                if (out[i].host_ip == host_ip && out[i].local_comm_id == info->networkId.intentId.localCommunicationId) {
                    return false;
                }
            }

            lp_room_t &r = out[*count];
            std::memset(std::addressof(r), 0, sizeof(r));
            r.local_comm_id = info->networkId.intentId.localCommunicationId;
            r.scene_id      = info->networkId.intentId.sceneId;
            r.host_ip       = host_ip;
            r.comm_version  = info->ldn.nodes[0].localCommunicationVersion;
            r.node_count    = info->ldn.nodeCount;
            r.node_max      = info->ldn.nodeCountMax;
            r.has_password  = 0;   /* the passphrase is not visible in scan replies */
            r.is_own        = own ? 1 : 0;
            int p = 0;
            for (int n = 0; n < NodeCountMax && p < LP_ROOM_PLAYERS; n++) {
                const NodeInfo &node = info->ldn.nodes[n];
                if (!node.isConnected) {
                    continue;
                }
                std::memcpy(r.players[p], node.userName, sizeof(r.players[p]) - 1);
                r.players[p][sizeof(r.players[p]) - 1] = 0;
                p++;
            }
            (*count)++;
            return true;
        }

    }

    Result Scan(lp_room_t *out, size_t max_rooms, u32 *out_count) {
        std::scoped_lock lk(g_scan_lock);
        *out_count = 0;
        const u32 mode = lanplay::GetMode();
        R_UNLESS(mode != LP_MODE_OFF, ResultModeOff);
        const bool virt = mode == LP_MODE_INTERNET;
        R_UNLESS(!virt || lanplay::UseVirtualNetwork(), ResultNotConnected);

        u32 ip = 0, mask = 0;
        R_TRY(net::GetIpConfig(virt, std::addressof(ip), std::addressof(mask)));
        const u32 broadcast = ip | ~mask;

        const int fd = net::Socket(virt, SOCK_DGRAM);
        R_UNLESS(fd >= 0, ResultSocket);
        ON_SCOPE_EXIT { net::Close(fd); };

        net::SetBoolOpt(fd, SOL_SOCKET, SO_BROADCAST, 1);
        struct sockaddr_in any;
        std::memset(std::addressof(any), 0, sizeof(any));
        any.sin_len    = sizeof(any);
        any.sin_family = AF_INET;
        R_UNLESS(net::Bind(fd, std::addressof(any)) == 0, ResultSocket);

        {
            std::scoped_lock lk2(g_local_lock);
            if (g_local_valid) {
                AddRoom(reinterpret_cast<const NetworkInfo *>(g_local_info), out, max_rooms, out_count, true);
            }
        }

        const s64 start = NowMs();
        bool resent = false;
        SendScan(fd, broadcast);

        static u8 buf[2048];
        for (;;) {
            const s64 now = NowMs();
            if (now - start >= ScanTimeMs) {
                break;
            }
            if (!resent && now - start >= ResendAfterMs) {
                SendScan(fd, broadcast);
                resent = true;
            }
            struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
            const s64 wait = std::min<s64>(100, ScanTimeMs - (now - start));
            if (net::Poll(std::addressof(pfd), 1, static_cast<int>(wait)) <= 0 || !(pfd.revents & POLLIN)) {
                continue;
            }
            struct sockaddr_in from;
            const ssize_t n = net::RecvFrom(fd, buf, sizeof(buf), std::addressof(from));
            if (n > 0) {
                HandleReply(buf, static_cast<size_t>(n), out, max_rooms, out_count);
            }
        }
        LogFormat("rooms: scan found %u (virt=%d)", *out_count, virt ? 1 : 0);
        R_SUCCEED();
    }

    void PublishLocal(const void *network_info) {
        std::scoped_lock lk(g_local_lock);
        g_local_valid = network_info != nullptr;
        if (g_local_valid) {
            std::memcpy(g_local_info, network_info, sizeof(g_local_info));
        }
    }

}
