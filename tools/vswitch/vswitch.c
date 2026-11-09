/*
 * 虛擬 Switch 測試工具：在電腦上扮成一台開了「網際網路」模式的 Switch，
 * 連到 lan-play 伺服器，測試連線、列出房間、對真 Switch 做封包測試、開一個測試房間。
 *
 *   vswitch                        互動選單
 *   vswitch status   [-s 伺服器]
 *   vswitch rooms    [-s 伺服器]
 *   vswitch ping IP  [-s 伺服器]
 *   vswitch host     [-s 伺服器] [-t 秒]
 * 伺服器格式：位址[:埠]，預設埠 11451；也可加 -u 帳號 -p 密碼。
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "vs_net.h"
#include "vs_os.h"
#include "lanplay/lp_tunnel.h"
#include "lanplay/lp_config.h"
#include "lanplay/lp_platform.h"
#include "lanplay/vnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VS_VERSION "1.0"
#define CFG_FILE   "vswitch_server.txt"

static lp_tunnel_params_t g_p;

static int out_cb(const uint8_t *p, size_t n) { return lp_tunnel_send_ipv4(p, n); }

static int conflict_cb(uint32_t elapsed, uint32_t *ip) {
    (void)elapsed;
    uint8_t r[2]; lp_random(r, 2);
    *ip = lp_generate_virtual_ip(r);
    vnet_configure(*ip, LP_SUBNET_MASK, 1);
    char s[16]; lp_ip_to_str(*ip, s);
    printf("（虛擬 IP 和別人重複，已自動換成 %s）\n", s);
    return 1;
}

static const char *state_text(uint32_t st) {
    switch (st) {
        case LP_STATE_RESOLVING:       return "正在查詢伺服器位址";
        case LP_STATE_RESOLVE_FAILED:  return "找不到伺服器位址（網址打錯，或 DNS 查不到）";
        case LP_STATE_CONNECTING:      return "連線中（伺服器沒有回應）";
        case LP_STATE_AUTH_REQUIRED:   return "這台伺服器需要帳號密碼";
        case LP_STATE_AUTH_FAILED:     return "帳號或密碼錯誤";
        case LP_STATE_CONNECTED:       return "已連線";
        default:                       return "未連線";
    }
}

static int parse_server(const char *s, lp_tunnel_params_t *p) {
    char buf[160];
    snprintf(buf, sizeof buf, "%s", s);
    char *c = strrchr(buf, ':');
    p->port = 11451;
    if (c) { *c = 0; int port = atoi(c + 1); if (port <= 0 || port > 65535) return 0; p->port = (uint16_t)port; }
    if (!buf[0]) return 0;
    snprintf(p->host, sizeof p->host, "%s", buf);
    return 1;
}

static void load_saved(char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(CFG_FILE, "r");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
}

static void save_server(const char *s) {
    FILE *f = fopen(CFG_FILE, "w");
    if (f) { fprintf(f, "%s\n", s); fclose(f); }
}

static int read_line(const char *prompt, char *out, size_t n) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(out, (int)n, stdin)) return 0;
    out[strcspn(out, "\r\n")] = 0;
    return 1;
}

/* Connects and waits up to 6 s. Returns 1 when connected. */
static int connect_server(int quiet) {
    uint8_t r[2]; lp_random(r, 2);
    g_p.virt_ip = lp_generate_virtual_ip(r);
    vnet_init(out_cb);
    vnet_configure(g_p.virt_ip, LP_SUBNET_MASK, 1);
    lp_tunnel_set_input(vnet_input);
    lp_tunnel_set_conflict_handler(conflict_cb);
    if (!quiet) printf("連線到 %s:%u …\n", g_p.host, g_p.port);
    lp_tunnel_start(&g_p);
    lp_status_t st;
    for (int i = 0; i < 60; i++) {
        lp_tunnel_get_status(&st);
        if (st.state == LP_STATE_CONNECTED || st.state == LP_STATE_RESOLVE_FAILED ||
            st.state == LP_STATE_AUTH_FAILED || st.state == LP_STATE_AUTH_REQUIRED) break;
        lp_sleep_ms(100);
    }
    if (st.state != LP_STATE_CONNECTED) {
        printf("✗ %s\n", state_text(st.state));
        if (st.state == LP_STATE_CONNECTING)
            printf("  請確認：伺服器有在執行、位址和埠（預設 11451）正確、路由器有把 UDP 埠轉到伺服器。\n");
        return 0;
    }
    /* wait a moment for the online count */
    for (int i = 0; i < 20 && st.online < 0; i++) { lp_sleep_ms(100); lp_tunnel_get_status(&st); }
    return 1;
}

