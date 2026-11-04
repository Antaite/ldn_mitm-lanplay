/* SPDX-License-Identifier: GPL-2.0-only
 * Straightforward FIPS 180-4 SHA-1 (only used for lan-play's login challenge). */
#include "lanplay/lp_sha1.h"
#include <string.h>

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void block(uint32_t h[5], const uint8_t p[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 | (uint32_t)p[i*4+2] << 8 | p[i*4+3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void lp_sha1_init(lp_sha1_ctx *c) {
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE; c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0; c->n = 0;
}

void lp_sha1_update(lp_sha1_ctx *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->len += len;
    while (len) {
        size_t k = 64 - c->n; if (k > len) k = len;
        memcpy(c->buf + c->n, p, k); c->n += k; p += k; len -= k;
        if (c->n == 64) { block(c->h, c->buf); c->n = 0; }
    }
}

void lp_sha1_final(lp_sha1_ctx *c, uint8_t out[20]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, zero = 0, lenb[8];
    lp_sha1_update(c, &pad, 1);
    while (c->n != 56) lp_sha1_update(c, &zero, 1);
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    lp_sha1_update(c, lenb, 8);
    for (int i = 0; i < 5; i++) { out[i*4] = c->h[i] >> 24; out[i*4+1] = c->h[i] >> 16; out[i*4+2] = c->h[i] >> 8; out[i*4+3] = c->h[i]; }
}

void lp_sha1(const void *data, size_t len, uint8_t out[20]) {
    lp_sha1_ctx c; lp_sha1_init(&c); lp_sha1_update(&c, data, len); lp_sha1_final(&c, out);
}
