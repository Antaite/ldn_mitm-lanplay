/*
 * Virtual Switch test tool — the parts that talk on the virtual (lwIP) network:
 * ICMP ping through the lan-play server, the ldn_mitm room scan, and a fake room.
 * Only lwIP headers here (no OS sockets), so it builds the same on every OS.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "vs_net.h"
#include "lanplay/vnet.h"
#include "lanplay/lp_platform.h"
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/icmp.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ ldn wire format */

#define LAN_MAGIC 0x11451400u
#define LDN_PORT  11452
enum { PKT_SCAN = 0, PKT_SCAN_RESP = 1 };

#pragma pack(push, 1)
typedef struct { uint32_t magic; uint8_t type, compressed; uint16_t length, decompress_length; uint8_t reserved[2]; } lan_hdr_t;
#pragma pack(pop)

typedef struct { uint8_t raw[6]; } mac_t;
typedef struct { uint8_t length; char raw[33]; } ssid_t;
typedef struct { mac_t bssid; ssid_t ssid; int16_t channel; int8_t link_level; uint8_t network_type; uint32_t unk; } common_info_t;
typedef struct {
    uint32_t ipv4; mac_t mac; int8_t node_id; int8_t connected; char user[33]; uint8_t unk1;
    int16_t comm_version; uint8_t unk2[16];
} node_info_t;
typedef struct {
    uint8_t random[16]; uint16_t security_mode; uint8_t accept_policy; uint8_t unk1[3];
    uint8_t node_max, node_count; node_info_t nodes[8]; uint16_t unk2; uint16_t adv_size;
    uint8_t adv[384]; uint8_t unk3[148];
} ldn_info_t;
typedef struct { uint64_t local_comm_id; uint8_t unk1[2]; uint16_t scene_id; uint8_t unk2[4]; } intent_t;
typedef struct { intent_t intent; uint64_t session_hi, session_lo; common_info_t common; ldn_info_t ldn; } network_info_t;

_Static_assert(sizeof(node_info_t) == 0x40, "NodeInfo");
_Static_assert(sizeof(network_info_t) == 0x480, "NetworkInfo");

static int decompress(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *outn) {
    const uint8_t *end = in + n; uint8_t *o = out, *oe = out + cap;
    while (in < end && o < oe) {
        uint8_t c = *in++;
        *o++ = c;
        if (c == 0) {
            if (in == end) return 0;
            uint8_t k = *in++;
            while (k-- > 0 && o < oe) *o++ = 0;
        }
    }
    *outn = (size_t)(o - out);
    return in == end;
}

static uint32_t broadcast_addr(void) { return (vnet_get_ip() & vnet_get_mask()) | ~vnet_get_mask(); }

static void send_scan(int fd) {
    lan_hdr_t h; memset(&h, 0, sizeof h);
    h.magic = LAN_MAGIC; h.type = PKT_SCAN;
    vnet_sendto(fd, &h, sizeof h, broadcast_addr(), LDN_PORT);
}