static void cmd_status(void) {
    lp_status_t st; lp_tunnel_get_status(&st);
    char ip[16]; lp_ip_to_str(vnet_get_ip(), ip);
    printf("\n狀態：%s\n", state_text(st.state));
    printf("  伺服器　　%s:%u\n", g_p.host, g_p.port);
    printf("  我的虛擬IP %s\n", ip);
    if (st.ping_ms >= 0) printf("  延遲　　　%d ms\n", st.ping_ms);
    if (st.online >= 0)  printf("  伺服器在線 %d 人\n", st.online);
    if (st.server_msg[0])   printf("  伺服器訊息 %s\n", st.server_msg);
    printf("  已送出 %u 個封包、收到 %u 個%s\n", (unsigned)st.up_packets, (unsigned)st.down_packets,
           st.up_fail ? "（有送出失敗）" : "");
}

static vs_room_t g_rooms[32];
static int g_nrooms;

static void cmd_rooms(void) {
    printf("\n搜尋房間中（約 2 秒）…\n");
    g_nrooms = vs_scan_rooms(g_rooms, 32, 2000);
    if (g_nrooms < 0) { printf("✗ 無法搜尋\n"); g_nrooms = 0; return; }
    if (g_nrooms == 0) {
        printf("目前沒有人開房。\n");
        printf("  （只看得到「正在開房」的 Switch；開房的人要在遊戲裡建立房間後才會出現）\n");
        return;
    }
    printf("找到 %d 個房間：\n", g_nrooms);
    for (int i = 0; i < g_nrooms; i++) {
        char ip[16]; lp_ip_to_str(g_rooms[i].host_ip, ip);
        printf("  [%d] 房主 %-16s 遊戲 %016llX  人數 %u/%u  通訊版本 %d  IP %s\n", i + 1,
               g_rooms[i].host_name, (unsigned long long)g_rooms[i].game_id,
               g_rooms[i].players, g_rooms[i].players_max, g_rooms[i].comm_version, ip);
    }
}

static void print_result(const vs_ping_result_t *r, const char *label, const char *note) {
    int loss = r->sent ? (100 * (r->sent - r->received) + r->sent / 2) / r->sent : 100;
    if (r->received)
        printf("  %s  送出 %2d  收到 %2d  掉包 %3d%%  平均 %4d ms  最慢 %4d ms%s\n",
               label, r->sent, r->received, loss, r->avg_ms, r->max_ms, note);
    else
        printf("  %s  送出 %2d  收到  0  掉包 100%%%s\n", label, r->sent, note);
}

