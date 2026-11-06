/*
 * lan-play client (re-implemented from the protocol used by
 * spacemeowx2/switch-lan-play's relay server).
 *
 * Wire format, UDP to the server (default port 11451):
 *   byte 0      : type (bit 7 = encrypted, never used)
 *   0x00 KEEPALIVE  (no payload, every 10 s)
 *   0x01 IPV4       raw IPv4 packet
 *   0x02 PING       client sends 1+3 bytes, server echoes the first 4 bytes
 *   0x03 IPV4_FRAG  16 byte header (src[4] dst[4] id u16 part u8 total u8 len u16 pmtu u16) + data
 *   0x04 AUTH_ME    server: auth_type(0) + 64 byte challenge
 *                   client: sha1(sha1(password) + challenge) + username
 *   0x10 INFO       server message (utf-8 text)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "lanplay/lp_tunnel.h"
#include "lanplay/lp_platform.h"
#include "lanplay/lp_sha1.h"
#include "lanplay/lp_config.h"
#include "lanplay/lp_dns.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

enum {
    T_KEEPALIVE = 0x00, T_IPV4 = 0x01, T_PING = 0x02, T_IPV4_FRAG = 0x03, T_AUTH_ME = 0x04, T_INFO = 0x10,
};

#ifdef __SWITCH__
#define SET_SIN_LEN(a) ((a).sin_len = sizeof(a))
#else
#define SET_SIN_LEN(a) ((void)0)
#endif

#define FRAG_HDR_LEN      16
#define FRAG_SLOTS        4
#define FRAG_MAX_LEN      2048
#define RECV_BUF_LEN      4096
#define KEEPALIVE_MS      10000
#define PING_MS           3000
#define PONG_TIMEOUT_MS   10000
#define RESOLVE_RETRY_MS  5000
#define RERESOLVE_MS      30000
#define INFO_PERIOD_MS    20000
#define PING_SLOTS        8
#define SENT_SIGS         32        /* recent own packets, to tell echoes from a real conflict */
#define PROBE_PERIOD_MS   300000    /* re-announce our IP every 5 min */
#define PROBE_MIN_GAP_MS  2000
#define CONFLICT_QUIET_MS 3000      /* no more foreign packets for this long = resolved */
#define PROBE_PORT        11449
#define NEWCOMER_MS       15000     /* connected for less than this: we are the one who moves */
#define SETTLED_GRACE_MS  5000      /* settled clients give the newcomer this long to move */

typedef struct {
    int      used;
    uint16_t id;
    uint8_t  src[4];
    uint32_t parts;     /* bitmask of received parts */
    uint16_t total_len; /* 0 until last part seen */
    uint64_t stamp;
    uint8_t  buf[FRAG_MAX_LEN];
} frag_slot_t;

static struct {
    volatile int       running;
    int                fd;
    lp_tunnel_params_t p;
    uint8_t            key[20];
    struct sockaddr_in server;
    int                have_server;
    uint16_t           frag_id;
    uint16_t           ping_seq;
    struct { uint16_t seq; uint64_t sent; } pings[PING_SLOTS];
    uint64_t           last_pong;
    uint64_t           first_send;
    uint64_t           last_auth_error;
    uint64_t           last_auth_req;
    frag_slot_t        frags[FRAG_SLOTS];
    lp_status_t        st;
    lp_ipv4_input_fn   input;
    lp_ip_conflict_fn  on_conflict;
    uint8_t            ip_be[4];       /* our virtual IP, network order */
    uint32_t           sent_sig[SENT_SIGS];
    uint32_t           sent_idx;
    uint64_t           conflict_first, conflict_last, last_probe;
    int                was_connected;
    uint64_t           connected_at;
    uint8_t            rx[RECV_BUF_LEN];
} T = { .fd = -1 };

void lp_tunnel_set_input(lp_ipv4_input_fn fn) { T.input = fn; }
void lp_tunnel_set_conflict_handler(lp_ip_conflict_fn fn) { T.on_conflict = fn; }

static void set_ip(uint32_t ip) {
    T.p.virt_ip = ip;
    T.ip_be[0] = (uint8_t)(ip >> 24); T.ip_be[1] = (uint8_t)(ip >> 16);
    T.ip_be[2] = (uint8_t)(ip >> 8);  T.ip_be[3] = (uint8_t)ip;
}