int vs_scan_rooms(vs_room_t *out, int max, int ms) {
    int fd = vnet_socket(VNET_SOCK_DGRAM);
    if (fd < 0) return -1;
    vnet_setopt(fd, VNET_OPT_BROADCAST, 1);
    if (vnet_bind(fd, 0, 0) != 0) { vnet_close(fd); return -1; }

    int n = 0, resent = 0;
    uint64_t t0 = lp_now_ms();
    send_scan(fd);
    static uint8_t buf[2048];
    static network_info_t info;
    while (lp_now_ms() - t0 < (uint64_t)ms) {
        if (!resent && lp_now_ms() - t0 > 500) { send_scan(fd); resent = 1; }
        struct vnet_pollfd pf = { fd, VNET_POLLIN, 0 };
        if (vnet_poll(&pf, 1, 100) <= 0) continue;
        uint32_t ip; uint16_t port;
        long r = vnet_recvfrom(fd, buf, sizeof buf, &ip, &port, 1);
        if (r < (long)sizeof(lan_hdr_t)) continue;
        lan_hdr_t h; memcpy(&h, buf, sizeof h);
        if (h.magic != LAN_MAGIC || h.type != PKT_SCAN_RESP || sizeof h + h.length > (size_t)r) continue;
        size_t size = h.length;
        if (h.compressed) {
            if (!decompress(buf + sizeof h, h.length, (uint8_t *)&info, sizeof info, &size)) continue;
        } else {
            if (size > sizeof info) continue;
            memcpy(&info, buf + sizeof h, size);
        }
        if (size != sizeof info || n >= max) continue;
        uint32_t host = info.ldn.nodes[0].ipv4;
        int dup = 0;
        for (int i = 0; i < n; i++) if (out[i].host_ip == host && out[i].game_id == info.intent.local_comm_id) dup = 1;
        if (dup) continue;
        vs_room_t *rm = &out[n++];
        memset(rm, 0, sizeof *rm);
        rm->game_id = info.intent.local_comm_id;
        rm->host_ip = host;
        rm->comm_version = info.ldn.nodes[0].comm_version;
        rm->players = info.ldn.node_count;
        rm->players_max = info.ldn.node_max;
        memcpy(rm->host_name, info.ldn.nodes[0].user, 32);
        rm->host_name[32] = 0;
    }
    vnet_close(fd);
    return n;
}

/* ------------------------------------------------------------------ fake room */

static volatile int g_host_run;
static vs_scan_cb g_scan_cb;

static void host_thread(void *arg) {
    (void)arg;
    int fd = vnet_socket(VNET_SOCK_DGRAM);
    if (fd < 0) return;
    vnet_setopt(fd, VNET_OPT_REUSEADDR, 1);
    vnet_setopt(fd, VNET_OPT_BROADCAST, 1);
    if (vnet_bind(fd, 0, LDN_PORT) != 0) { vnet_close(fd); return; }

    static network_info_t info;
    memset(&info, 0, sizeof info);
    info.intent.local_comm_id = VS_TEST_GAME_ID;
    lp_random(&info.session_hi, 16);
    info.common.ssid.length = 7; memcpy(info.common.ssid.raw, "vswitch", 7);
    info.common.channel = 6; info.common.link_level = 3; info.common.network_type = 2;
    info.ldn.node_max = 8; info.ldn.node_count = 1;
    node_info_t *me = &info.ldn.nodes[0];
    me->ipv4 = vnet_get_ip();
    lp_random(me->mac.raw, 6); me->mac.raw[0] = 0x02;
    me->node_id = 0; me->connected = 1; me->comm_version = 1;
    snprintf(me->user, sizeof me->user, "%s", "虛擬Switch測試");

    static uint8_t pkt[sizeof(lan_hdr_t) + sizeof(network_info_t)];
    lan_hdr_t h; memset(&h, 0, sizeof h);
    h.magic = LAN_MAGIC; h.type = PKT_SCAN_RESP; h.length = sizeof info; h.decompress_length = sizeof info;
    memcpy(pkt, &h, sizeof h);
    memcpy(pkt + sizeof h, &info, sizeof info);

    while (g_host_run) {
        struct vnet_pollfd pf = { fd, VNET_POLLIN, 0 };
        if (vnet_poll(&pf, 1, 200) <= 0) continue;
        uint8_t b[64]; uint32_t ip; uint16_t port;
        long r = vnet_recvfrom(fd, b, sizeof b, &ip, &port, 1);
        if (r < (long)sizeof(lan_hdr_t)) continue;
        lan_hdr_t q; memcpy(&q, b, sizeof q);
        if (q.magic != LAN_MAGIC || q.type != PKT_SCAN || ip == vnet_get_ip()) continue;
        me->ipv4 = vnet_get_ip();
        memcpy(pkt + sizeof h, &info, sizeof info);
        vnet_sendto(fd, pkt, sizeof pkt, ip, port);
        if (g_scan_cb) g_scan_cb(ip);
    }
    vnet_close(fd);
}

