/* SPDX-License-Identifier: GPL-2.0-only */
#include "lanplay/lp_probe.h"
#include "lanplay/lp_dns.h"
#include "lanplay/lp_config.h"
#include "lanplay/lp_platform.h"

#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __SWITCH__
#define SET_SIN_LEN(a) ((a).sin_len = sizeof(a))
#else
#define SET_SIN_LEN(a) ((void)0)
#endif

#define DNS_TIME_MS   1600
#define DNS_RESEND_MS 700
#define PING_TIME_MS  1300
#define PING_RESEND_MS 600

static uint32_t g_test_dns_ip;
static uint16_t g_test_dns_port;

void lp_probe_set_dns(uint32_t ip, uint16_t port) { g_test_dns_ip = ip; g_test_dns_port = port; }

static void mk(struct sockaddr_in *a, uint32_t ip, uint16_t port) {
    memset(a, 0, sizeof(*a));
    SET_SIN_LEN(*a);
    a->sin_family = AF_INET;
    a->sin_port = htons(port);
    a->sin_addr.s_addr = htonl(ip);
}

static int wait_readable(int fd, int ms) {
    struct pollfd pf = { .fd = fd, .events = POLLIN, .revents = 0 };
    return poll(&pf, 1, ms) > 0 && (pf.revents & POLLIN);
}

static void resolve_all(const lp_probe_target_t *t, int n, uint32_t *ip, int *ok) {
    uint32_t dns[3]; uint16_t dport = 53; int nd = 0;
    if (g_test_dns_ip) { dns[nd++] = g_test_dns_ip; dport = g_test_dns_port; }
    else {
        uint32_t sys = lp_platform_dns_server();
        if (sys) dns[nd++] = sys;
        if (sys != 0x01010101u) dns[nd++] = 0x01010101u;   /* 1.1.1.1 */
        if (sys != 0x08080808u) dns[nd++] = 0x08080808u;   /* 8.8.8.8 */
    }
    int pending = 0;
    for (int i = 0; i < n; i++) {
        if (lp_str_to_ip(t[i].host, &ip[i]) == 0) ok[i] = 1; else pending++;
    }
    if (!pending) return;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    uint8_t salt; lp_random(&salt, 1);
    uint8_t q[300], r[512];
    const uint64_t start = lp_now_ms();
    int round = 0;
    while (pending > 0) {
        uint64_t el = lp_now_ms() - start;
        if (el >= DNS_TIME_MS) break;
        if ((round == 0) || (round == 1 && el >= DNS_RESEND_MS)) {
            for (int i = 0; i < n; i++) {
                if (ok[i]) continue;
                int first = 1;   /* query each distinct host once */
                for (int j = 0; j < i; j++) if (!ok[j] && !strcmp(t[j].host, t[i].host)) { first = 0; break; }
                if (!first) continue;
                size_t ql = lp_dns_build(t[i].host, (uint16_t)(salt << 8 | i), q, sizeof q);
                if (ql == 0) continue;
                for (int d = 0; d < nd; d++) {
                    struct sockaddr_in a; mk(&a, dns[d], dport);
                    sendto(fd, q, ql, 0, (struct sockaddr *)&a, sizeof a);
                }
            }
            round++;
        }
        if (!wait_readable(fd, 50)) continue;
        for (;;) {
            ssize_t m = recv(fd, r, sizeof r, MSG_DONTWAIT);
            if (m <= 0) break;
            uint16_t id; uint32_t addr;
            if (lp_dns_parse(r, (size_t)m, &id, &addr) != 0 || (id >> 8) != salt) continue;
            int idx = id & 0xFF;
            if (idx >= n) continue;
            for (int j = 0; j < n; j++) {
                if (!ok[j] && !strcmp(t[j].host, t[idx].host)) { ok[j] = 1; ip[j] = addr; pending--; }
            }
        }
    }
    close(fd);
}

void lp_probe(const lp_probe_target_t *t, int n, lp_probe_result_t *res) {
    if (n > LP_PROBE_MAX) n = LP_PROBE_MAX;
    uint32_t ip[LP_PROBE_MAX];
    int ok[LP_PROBE_MAX];
    uint64_t sent[LP_PROBE_MAX][2];
    memset(ok, 0, sizeof ok);
    memset(sent, 0, sizeof sent);
    for (int i = 0; i < n; i++) { res[i].status = LP_PROBE_DNS_FAILED; res[i].ping_ms = -1; }

    resolve_all(t, n, ip, ok);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct sockaddr_in any; mk(&any, 0, 0);
    bind(fd, (struct sockaddr *)&any, sizeof any);

    int waiting = 0;
    for (int i = 0; i < n; i++) if (ok[i]) { res[i].status = LP_PROBE_NO_RESPONSE; waiting++; }

    const uint64_t start = lp_now_ms();
    int round = 0;
    while (waiting > 0) {
        uint64_t el = lp_now_ms() - start;
        if (el >= PING_TIME_MS) break;
        if (round == 0 || (round == 1 && el >= PING_RESEND_MS)) {
            for (int i = 0; i < n; i++) {
                if (!ok[i] || res[i].status == LP_PROBE_OK) continue;
                uint8_t pkt[4] = { 0x02, 0xA5, (uint8_t)i, (uint8_t)round };
                struct sockaddr_in a; mk(&a, ip[i], t[i].port);
                sent[i][round] = lp_now_ms();
                sendto(fd, pkt, sizeof pkt, 0, (struct sockaddr *)&a, sizeof a);
            }
            round++;
        }
        if (!wait_readable(fd, 30)) continue;
        for (;;) {
            uint8_t b[64];
            struct sockaddr_in from; socklen_t fl = sizeof from;
            ssize_t m = recvfrom(fd, b, sizeof b, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
            if (m <= 0) break;
            if (m < 4 || b[0] != 0x02 || b[1] != 0xA5) continue;
            int i = b[2], rd = b[3] & 1;
            if (i >= n || !ok[i] || res[i].status == LP_PROBE_OK) continue;
            if (ntohl(from.sin_addr.s_addr) != ip[i] || ntohs(from.sin_port) != t[i].port) continue;
            res[i].status = LP_PROBE_OK;
            res[i].ping_ms = (int32_t)(lp_now_ms() - (sent[i][rd] ? sent[i][rd] : start));
            waiting--;
        }
    }
    close(fd);
}