/* IP id + header checksum: identifies one of our own packets if the server echoes it. */
static uint32_t pkt_sig(const uint8_t *pkt) {
    return (uint32_t)pkt[4] << 24 | (uint32_t)pkt[5] << 16 | (uint32_t)pkt[10] << 8 | pkt[11];
}
int  lp_tunnel_running(void) { return T.running; }

static void set_state(uint32_t s) {
    lp_lock();
    T.st.state = s;
    lp_unlock();
}

static void set_msg(const char *m, size_t n) {
    lp_lock();
    if (n >= sizeof(T.st.server_msg)) n = sizeof(T.st.server_msg) - 1;
    memcpy(T.st.server_msg, m, n);
    T.st.server_msg[n] = 0;
    lp_unlock();
}

void lp_tunnel_get_status(lp_status_t *out) {
    lp_lock();
    *out = T.st;
    lp_unlock();
}

/* ---------------------------------------------------------------- sending */

static int send_raw2(const uint8_t *a, size_t al, const uint8_t *b, size_t bl, const uint8_t *c, size_t cl) {
    uint8_t buf[1 + FRAG_HDR_LEN + 1600];
    size_t n = al + bl + cl;
    if (n > sizeof(buf) || T.fd < 0 || !T.have_server) return -1;
    memcpy(buf, a, al);
    if (bl) memcpy(buf + al, b, bl);
    if (cl) memcpy(buf + al + bl, c, cl);
    ssize_t r = sendto(T.fd, buf, n, 0, (const struct sockaddr *)&T.server, sizeof(T.server));
    if (r < 0) return -1;
    __atomic_add_fetch(&T.st.up_bytes, (uint64_t)n, __ATOMIC_RELAXED);
    __atomic_add_fetch(&T.st.up_packets, 1, __ATOMIC_RELAXED);
    return 0;
}

static int send_type(uint8_t type, const void *payload, size_t len) {
    return send_raw2(&type, 1, payload, len, NULL, 0);
}

int lp_tunnel_send_ipv4(const uint8_t *pkt, size_t len) {
    if (!T.running || len < 20 || len > 1600) return -1;
    if (!memcmp(pkt + 12, T.ip_be, 4)) {
        uint32_t i = __atomic_fetch_add(&T.sent_idx, 1, __ATOMIC_RELAXED);
        T.sent_sig[i % SENT_SIGS] = pkt_sig(pkt);
    }
    uint32_t pmtu = T.p.pmtu;
    if (pmtu == 0 || len <= pmtu) {
        uint8_t t = T_IPV4;
        return send_raw2(&t, 1, pkt, len, NULL, 0);
    }
    uint32_t total = (uint32_t)((len + pmtu - 1) / pmtu);
    if (total > 32) return -1;
    uint16_t id = __atomic_fetch_add(&T.frag_id, 1, __ATOMIC_RELAXED);
    uint8_t t = T_IPV4_FRAG, h[FRAG_HDR_LEN];
    memcpy(h + 0, pkt + 12, 4);      /* src */
    memcpy(h + 4, pkt + 16, 4);      /* dst */
    h[8] = id >> 8; h[9] = id & 0xFF;
    h[11] = (uint8_t)total;
    h[14] = pmtu >> 8; h[15] = pmtu & 0xFF;
    for (uint32_t i = 0, pos = 0; pos < len; i++, pos += pmtu) {
        uint32_t plen = len - pos < pmtu ? (uint32_t)(len - pos) : pmtu;
        h[10] = (uint8_t)i;
        h[12] = plen >> 8; h[13] = plen & 0xFF;
        if (send_raw2(&t, 1, h, sizeof h, pkt + pos, plen) != 0) return -1;
    }
    return 0;
}

/* -------------------------------------------------------------- receiving */

/* Small UDP broadcast from our IP. Any other client using the same IP sees a packet
 * "from itself" and knows there is a conflict; it answers with its own probe. */
