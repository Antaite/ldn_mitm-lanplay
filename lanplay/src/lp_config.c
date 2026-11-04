/* SPDX-License-Identifier: GPL-2.0-only */
#include "lanplay/lp_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void copy_str(char *dst, size_t cap, const char *src, size_t n) {
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

void lp_config_default(lp_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = LP_MODE_LAN;
    cfg->current = -1;
}

const char *lp_mode_name(uint32_t mode) {
    switch (mode) {
        case LP_MODE_OFF: return "off";
        case LP_MODE_INTERNET: return "internet";
        default: return "lan";
    }
}

static uint32_t parse_mode(const char *v) {
    if (!strcmp(v, "off") || !strcmp(v, "0")) return LP_MODE_OFF;
    if (!strcmp(v, "internet") || !strcmp(v, "2")) return LP_MODE_INTERNET;
    return LP_MODE_LAN;
}

int lp_str_to_ip(const char *s, uint32_t *ip_host) {
    unsigned a, b, c, d; char tail;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return -1;
    if (a > 255 || b > 255 || c > 255 || d > 255) return -1;
    *ip_host = (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

void lp_ip_to_str(uint32_t ip, char out[16]) {
    snprintf(out, 16, "%u.%u.%u.%u", (unsigned)(ip >> 24) & 0xFF, (unsigned)(ip >> 16) & 0xFF,
             (unsigned)(ip >> 8) & 0xFF, (unsigned)ip & 0xFF);
}

int lp_ip_is_valid_virtual(uint32_t ip) {
    if ((ip & LP_SUBNET_MASK) != LP_SUBNET_NET) return 0;
    uint32_t x = (ip >> 8) & 0xFF, y = ip & 0xFF;
    if (y == 0 || y == 255 || x == 255) return 0;
    if (x == 37) return 0;                 /* 10.13.37.x: lan-play gateway */
    return 1;
}

uint32_t lp_generate_virtual_ip(const uint8_t r[2]) {
    uint32_t x = 1 + (r[0] % 254);         /* 1..254 */
    if (x == 37) x = 38;
    uint32_t y = 1 + (r[1] % 254);         /* 1..254 */
    return LP_SUBNET_NET | (x << 8) | y;
}

const lp_server_t *lp_config_current(const lp_config_t *cfg) {
    if (cfg->current < 0 || cfg->current >= cfg->count) return NULL;
    return &cfg->servers[cfg->current];
}

int lp_config_add_server(lp_config_t *cfg, const lp_server_t *srv) {
    if (cfg->count >= LP_MAX_SERVERS) return -1;
    cfg->servers[cfg->count] = *srv;
    if (cfg->servers[cfg->count].port == 0) cfg->servers[cfg->count].port = LP_DEFAULT_PORT;
    if (cfg->current < 0) cfg->current = cfg->count;
    return cfg->count++;
}

void lp_config_remove_server(lp_config_t *cfg, int index) {
    if (index < 0 || index >= cfg->count) return;
    memmove(&cfg->servers[index], &cfg->servers[index + 1], sizeof(lp_server_t) * (cfg->count - index - 1));
    cfg->count--;
    memset(&cfg->servers[cfg->count], 0, sizeof(lp_server_t));
    if (cfg->current == index) cfg->current = cfg->count > 0 ? 0 : -1;
    else if (cfg->current > index) cfg->current--;
}

static void trim(const char **b, const char **e) {
    while (*b < *e && (**b == ' ' || **b == '\t' || **b == '\r')) (*b)++;
    while (*e > *b && ((*e)[-1] == ' ' || (*e)[-1] == '\t' || (*e)[-1] == '\r')) (*e)--;
}

void lp_config_parse(lp_config_t *cfg, const char *buf, size_t len) {
    lp_config_default(cfg);
    int section = -2;               /* -2 none, -1 general, >=0 server index */
    int server_slot[LP_MAX_SERVERS];
    int current_from_file = -1;
    for (int i = 0; i < LP_MAX_SERVERS; i++) server_slot[i] = -1;

    const char *p = buf, *end = buf + len;
    /* skip UTF-8 BOM */
    if (len >= 3 && (uint8_t)p[0] == 0xEF && (uint8_t)p[1] == 0xBB && (uint8_t)p[2] == 0xBF) p += 3;

    while (p < end) {
        const char *ls = p;
        while (p < end && *p != '\n') p++;
        const char *le = p;
        if (p < end) p++;
        trim(&ls, &le);
        if (ls == le || *ls == ';' || *ls == '#') continue;

        if (*ls == '[') {
            const char *ne = memchr(ls, ']', (size_t)(le - ls));
            if (!ne) { section = -2; continue; }
            char name[32];
            copy_str(name, sizeof(name), ls + 1, (size_t)(ne - ls - 1));
            if (!strcmp(name, "general")) {
                section = -1;
            } else if (!strncmp(name, "server", 6)) {
                int idx = atoi(name + 6);
                section = -2;
                if (idx >= 0 && idx < LP_MAX_SERVERS && cfg->count < LP_MAX_SERVERS) {
                    if (server_slot[idx] < 0) {
                        server_slot[idx] = cfg->count;
                        memset(&cfg->servers[cfg->count], 0, sizeof(lp_server_t));
                        cfg->servers[cfg->count].port = LP_DEFAULT_PORT;
                        cfg->count++;
                    }
                    section = server_slot[idx];
                }
            } else {
                section = -2;
            }
            continue;
        }

        const char *eq = memchr(ls, '=', (size_t)(le - ls));
        if (!eq) continue;
        const char *kb = ls, *ke = eq, *vb = eq + 1, *ve = le;
        trim(&kb, &ke); trim(&vb, &ve);
        char key[32], val[160];
        copy_str(key, sizeof(key), kb, (size_t)(ke - kb));
        copy_str(val, sizeof(val), vb, (size_t)(ve - vb));

        if (section == -1) {
            if (!strcmp(key, "mode")) cfg->mode = parse_mode(val);
            else if (!strcmp(key, "server")) current_from_file = atoi(val);
            else if (!strcmp(key, "ip")) { uint32_t ip; if (!lp_str_to_ip(val, &ip) && lp_ip_is_valid_virtual(ip)) cfg->virt_ip = ip; }
            else if (!strcmp(key, "pmtu")) { int v = atoi(val); cfg->pmtu = (v >= 500 && v <= 1500) ? (uint32_t)v : 0; }
        } else if (section >= 0) {
            lp_server_t *s = &cfg->servers[section];
            if (!strcmp(key, "name")) copy_str(s->name, sizeof(s->name), val, strlen(val));
            else if (!strcmp(key, "host")) copy_str(s->host, sizeof(s->host), val, strlen(val));
            else if (!strcmp(key, "port")) { int v = atoi(val); s->port = (v > 0 && v < 65536) ? (uint16_t)v : LP_DEFAULT_PORT; }
            else if (!strcmp(key, "user")) copy_str(s->user, sizeof(s->user), val, strlen(val));
            else if (!strcmp(key, "pass")) copy_str(s->pass, sizeof(s->pass), val, strlen(val));
        }
    }

    /* Drop servers without a host. */
    for (int i = 0; i < LP_MAX_SERVERS; i++) {
        if (server_slot[i] >= 0 && current_from_file == i) current_from_file = server_slot[i] + 1000;
    }
    int cur = current_from_file >= 1000 ? current_from_file - 1000 : -1;
    for (int i = cfg->count - 1; i >= 0; i--) {
        if (cfg->servers[i].host[0] == 0) {
            if (cur == i) cur = -1; else if (cur > i) cur--;
            memmove(&cfg->servers[i], &cfg->servers[i + 1], sizeof(lp_server_t) * (cfg->count - i - 1));
            cfg->count--;
            memset(&cfg->servers[cfg->count], 0, sizeof(lp_server_t));
        }
    }
    for (int i = 0; i < cfg->count; i++) {
        if (cfg->servers[i].name[0] == 0) copy_str(cfg->servers[i].name, sizeof(cfg->servers[i].name), cfg->servers[i].host, strlen(cfg->servers[i].host));
    }
    cfg->current = (cur >= 0 && cur < cfg->count) ? cur : (cfg->count > 0 ? 0 : -1);
}

#define APPEND(...) do { \
        int _n = snprintf(buf + pos, cap > pos ? cap - pos : 0, __VA_ARGS__); \
        if (_n < 0 || pos + (size_t)_n >= cap) return 0; \
        pos += (size_t)_n; } while (0)

size_t lp_config_format(const lp_config_t *cfg, char *buf, size_t cap) {
    size_t pos = 0;
    char ip[16] = "";
    if (cfg->virt_ip) lp_ip_to_str(cfg->virt_ip, ip);
    APPEND("; ldn_mitm-lanplay 設定檔（Tesla 覆蓋層會自動改寫）\n");
    APPEND("; mode = off（關閉）/ lan（同區網）/ internet（網際網路 lan-play）\n");
    APPEND("[general]\nmode=%s\nserver=%d\nip=%s\npmtu=%u\n", lp_mode_name(cfg->mode), cfg->current, ip, (unsigned)cfg->pmtu);
    for (int i = 0; i < cfg->count; i++) {
        const lp_server_t *s = &cfg->servers[i];
        APPEND("\n[server%d]\nname=%s\nhost=%s\nport=%u\nuser=%s\npass=%s\n", i, s->name, s->host, (unsigned)s->port, s->user, s->pass);
    }
    return pos;
}
