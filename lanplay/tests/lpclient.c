/*
 * Host test: emulates what ldn_mitm does on the virtual network.
 *   lpclient host <server> <virt-ip>   : answers UDP "scan" broadcasts on 11452 and echoes TCP 11452
 *   lpclient join <server> <virt-ip>   : broadcasts a scan, connects to the responder, sends 200 KB, checks echo
 */
#include "lanplay/lp_tunnel.h"
#include "lanplay/lp_config.h"
#include "lanplay/lp_platform.h"
#include "lanplay/vnet.h"
#include "lanplay/lp_bridge.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "lanplay/lp_probe.h"
#include "lanplay/lp_dns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int out_cb(const uint8_t *p, size_t n) { return lp_tunnel_send_ipv4(p, n); }

/* Same policy as the sysmodule: in a session (SESSION=1) wait 5 s, otherwise move at once. */
static int conflict_cb(uint32_t elapsed, uint32_t *ip) {
    if (getenv("SESSION") && elapsed < 5000) return 0;
    uint8_t r[2]; lp_random(r, 2);
    *ip = lp_generate_virtual_ip(r);
    vnet_configure(*ip, LP_SUBNET_MASK, 1);
    return 1;
}

static void wait_connected(void) {
    for (int i = 0; i < 100; i++) {
        lp_status_t s; lp_tunnel_get_status(&s);
        if (s.state == LP_STATE_CONNECTED) { fprintf(stderr, "connected, ping %d ms\n", s.ping_ms); return; }
        lp_sleep_ms(100);
    }
    fprintf(stderr, "never connected\n"); exit(2);
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "probe")) {
        /* lpclient probe dnsport host:port ... */
        lp_probe_set_dns(0x7F000001, (uint16_t)atoi(argv[2]));
        lp_probe_target_t t[LP_PROBE_MAX]; lp_probe_result_t r[LP_PROBE_MAX];
        int n = 0;
        for (int i = 3; i < argc && n < LP_PROBE_MAX; i++, n++) {
            memset(&t[n], 0, sizeof t[n]);
            char *c = strrchr(argv[i], ':'); *c = 0;
            snprintf(t[n].host, sizeof t[n].host, "%s", argv[i]); t[n].port = (uint16_t)atoi(c + 1);
        }
        uint64_t t0 = lp_now_ms();
        lp_probe(t, n, r);
        for (int i = 0; i < n; i++) printf("%-24s %u status=%d ping=%d\n", t[i].host, t[i].port, r[i].status, r[i].ping_ms);
        printf("took %llu ms\n", (unsigned long long)(lp_now_ms() - t0));
        return 0;
    }
    if (argc < 4) { fprintf(stderr, "usage: %s host|join server ip [user pass]\n", argv[0]); return 1; }
    srand((unsigned)lp_now_ms());
    uint32_t ip; lp_str_to_ip(argv[3], &ip);
    lp_tunnel_params_t p; memset(&p, 0, sizeof p);
    snprintf(p.host, sizeof p.host, "%s", argv[2]); p.port = getenv("PORT") ? (uint16_t)atoi(getenv("PORT")) : 11451; p.virt_ip = ip;
    if (getenv("PMTU")) p.pmtu = (uint32_t)atoi(getenv("PMTU"));
    if (argc >= 6) { snprintf(p.user, sizeof p.user, "%s", argv[4]); snprintf(p.pass, sizeof p.pass, "%s", argv[5]); }

    vnet_init(out_cb);
    vnet_configure(ip, LP_SUBNET_MASK, 1);
    lp_tunnel_set_input(vnet_input);
    lp_tunnel_set_conflict_handler(conflict_cb);
    lp_tunnel_start(&p);
    if (!strcmp(argv[1], "bgame")) {
        /* Plays the game: a REAL host UDP socket, bridged; answers PING with PONG. */
        if (lp_bridge_start() != 0) { printf("bridge start failed\n"); return 1; }
        int g = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(49152);
        if (bind(g, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); return 1; }
        if (lp_bridge_open(1, g, 49152, 0x7F000001, 0) != 0) { printf("open failed\n"); return 1; }
        int got = 0;
        for (int i = 0; i < 100 && got < 3; i++) {
            static uint8_t b[9000]; struct sockaddr_in f; socklen_t fl = sizeof f;
            struct timeval tv = { 0, 100000 }; setsockopt(g, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            long n = recvfrom(g, b, sizeof b, 0, (struct sockaddr *)&f, &fl);
            if (n <= 0) continue;
            uint32_t ip; uint16_t port;
            int ours = ntohs(f.sin_port) == lp_bridge_inject_port() && lp_bridge_parse(b, (size_t)n, &ip, &port);
            if (!ours) { printf("game: foreign datagram\n"); continue; }
            char s[16]; lp_ip_to_str(ip, s);
            printf("game: %ld bytes from %s:%u '%.*s'\n", n - LP_BRIDGE_HDR, s, port, (int)(n - LP_BRIDGE_HDR), b + LP_BRIDGE_HDR);
            lp_bridge_sendto(1, g, "PONG", 4, ip, port);
            got++;
        }
        lp_bridge_close(1, g);
        lp_sleep_ms(100);
        printf("game: done, %d exchanged\n", got);
        lp_tunnel_stop();
        return got == 3 ? 0 : 1;
    }
    if (!strcmp(argv[1], "bpeer")) {
        /* A remote console: vnet UDP to the game's virtual IP:49152, expects PONG. */
        uint32_t host; lp_str_to_ip(argv[4], &host);
        int u = vnet_socket(VNET_SOCK_DGRAM); vnet_bind(u, 0, 50000);
        int ok = 0;
        for (int i = 0; i < 3; i++) {
            static char m[3000]; memset(m, 'x', sizeof m); snprintf(m, 16, "PING%d", i);
            size_t ml = getenv("BIG") ? sizeof m : strlen(m);
            vnet_sendto(u, m, ml, host, 49152);
            for (int k = 0; k < 20; k++) {
                char b[64]; uint32_t ip; uint16_t port;
                long n = vnet_recvfrom(u, b, sizeof b, &ip, &port, 1);
                if (n == 4 && !memcmp(b, "PONG", 4) && ip == host && port == 49152) { ok++; break; }
                lp_sleep_ms(50);
            }
        }
        printf("peer: %d/3 PONG from %s:49152\n", ok, argv[4]);
        lp_tunnel_stop();
        return ok == 3 ? 0 : 1;
    }
    if (!strcmp(argv[1], "apsock")) {
        /* Same calls as LANDiscovery::initTcp(true) + setSocketOpts on the virtual network. */
        int fd = vnet_socket(VNET_SOCK_STREAM);
        int b = vnet_setopt(fd, VNET_OPT_BROADCAST, 1);
        int r = vnet_setopt(fd, VNET_OPT_REUSEADDR, 1);
        int bi = vnet_bind(fd, 0, 11452), li = vnet_listen(fd, 10);
        int u = vnet_socket(VNET_SOCK_DGRAM), ub = vnet_setopt(u, VNET_OPT_BROADCAST, 1);
        printf("tcp: broadcast=%d reuse=%d bind=%d listen=%d | udp: broadcast=%d\n", b, r, bi, li, ub);
        lp_tunnel_stop();
        return 0;
    }
    if (!strcmp(argv[1], "idle")) {
        /* lpclient idle server ip seconds : stay connected, then print the final virtual IP */
        lp_sleep_ms((uint64_t)atoi(argc > 4 ? argv[4] : "10") * 1000);
        lp_status_t s; lp_tunnel_get_status(&s);
        char b[16]; lp_ip_to_str(s.virt_ip, b);
        printf("final ip %s (vnet %s) state=%d ping=%d\n", b, vnet_get_ip() == s.virt_ip ? "same" : "DIFFERENT", (int)s.state, (int)s.ping_ms);
        lp_tunnel_stop();
        return 0;
    }
    if (!strcmp(argv[1], "status")) {
        lp_sleep_ms(4000);
        lp_status_t s; lp_tunnel_get_status(&s);
        printf("state=%u ping=%d online=%d msg='%s'\n", s.state, s.ping_ms, s.online, s.server_msg);
        lp_tunnel_stop();
        return 0;
    }
    wait_connected();

    uint32_t bcast = (ip & LP_SUBNET_MASK) | ~LP_SUBNET_MASK;
    int u = vnet_socket(VNET_SOCK_DGRAM);
    vnet_setopt(u, VNET_OPT_BROADCAST, 1);
    vnet_setopt(u, VNET_OPT_REUSEADDR, 1);
    if (vnet_bind(u, 0, 11452) != 0) { perror("bind"); return 3; }

    if (!strcmp(argv[1], "host")) {
        int l = vnet_socket(VNET_SOCK_STREAM);
        vnet_setopt(l, VNET_OPT_REUSEADDR, 1);
        vnet_bind(l, 0, 11452); vnet_listen(l, 4);
        int c = -1; size_t echoed = 0;
        for (;;) {
            struct vnet_pollfd pf[2] = { { u, VNET_POLLIN, 0 }, { c >= 0 ? c : l, VNET_POLLIN, 0 } };
            vnet_poll(pf, 2, 100);
            if (pf[0].revents & VNET_POLLIN) {
                char b[256]; uint32_t fip; uint16_t fport;
                long n = vnet_recvfrom(u, b, sizeof b, &fip, &fport, 1);
                char s[16]; lp_ip_to_str(fip, s);
                fprintf(stderr, "host: udp %ld bytes from %s:%u '%.*s'\n", n, s, fport, (int)(n > 0 ? n : 0), b);
                if (n > 0 && !strncmp(b, "SCAN", 4)) vnet_sendto(u, "SCANRESP", 8, fip, fport);
            }
            if (pf[1].revents) {
                if (c < 0) { uint32_t fip; c = vnet_accept(l, &fip, NULL); fprintf(stderr, "host: accepted %d\n", c); }
                else {
                    char b[4096]; long n = vnet_recv(c, b, sizeof b, 1);
                    if (n <= 0) { fprintf(stderr, "host: peer closed after %zu bytes\n", echoed); vnet_close(c); c = -1; if (getenv("ONESHOT")) break; continue; }
                    for (long off = 0; off < n;) { long w = vnet_send(c, b + off, (size_t)(n - off), 0); if (w <= 0) break; off += w; }
                    echoed += (size_t)n;
                }
            }
        }
    } else {
        uint32_t hip = 0;
        for (int tries = 0; tries < 20 && !hip; tries++) {
            vnet_sendto(u, "SCAN", 4, bcast, 11452);
            struct vnet_pollfd pf = { u, VNET_POLLIN, 0 };
            if (vnet_poll(&pf, 1, 500) > 0) {
                char b[64]; uint32_t fip; uint16_t fp; long n = vnet_recvfrom(u, b, sizeof b, &fip, &fp, 1);
                if (n == 8 && !memcmp(b, "SCANRESP", 8)) hip = fip;
            }
        }
        if (!hip) { fprintf(stderr, "join: no scan response\n"); return 4; }
        char s[16]; lp_ip_to_str(hip, s); fprintf(stderr, "join: found host %s\n", s);
        int t = vnet_socket(VNET_SOCK_STREAM);
        vnet_setopt(t, VNET_OPT_NODELAY, 1);
        if (vnet_connect(t, hip, 11452) != 0) { fprintf(stderr, "join: connect failed\n"); return 5; }
        const size_t total = 200 * 1024;
        uint8_t *tx = malloc(total), *rx = malloc(total);
        for (size_t i = 0; i < total; i++) tx[i] = (uint8_t)(i * 7 + (i >> 9));
        uint64_t t0 = lp_now_ms();
        size_t sent = 0, got = 0;
        while (got < total && lp_now_ms() - t0 < 20000) {
            if (sent < total) { long w = vnet_send(t, tx + sent, total - sent > 4096 ? 4096 : total - sent, 1); if (w > 0) sent += (size_t)w; }
            struct vnet_pollfd pf = { t, VNET_POLLIN, 0 };
            if (vnet_poll(&pf, 1, 5) > 0) { long n = vnet_recv(t, rx + got, total - got, 1); if (n > 0) got += (size_t)n; else if (n == 0) break; }
        }
        int ok = got == total && !memcmp(tx, rx, total);
        lp_status_t st; lp_tunnel_get_status(&st);
        fprintf(stderr, "join: echo %s (%zu/%zu bytes in %llu ms) up=%llu down=%llu ping=%d online=%d\n", ok ? "OK" : "FAILED", got, total,
                (unsigned long long)(lp_now_ms() - t0), (unsigned long long)st.up_bytes, (unsigned long long)st.down_bytes, st.ping_ms, st.online);
        vnet_close(t);
        lp_sleep_ms(500);
        lp_tunnel_stop();
        return ok ? 0 : 6;
    }
    return 0;
}