static void send_probe(uint64_t now) {
    static const char tag[] = "LPIPCHK";
    uint8_t b[20 + 8 + sizeof tag - 1];
    uint8_t rnd[2]; lp_random(rnd, 2);
    memset(b, 0, sizeof b);
    b[0] = 0x45; b[2] = 0; b[3] = sizeof b;
    b[4] = rnd[0]; b[5] = rnd[1];
    b[8] = 64; b[9] = 17;
    memcpy(b + 12, T.ip_be, 4);
    b[16] = T.ip_be[0]; b[17] = T.ip_be[1]; b[18] = 0xFF; b[19] = 0xFF;
    uint32_t sum = 0;
    for (int i = 0; i < 20; i += 2) sum += (uint32_t)(b[i] << 8 | b[i + 1]);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    sum = ~sum & 0xFFFF;
    b[10] = (uint8_t)(sum >> 8); b[11] = (uint8_t)sum;
    b[20] = PROBE_PORT >> 8; b[21] = PROBE_PORT & 0xFF; b[22] = PROBE_PORT >> 8; b[23] = PROBE_PORT & 0xFF;
    b[25] = 8 + sizeof tag - 1;                                     /* UDP checksum 0 = none */
    memcpy(b + 28, tag, sizeof tag - 1);
    T.last_probe = now;
    lp_tunnel_send_ipv4(b, sizeof b);
}

static void deliver(const uint8_t *pkt, size_t len) {
    if (len < 20 || (pkt[0] >> 4) != 4) return;
    if (!memcmp(pkt + 12, T.ip_be, 4)) {
        /* "From us": either the server echoed our own packet, or someone else has our IP. */
        const uint32_t sig = pkt_sig(pkt);
        for (int i = 0; i < SENT_SIGS; i++) if (T.sent_sig[i] == sig) return;
        const uint64_t now = lp_now_ms();
        if (!T.conflict_first) {
            T.conflict_first = now;
            lp_log("lanplay: another client uses our virtual IP");
        }
        T.conflict_last = now;
        if (now - T.last_probe >= PROBE_MIN_GAP_MS) send_probe(now);   /* let the other side notice too */
        return;
    }
    if (T.input) T.input(pkt, len);
}

static void check_conflict(uint64_t now) {
    if (!T.conflict_first) return;
    if (now - T.conflict_last > CONFLICT_QUIET_MS) { T.conflict_first = 0; return; }   /* the other side moved */
    if (now - T.connected_at >= NEWCOMER_MS && now - T.conflict_first < SETTLED_GRACE_MS) return;
    uint32_t ip = 0;
    if (!T.on_conflict || !T.on_conflict((uint32_t)(now - T.conflict_first), &ip) || !lp_ip_is_valid_virtual(ip)) return;
    char s[16]; lp_ip_to_str(ip, s);
    lp_log("lanplay: virtual IP changed to %s", s);
    set_ip(ip);
    lp_lock();
    T.st.virt_ip = ip;
    lp_unlock();
    T.conflict_first = 0;
    send_probe(now);
}

static void on_frag(const uint8_t *p, size_t len) {
    if (len < FRAG_HDR_LEN) return;
    uint16_t id = (uint16_t)(p[8] << 8 | p[9]);
    uint8_t part = p[10], total = p[11];
    uint16_t plen = (uint16_t)(p[12] << 8 | p[13]), pmtu = (uint16_t)(p[14] << 8 | p[15]);
    if (total == 0 || total > 32 || part >= total || plen > len - FRAG_HDR_LEN) return;
    if ((size_t)pmtu * part + plen > FRAG_MAX_LEN) return;

    uint64_t now = lp_now_ms();
    frag_slot_t *f = NULL, *oldest = &T.frags[0];
    for (int i = 0; i < FRAG_SLOTS; i++) {
        frag_slot_t *s = &T.frags[i];
        if (s->used && s->id == id && !memcmp(s->src, p, 4)) { f = s; break; }
        if (s->used && now - s->stamp > 2000) s->used = 0;        /* expire stale */
        if (!s->used) oldest = s; else if (oldest->used && s->stamp < oldest->stamp) oldest = s;
    }
    if (!f) {
        f = oldest;
        memset(f, 0, offsetof(frag_slot_t, buf));
        f->used = 1; f->id = id; memcpy(f->src, p, 4);
    }
    f->stamp = now;
    memcpy(f->buf + (size_t)pmtu * part, p + FRAG_HDR_LEN, plen);
    f->parts |= 1u << part;
    if (part == total - 1) f->total_len = (uint16_t)((total - 1) * pmtu + plen);
    uint32_t want = total == 32 ? 0xFFFFFFFFu : ((1u << total) - 1);
    if (f->parts == want && f->total_len) {
        f->used = 0;
        deliver(f->buf, f->total_len);
    }
}

