/* SPDX-License-Identifier: GPL-2.0-only */
#include "lanplay/lp_dns.h"
#include <string.h>

size_t lp_dns_build(const char *host, uint16_t id, uint8_t *q, size_t cap) {
    size_t hl = strlen(host), pos = 12;
    if (hl == 0 || hl > 250 || cap < hl + 18) return 0;
    memset(q, 0, 12);
    q[0] = (uint8_t)(id >> 8); q[1] = (uint8_t)id;
    q[2] = 0x01;              /* recursion desired */
    q[5] = 1;                 /* QDCOUNT */
    const char *s = host;
    while (*s) {
        const char *dot = strchr(s, '.');
        size_t l = dot ? (size_t)(dot - s) : strlen(s);
        if (l == 0 || l > 63) return 0;
        q[pos++] = (uint8_t)l; memcpy(q + pos, s, l); pos += l;
        s += l; if (*s == '.') s++;
    }
    q[pos++] = 0; q[pos++] = 0; q[pos++] = 1; q[pos++] = 0; q[pos++] = 1;   /* QTYPE A, QCLASS IN */
    return pos;
}

static int skip_name(const uint8_t *r, size_t n, size_t *i) {
    while (*i < n) {
        uint8_t l = r[*i];
        if ((l & 0xC0) == 0xC0) { *i += 2; return *i <= n ? 0 : -1; }
        if (l == 0) { *i += 1; return 0; }
        *i += (size_t)l + 1;
    }
    return -1;
}

int lp_dns_parse(const uint8_t *r, size_t n, uint16_t *id, uint32_t *ip) {
    if (n < 12 || !(r[2] & 0x80) || (r[3] & 0x0F) != 0) return -1;   /* not a response / error rcode */
    *id = (uint16_t)(r[0] << 8 | r[1]);
    size_t qd = (size_t)(r[4] << 8 | r[5]), an = (size_t)(r[6] << 8 | r[7]), i = 12;
    for (size_t k = 0; k < qd; k++) {
        if (skip_name(r, n, &i) != 0 || i + 4 > n) return -1;
        i += 4;
    }
    for (size_t k = 0; k < an; k++) {
        if (skip_name(r, n, &i) != 0 || i + 10 > n) return -1;
        uint16_t type = (uint16_t)(r[i] << 8 | r[i + 1]), rdl = (uint16_t)(r[i + 8] << 8 | r[i + 9]);
        i += 10;
        if (i + rdl > n) return -1;
        if (type == 1 && rdl == 4) {
            *ip = (uint32_t)r[i] << 24 | (uint32_t)r[i + 1] << 16 | (uint32_t)r[i + 2] << 8 | r[i + 3];
            return 0;
        }
        i += rdl;    /* e.g. CNAME */
    }
    return -1;
}