int vs_host_start(vs_scan_cb cb) {
    g_scan_cb = cb;
    g_host_run = 1;
    return lp_thread_start(LP_THREAD_BRIDGE, host_thread, NULL);   /* the bridge slot is free in this tool */
}

void vs_host_stop(void) {
    g_host_run = 0;
    lp_thread_join(LP_THREAD_BRIDGE);
}

/* ------------------------------------------------------------------ ping */

#define PING_ID 0x4C50   /* "LP" */

int vs_ping_run(uint32_t ip, int size, int count, int interval_ms, int timeout_ms, vs_ping_result_t *res) {
    memset(res, 0, sizeof *res);
    res->size = size;
    res->min_ms = -1;
    if (size < 8) size = 8;
    if (size > VS_PING_MAX) size = VS_PING_MAX;
    if (count > VS_PING_COUNT_MAX) count = VS_PING_COUNT_MAX;

    int s = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (s < 0) return -1;
    struct timeval tv = { 0, 20 * 1000 };
    lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    struct sockaddr_in to; memset(&to, 0, sizeof to);
    to.sin_len = sizeof to; to.sin_family = AF_INET; to.sin_addr.s_addr = lwip_htonl(ip);

    static uint8_t pkt[VS_PING_MAX], rb[VS_PING_MAX + 64];
    uint64_t sent_at[VS_PING_COUNT_MAX];
    uint8_t got[VS_PING_COUNT_MAX];
    memset(got, 0, sizeof got);
    static uint16_t s_seq_base = 1;
    uint16_t base = s_seq_base; s_seq_base = (uint16_t)(s_seq_base + count + 1);

    long total_ms = 0;
    int next = 0;
    uint64_t next_at = lp_now_ms(), deadline = 0;
    for (;;) {
        uint64_t now = lp_now_ms();
        if (next < count && now >= next_at) {
            struct icmp_echo_hdr *e = (struct icmp_echo_hdr *)pkt;
            memset(pkt, 0, (size_t)size);
            ICMPH_TYPE_SET(e, ICMP_ECHO); ICMPH_CODE_SET(e, 0);
            e->id = lwip_htons(PING_ID); e->seqno = lwip_htons((uint16_t)(base + next));
            for (int i = (int)sizeof *e; i < size; i++) pkt[i] = (uint8_t)i;
            e->chksum = inet_chksum(pkt, (u16_t)size);
            sent_at[next] = now;
            if (lwip_sendto(s, pkt, (size_t)size, 0, (struct sockaddr *)&to, sizeof to) >= 0) res->sent++;
            else res->send_fail++;
            next++;
            next_at = now + (uint64_t)interval_ms;
            if (next == count) deadline = now + (uint64_t)timeout_ms;
        }
        if (next == count && now >= deadline) break;

        struct sockaddr_in from; socklen_t fl = sizeof from;
        int r = lwip_recvfrom(s, rb, sizeof rb, 0, (struct sockaddr *)&from, &fl);
        if (r <= 0) continue;
        int ihl = (rb[0] & 0x0F) * 4;
        if (r < ihl + (int)sizeof(struct icmp_echo_hdr)) continue;
        struct icmp_echo_hdr *e = (struct icmp_echo_hdr *)(rb + ihl);
        if (ICMPH_TYPE(e) != ICMP_ER || lwip_ntohs(e->id) != PING_ID) continue;
        if (lwip_ntohl(from.sin_addr.s_addr) != ip) continue;
        int k = (uint16_t)(lwip_ntohs(e->seqno) - base);
        if (k < 0 || k >= next || got[k]) continue;
        got[k] = 1;
        int rtt = (int)(lp_now_ms() - sent_at[k]);
        res->received++;
        total_ms += rtt;
        if (res->min_ms < 0 || rtt < res->min_ms) res->min_ms = rtt;
        if (rtt > res->max_ms) res->max_ms = rtt;
        if (next == count && res->received == res->sent) break;
    }
    lwip_close(s);
    res->avg_ms = res->received ? (int)(total_ms / res->received) : -1;
    return 0;
}