static void on_auth(const uint8_t *p, size_t len) {
    T.last_auth_req = lp_now_ms();
    if (len < 1 || p[0] != 0) return;
    if (T.p.user[0] == 0) {
        set_state(LP_STATE_AUTH_REQUIRED);
        return;
    }
    uint8_t resp[20 + sizeof(T.p.user)];
    size_t ul = strlen(T.p.user);
    lp_sha1_ctx c;
    lp_sha1_init(&c);
    lp_sha1_update(&c, T.key, 20);
    lp_sha1_update(&c, p + 1, len - 1);
    lp_sha1_final(&c, resp);
    memcpy(resp + 20, T.p.user, ul);
    send_type(T_AUTH_ME, resp, 20 + ul);
}

static int contains(const uint8_t *h, size_t hl, const char *n) {
    size_t nl = strlen(n);
    for (size_t i = 0; i + nl <= hl; i++) if (!memcmp(h + i, n, nl)) return 1;
    return 0;
}

static void on_packet(const uint8_t *b, size_t n) {
    if (n == 0) return;
    __atomic_add_fetch(&T.st.down_bytes, (uint64_t)n, __ATOMIC_RELAXED);
    __atomic_add_fetch(&T.st.down_packets, 1, __ATOMIC_RELAXED);
    uint8_t type = b[0] & 0x7F;
    const uint8_t *p = b + 1;
    size_t len = n - 1;
    switch (type) {
        case T_IPV4:      deliver(p, len); break;
        case T_IPV4_FRAG: on_frag(p, len); break;
        case T_AUTH_ME:   on_auth(p, len); break;
        case T_PING: {
            if (len < 2) break;
            uint16_t seq = (uint16_t)(p[0] << 8 | p[1]);
            uint64_t now = lp_now_ms();
            for (int i = 0; i < PING_SLOTS; i++) {
                if (T.pings[i].sent && T.pings[i].seq == seq) {
                    lp_lock();
                    T.st.ping_ms = (int32_t)(now - T.pings[i].sent);
                    lp_unlock();
                    T.pings[i].sent = 0;
                    break;
                }
            }
            T.last_pong = now;
            break;
        }
        case T_INFO: {
            set_msg((const char *)p, len);
            if (len >= 5 && (contains(p, len, "Error when login") || contains(p, len, "Wrong password"))) {
                T.last_auth_error = lp_now_ms();
            }
            break;
        }
        default: break;
    }
}

/* -------------------------------------------------------------- resolving */

static int dns_query(uint32_t dns_ip, const char *host, uint32_t *out) {
    uint8_t q[300], r[512];
    uint8_t rnd[2]; lp_random(rnd, 2);
    const uint16_t qid = (uint16_t)(rnd[0] << 8 | rnd[1]);
    size_t qlen = lp_dns_build(host, qid, q, sizeof q);
    if (qlen == 0) return -1;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(dns_ip) };
    SET_SIN_LEN(a);
    int ok = -1;
    for (int attempt = 0; attempt < 2 && ok != 0 && T.running; attempt++) {
        if (sendto(fd, q, qlen, 0, (struct sockaddr *)&a, sizeof a) < 0) break;
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        if (poll(&pf, 1, 1500) <= 0) continue;
        ssize_t n = recv(fd, r, sizeof r, 0);
        uint16_t rid; uint32_t ip;
        if (n > 0 && lp_dns_parse(r, (size_t)n, &rid, &ip) == 0 && rid == qid) { *out = ip; ok = 0; }
    }
    close(fd);
    return ok;
}

static int resolve(const char *host, uint32_t *out) {
    if (lp_str_to_ip(host, out) == 0) return 0;
    /* Plain DNS first: fast, bounded, and independent of the firmware 23 resolver changes. */
    uint32_t dns[3] = { lp_platform_dns_server(), 0x01010101u /* 1.1.1.1 */, 0x08080808u };
    for (int i = 0; i < 3; i++) {
        if (dns[i] && dns_query(dns[i], host, out) == 0) return 0;
    }
    /* Fallback: system resolver. */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) == 0 && res) {
        for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
            if (ai->ai_family == AF_INET) {
                *out = ntohl(((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr);
                freeaddrinfo(res);
                return 0;
            }
        }
        freeaddrinfo(res);
    }
    return -1;
}