static void cmd_ping(uint32_t ip) {
    char s[16]; lp_ip_to_str(ip, s);
    printf("\n封包測試：%s（經過伺服器來回，約 15 秒）\n", s);
    printf("  對方要在線上：請先在那台 Switch 叫出特斯拉面板（會保持連線 1 分鐘），\n");
    printf("  或讓遊戲停在鄰近主機通訊畫面。待機中的 Switch 不會回應。\n\n");

    static const int sizes[] = { 64, 600, 1200, 1400, 1472, 2000, 3000 };
    vs_ping_result_t r[7];
    int n = 7;
    vs_ping_result_t first;
    vs_ping_run(ip, 64, 3, 100, 2000, &first);
    if (first.received == 0) {
        printf("  ✗ 對方完全沒有回應。\n");
        printf("    可能原因：IP 打錯、對方不在網際網路模式、對方在待機中（叫出面板就會醒來）、\n");
        printf("    或你們連的不是同一台伺服器。\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        vs_ping_run(ip, sizes[i] - 28 > 8 ? sizes[i] - 28 : 8, 20, 50, 1500, &r[i]);  /* sizes are whole IP packets */
        r[i].size = sizes[i];
        char label[32]; snprintf(label, sizeof label, "%4d 位元組 ", sizes[i]);
        print_result(&r[i], label, sizes[i] > 1500 ? "（要切片）" : "");
    }
    vs_ping_result_t burst;
    vs_ping_run(ip, 1200 - 28, 40, 0, 2000, &burst);
    print_result(&burst, "連發 40 個  ", "（1200 位元組一次送完）");

    /* verdict */
    int ok_small = r[0].received >= r[0].sent * 9 / 10;
    int lost_1400 = r[3].received < r[3].sent * 9 / 10;
    int lost_big = r[4].received < r[4].sent * 9 / 10 || r[5].received < r[5].sent * 9 / 10 ||
                   r[6].received < r[6].sent * 9 / 10;
    int lost_burst = burst.received < burst.sent * 8 / 10;
    printf("\n結論：");
    if (!ok_small) {
        printf("連小封包都會掉，網路本身不穩（Wi-Fi 訊號、電信業者或伺服器）。\n");
    } else if (lost_1400 || lost_big) {
        if (lost_1400) printf("1400 位元組以上的封包送不過去，這段網路一次能送的大小比一般小。\n");
        else printf("超過約 1470 位元組的封包送不過去：網路會丟掉被切片的封包。\n");
        printf("  遊戲的對戰資料常常這麼大，所以會「加得進房、但比賽連不上」。\n");
        if (g_p.pmtu == 0)
            printf("  解法：把 lanplay.ini 的 pmtu 設成 1200。可以先用「-m 1200」讓這個工具照同樣方式再測一次。\n");
        else
            printf("  已經用 pmtu=%u 測試還是不行：可能是伺服器不支援切片封包，或對方 Switch 沒有設 pmtu。\n", (unsigned)g_p.pmtu);
    } else if (lost_burst) {
        printf("一般封包正常，但連續大量送出時會掉包：對方網路或伺服器可能有限速。\n");
    } else {
        printf("這條路線很健康，對戰資料應該都送得到。\n");
    }
    if (ok_small && r[0].avg_ms > 120) printf("  延遲偏高（%d ms），比賽時可能會頓。\n", r[0].avg_ms);
}

static void on_scan(uint32_t ip) {
    char s[16]; lp_ip_to_str(ip, s);
    printf("  ← %s 正在搜尋房間（房間列表或遊戲的找房畫面）\n", s);
    fflush(stdout);
}

static void cmd_host(int seconds) {
    char ip[16]; lp_ip_to_str(vnet_get_ip(), ip);
    printf("\n已開啟測試房間「虛擬Switch測試」，房主 IP %s。\n", ip);
    printf("在 Switch 的特斯拉面板按「房間列表」，看得到這個房間就代表找房的路線是通的。\n");
    printf("（遊戲裡的找房畫面看不到它，因為它不屬於任何遊戲。）\n");
    if (seconds > 0) printf("%d 秒後自動關閉…\n", seconds);
    else printf("按 Enter 關閉房間。\n");
    vs_host_start(on_scan);
    if (seconds > 0) lp_sleep_ms((uint32_t)seconds * 1000);
    else { char b[8]; read_line("", b, sizeof b); }
    vs_host_stop();
    printf("測試房間已關閉。\n");
}

static void usage(const char *a0) {
    printf("虛擬 Switch 測試工具 v%s\n", VS_VERSION);
    printf("用法：%s [status|rooms|ping IP|host] [-s 伺服器[:埠]] [-u 帳號 -p 密碼] [-m pmtu] [-t 秒] [-v]\n", a0);
    printf("不加參數會打開互動選單。\n");
}

static int interactive(void) {
    char saved[160], line[160];
    printf("====================================\n");
    printf("  虛擬 Switch 測試工具 v%s\n", VS_VERSION);
    printf("====================================\n");
    if (!g_p.host[0]) {
        load_saved(saved, sizeof saved);
        if (saved[0]) {
            printf("伺服器：%s（直接按 Enter 沿用，或輸入新的位址）\n", saved);
            if (!read_line("> ", line, sizeof line)) return 0;
            if (!line[0]) snprintf(line, sizeof line, "%s", saved);
        } else {
            printf("請輸入伺服器位址（例如 example.com:11451）\n");
            if (!read_line("> ", line, sizeof line)) return 0;
        }
        if (!parse_server(line, &g_p)) { printf("位址格式不對。\n"); return 1; }
        save_server(line);
    }
    if (!connect_server(0)) { char b[8]; read_line("按 Enter 結束", b, sizeof b); return 1; }
    cmd_status();
    for (;;) {
        printf("\n[1] 房間列表  [2] 封包測試  [3] 開測試房間  [4] 連線狀態  [0] 離開\n");
        if (!read_line("請選擇：", line, sizeof line)) break;
        if (!strcmp(line, "0") || !strcmp(line, "q") || !strcmp(line, "Q")) break;
        if (!strcmp(line, "1")) cmd_rooms();
        else if (!strcmp(line, "2")) {
            if (g_nrooms > 0) printf("輸入房間編號（見房間列表），或直接輸入對方的虛擬 IP（10.13.x.x）\n");
            else printf("輸入對方 Switch 的虛擬 IP（10.13.x.x，在特斯拉面板最上面的狀態欄）\n");
            if (!read_line("> ", line, sizeof line)) break;
            uint32_t ip = 0;
            int k = atoi(line);
            if (!strchr(line, '.') && k >= 1 && k <= g_nrooms) ip = g_rooms[k - 1].host_ip;
            else if (lp_str_to_ip(line, &ip) != 0 || (ip & 0xFFFF0000u) != LP_SUBNET_NET) { printf("IP 格式不對，要是 10.13.x.x。\n"); continue; }
            cmd_ping(ip);
        }
        else if (!strcmp(line, "3")) cmd_host(0);
        else if (!strcmp(line, "4")) cmd_status();
    }
    lp_tunnel_stop();
    return 0;
}

int main(int argc, char **argv) {
    vs_os_init();
    const char *cmd = NULL, *arg = NULL;
    int seconds = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) { if (!parse_server(argv[++i], &g_p)) { printf("位址格式不對。\n"); return 1; } }
        else if (!strcmp(argv[i], "-u") && i + 1 < argc) snprintf(g_p.user, sizeof g_p.user, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) snprintf(g_p.pass, sizeof g_p.pass, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) g_p.pmtu = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) vs_os_verbose(1);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else if (!cmd) cmd = argv[i];
        else if (!arg) arg = argv[i];
    }
    if (!cmd) return interactive();

    if (!g_p.host[0]) {
        char saved[160]; load_saved(saved, sizeof saved);
        if (!saved[0] || !parse_server(saved, &g_p)) { printf("請用 -s 指定伺服器。\n"); return 1; }
    }
    if (!connect_server(0)) return 2;
    int rc = 0;
    if (!strcmp(cmd, "status")) cmd_status();
    else if (!strcmp(cmd, "rooms")) cmd_rooms();
    else if (!strcmp(cmd, "ping")) {
        uint32_t ip;
        if (!arg || lp_str_to_ip(arg, &ip) != 0) { printf("請給對方的虛擬 IP，例如：ping 10.13.12.34\n"); rc = 1; }
        else cmd_ping(ip);
    }
    else if (!strcmp(cmd, "host")) cmd_host(seconds);
    else { usage(argv[0]); rc = 1; }
    lp_tunnel_stop();
    return rc;
}