/* ------------------------------------------------------------ info thread */

static int parse_online(const char *resp) {
    const char *k = strstr(resp, "\"online\"");
    if (!k) return -1;
    k = strchr(k, ':');
    if (!k) return -1;
    return atoi(k + 1);
}

static void info_thread(void *arg) {
    (void)arg;
    uint64_t next = 0;
    while (T.running) {
        if (lp_now_ms() < next || !T.have_server) { lp_sleep_ms(200); continue; }
        next = lp_now_ms() + INFO_PERIOD_MS;
        struct sockaddr_in a = T.server;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int online = -1;
        int rc = connect(fd, (struct sockaddr *)&a, sizeof a);
        if (rc == 0 || errno == EINPROGRESS) {
            struct pollfd pf = { .fd = fd, .events = POLLOUT };
            if (rc == 0 || poll(&pf, 1, 2500) == 1) {
                int err = 0; socklen_t el = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
                if (err == 0) {
                    char req[256], resp[1024];
                    char ip[16]; lp_ip_to_str(ntohl(a.sin_addr.s_addr), ip);
                    int rl = snprintf(req, sizeof req, "GET /info HTTP/1.0\r\nHost: %s:%u\r\nUser-Agent: ldn_mitm-lanplay\r\n\r\n", ip, (unsigned)ntohs(a.sin_port));
                    if (send(fd, req, (size_t)rl, 0) == rl) {
                        size_t got = 0;
                        uint64_t deadline = lp_now_ms() + 2500;
                        while (got < sizeof resp - 1 && lp_now_ms() < deadline && T.running) {
                            pf.events = POLLIN; pf.revents = 0;
                            if (poll(&pf, 1, 250) <= 0) continue;
                            ssize_t n = recv(fd, resp + got, sizeof resp - 1 - got, 0);
                            if (n <= 0) break;
                            got += (size_t)n;
                        }
                        resp[got] = 0;
                        online = parse_online(resp);
                    }
                }
            }
        }
        close(fd);
        lp_lock();
        T.st.online = online;
        lp_unlock();
    }
}

/* ---------------------------------------------------------- tunnel thread */

static void tunnel_thread(void *arg) {
    (void)arg;
    uint64_t next_keepalive = 0, next_ping = 0, next_resolve = 0, resolved_at = 0;
    while (T.running) {
        uint64_t now = lp_now_ms();

        /* (Re)resolve the server address. */
        int stale = T.have_server && T.last_pong + RERESOLVE_MS < now && resolved_at + RERESOLVE_MS < now;
        if ((!T.have_server || stale) && now >= next_resolve) {
            if (!T.have_server) set_state(LP_STATE_RESOLVING);
            uint32_t ip;
            if (resolve(T.p.host, &ip) == 0) {
                lp_lock();
                memset(&T.server, 0, sizeof(T.server));
                SET_SIN_LEN(T.server);
                T.server.sin_family = AF_INET;
                T.server.sin_port = htons(T.p.port);
                T.server.sin_addr.s_addr = htonl(ip);
                T.st.server_ip = ip;
                T.st.server_port = T.p.port;
                lp_unlock();
                if (!T.have_server) { T.first_send = now; set_state(LP_STATE_CONNECTING); }
                T.have_server = 1;
                resolved_at = now;
                next_keepalive = next_ping = now;
            } else if (!T.have_server) {
                set_state(LP_STATE_RESOLVE_FAILED);
            }
            next_resolve = now + RESOLVE_RETRY_MS;
        }

        if (T.have_server) {
            if (now >= next_keepalive) {
                if (send_type(T_KEEPALIVE, NULL, 0) != 0 && T.st.state != LP_STATE_CONNECTED) set_state(LP_STATE_NO_NETWORK);
                next_keepalive = now + KEEPALIVE_MS;
            }
            if (now >= next_ping) {
                uint16_t seq = ++T.ping_seq;
                uint8_t pl[3] = { (uint8_t)(seq >> 8), (uint8_t)seq, 0 };
                T.pings[seq % PING_SLOTS].seq = seq;
                T.pings[seq % PING_SLOTS].sent = now;
                send_type(T_PING, pl, sizeof pl);
                next_ping = now + PING_MS;
            }

            uint32_t s;
            if (T.last_auth_error && now - T.last_auth_error < 15000) s = LP_STATE_AUTH_FAILED;
            else if (T.p.user[0] == 0 && T.last_auth_req && now - T.last_auth_req < 15000) s = LP_STATE_AUTH_REQUIRED;
            else if (T.last_pong && now - T.last_pong < PONG_TIMEOUT_MS) s = LP_STATE_CONNECTED;
            else if (now - T.first_send < PONG_TIMEOUT_MS) s = LP_STATE_CONNECTING;
            else s = LP_STATE_NO_RESPONSE;
            if (s != LP_STATE_CONNECTED) {
                lp_lock();
                if (!T.last_pong || now - T.last_pong >= PONG_TIMEOUT_MS) T.st.ping_ms = -1;
                lp_unlock();
            }
            if (T.st.state != LP_STATE_NO_NETWORK || s == LP_STATE_CONNECTED) set_state(s);

            if (s == LP_STATE_CONNECTED) {
                if (!T.was_connected) T.connected_at = now;
                if (!T.was_connected || now - T.last_probe >= PROBE_PERIOD_MS) send_probe(now);
                T.was_connected = 1;
                check_conflict(now);
            } else {
                T.was_connected = 0;
            }
        }

        struct pollfd pf = { .fd = T.fd, .events = POLLIN };
        int pr = poll(&pf, 1, 100);
        if (pr > 0 && (pf.revents & POLLIN)) {
            for (int burst = 0; burst < 64; burst++) {
                struct sockaddr_in from; socklen_t fl = sizeof from;
                ssize_t n = recvfrom(T.fd, T.rx, sizeof T.rx, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
                if (n <= 0) break;
                if (from.sin_addr.s_addr != T.server.sin_addr.s_addr || from.sin_port != T.server.sin_port) continue;
                on_packet(T.rx, (size_t)n);
            }
        }
    }
}

int lp_tunnel_start(const lp_tunnel_params_t *params) {
    if (T.running) lp_tunnel_stop();
    lp_ipv4_input_fn input = T.input;
    lp_ip_conflict_fn on_conflict = T.on_conflict;
    memset(&T, 0, sizeof T);
    T.input = input;
    T.on_conflict = on_conflict;
    T.fd = -1;
    T.p = *params;
    set_ip(T.p.virt_ip);
    if (T.p.port == 0) T.p.port = LP_DEFAULT_PORT;
    if (T.p.pmtu && (T.p.pmtu < 500 || T.p.pmtu > 1500)) T.p.pmtu = 0;
    lp_sha1(T.p.pass, strlen(T.p.pass), T.key);
    T.st.mode = LP_MODE_INTERNET;
    T.st.virt_ip = T.p.virt_ip;
    T.st.ping_ms = -1;
    T.st.online = -1;
    T.st.state = LP_STATE_RESOLVING;

    T.fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (T.fd < 0) { T.st.state = LP_STATE_NO_NETWORK; return -1; }
    struct sockaddr_in any = { .sin_family = AF_INET };
    SET_SIN_LEN(any);
    bind(T.fd, (struct sockaddr *)&any, sizeof any);

    T.running = 1;
    if (lp_thread_start(LP_THREAD_TUNNEL, tunnel_thread, NULL) != 0) {
        T.running = 0; close(T.fd); T.fd = -1; T.st.state = LP_STATE_OFF;
        return -1;
    }
    if (lp_thread_start(LP_THREAD_INFO, info_thread, NULL) != 0) {
        lp_log("lanplay: info thread failed to start");
    }
    lp_log("lanplay: tunnel started (%s:%u)", T.p.host, (unsigned)T.p.port);
    return 0;
}

void lp_tunnel_stop(void) {
    if (!T.running) return;
    T.running = 0;
    lp_thread_join(LP_THREAD_TUNNEL);
    lp_thread_join(LP_THREAD_INFO);
    if (T.fd >= 0) { close(T.fd); T.fd = -1; }
    T.have_server = 0;
    lp_lock();
    T.st.state = LP_STATE_OFF;
    T.st.ping_ms = -1;
    T.st.online = -1;
    lp_unlock();
    lp_log("lanplay: tunnel stopped");
}
