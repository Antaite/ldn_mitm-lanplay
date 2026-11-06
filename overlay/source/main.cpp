/*
 * ldn_mitm-lanplay Tesla overlay
 *   - mode: off / same LAN / internet (lan-play)
 *   - lan-play server list (add / edit / delete / switch)
 *   - live status: connection, ping, players online, virtual IP
 *
 * Every change is written to sdmc:/config/ldn_mitm/lanplay.ini and applied
 * immediately through the ldn_mitm config service.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <sys/stat.h>
#include "ldn.h"

extern "C" {
#include "lanplay/lp_config.h"
#include "lanplay/lp_presets.h"
}

namespace {

    constexpr const char *ConfigPath = "sdmc:" LP_CONFIG_PATH;

    Service              g_ldnSrv;
    LdnMitmConfigService g_ldnConfig;
    bool                 g_serviceOk = false;
    bool                 g_lanplayOk = false;   /* sysmodule supports the lan-play commands */
    char                 g_version[32] = "";
    lp_status_t          g_status;
    lp_config_t          g_cfg;
    std::string          g_toast;
    u64                  g_toastUntil = 0;

    /* Public server pick list probe */
    lp_probe_result_t    g_pubProbe[LP_PROBE_MAX];
    bool                 g_pubProbeValid = false;
    bool                 g_pubProbeRequested = false;

    /* Server probe results, one per saved server */
    lp_probe_result_t    g_probe[LP_PROBE_MAX];
    int                  g_probeCount = 0;
    bool                 g_probeValid = false;
    bool                 g_probeRequested = false;

    /* Room browser */
    lp_room_t            g_rooms[LP_ROOM_MAX];
    u32                  g_roomCount = 0;
    Result               g_roomResult = 0;
    bool                 g_roomScanRequested = false;
    bool                 g_nsOk = false;
    std::unordered_map<u64, std::string> g_gameNames;

    /* Deferred navigation: tsl::goBack() is only called from Gui::update(),
     * never from inside a click handler of the Gui being destroyed. */
    int g_pendingBack = 0;

    u64 NowMs() {
        return armTicksToNs(armGetSystemTick()) / 1000000ULL;
    }

    void Toast(const std::string &s) {
        g_toast = s;
        g_toastUntil = NowMs() + 2500;
    }

    /* ------------------------------------------------------------------ config */

    void LoadConfig() {
        FILE *f = fopen(ConfigPath, "rb");
        if (f == nullptr) {
            lp_config_default(&g_cfg);
            return;
        }
        static char buf[16384];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        lp_config_parse(&g_cfg, buf, n);
    }

    bool SaveConfig() {
        static char buf[16384];
        /* The sysmodule may have changed the virtual IP (conflict on the server): keep its value. */
        if (g_lanplayOk) {
            lp_status_t st;
            if (R_SUCCEEDED(ldnMitmGetLanPlayStatus(&g_ldnConfig, &st)) && lp_ip_is_valid_virtual(st.virt_ip)) {
                g_cfg.virt_ip = st.virt_ip;
            }
        }
        size_t n = lp_config_format(&g_cfg, buf, sizeof(buf));
        if (n == 0) {
            return false;
        }
        mkdir("sdmc:/config", 0777);
        mkdir("sdmc:/config/ldn_mitm", 0777);
        FILE *f = fopen(ConfigPath, "wb");
        if (f == nullptr) {
            return false;
        }
        bool ok = fwrite(buf, 1, n, f) == n;
        fclose(f);
        return ok;
    }

    void RefreshStatus() {
        if (!g_lanplayOk) {
            return;
        }
        lp_status_t st;
        if (R_SUCCEEDED(ldnMitmGetLanPlayStatus(&g_ldnConfig, &st))) {
            g_status = st;
        }
    }

    /* Save + tell the sysmodule to reload (changes take effect immediately). */
    void Apply() {
        if (!SaveConfig()) {
            Toast("無法寫入設定檔");
            return;
        }
        if (g_lanplayOk) {
            if (R_FAILED(ldnMitmReloadLanPlayConfig(&g_ldnConfig))) {
                Toast("ldn_mitm 套用失敗");
                return;
            }
        } else if (g_serviceOk) {
            ldnMitmSetEnabled(&g_ldnConfig, g_cfg.mode != LP_MODE_OFF);
        }
        RefreshStatus();
        Toast("已儲存並套用");
    }

    /* ------------------------------------------------------------------- text */

    const char *ModeText(u32 mode) {
        switch (mode) {
            case LP_MODE_OFF:      return "關閉";
            case LP_MODE_INTERNET: return "網際網路";
            default:               return "同區網";
        }
    }

    const char *StateText(u32 state, tsl::Color *color) {
        const tsl::Color ok(0x0, 0xF, 0x8, 0xF), warn(0xF, 0xC, 0x0, 0xF), bad(0xF, 0x5, 0x5, 0xF), dim(0xA, 0xA, 0xA, 0xF);
        switch (state) {
            case LP_STATE_CONNECTED:        *color = ok;   return "已連線";
            case LP_STATE_RESOLVING:        *color = warn; return "正在查詢伺服器位址…";
            case LP_STATE_CONNECTING:       *color = warn; return "連線中…";
            case LP_STATE_RESOLVE_FAILED:   *color = bad;  return "找不到伺服器（位址錯誤或沒網路）";
            case LP_STATE_NO_RESPONSE:      *color = bad;  return "伺服器沒有回應";
            case LP_STATE_AUTH_REQUIRED:    *color = bad;  return "這台伺服器需要帳號密碼";
            case LP_STATE_AUTH_FAILED:      *color = bad;  return "帳號或密碼錯誤";
            case LP_STATE_NO_NETWORK:       *color = bad;  return "沒有網路連線";
            case LP_STATE_VERSION_MISMATCH: *color = bad;  return "大氣層版本不符，請換對應版本";
            case LP_STATE_NO_SERVER:        *color = warn; return "尚未設定伺服器";
            default:                        *color = dim;  return "未啟用";
        }
    }

    std::string Ip(u32 ip) {
        char s[16];
        lp_ip_to_str(ip, s);
        return s;
    }

    std::string GameName(u64 id) {
        auto it = g_gameNames.find(id);
        if (it != g_gameNames.end()) {
            return it->second;
        }
        std::string name;
        if (g_nsOk) {
            static NsApplicationControlData *data = new NsApplicationControlData;
            u64 size = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, id, data, sizeof(*data), &size)) && size >= sizeof(data->nacp)) {
                NacpLanguageEntry *lang = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &lang)) && lang != nullptr && lang->name[0]) {
                    name = lang->name;
                }
            }
        }
        if (name.empty()) {
            char buf[48];
            snprintf(buf, sizeof(buf), "未安裝的遊戲 %016lX", static_cast<unsigned long>(id));
            name = buf;
        }
        g_gameNames[id] = name;
        return name;
    }

    void DoProbe() {
        static lp_probe_target_t t[LP_PROBE_MAX];
        int n = 0;
        for (int i = 0; i < g_cfg.count && n < LP_PROBE_MAX; i++, n++) {
            memset(&t[n], 0, sizeof(t[n]));
            snprintf(t[n].host, sizeof(t[n].host), "%s", g_cfg.servers[i].host);
            t[n].port = g_cfg.servers[i].port;
        }
        g_probeCount = n;
        g_probeValid = g_lanplayOk && R_SUCCEEDED(ldnMitmProbeServers(&g_ldnConfig, t, sizeof(t[0]) * n, g_probe, sizeof(g_probe[0]) * n));
    }

    void DoPublicProbe() {
        static lp_probe_target_t t[LP_PROBE_MAX];
        int n = 0;
        for (int i = 0; i < lp_preset_count && n < LP_PROBE_MAX; i++, n++) {
            memset(&t[n], 0, sizeof(t[n]));
            snprintf(t[n].host, sizeof(t[n].host), "%s", lp_presets[i].host);
            t[n].port = lp_presets[i].port;
        }
        g_pubProbeValid = g_lanplayOk && R_SUCCEEDED(ldnMitmProbeServers(&g_ldnConfig, t, sizeof(t[0]) * n, g_pubProbe, sizeof(g_pubProbe[0]) * n));
    }

    std::string PubProbeText(int p) {
        if (!g_pubProbeValid || p < 0 || p >= lp_preset_count) return "";
        const lp_probe_result_t &r = g_pubProbe[p];
        if (r.status == LP_PROBE_OK) return std::to_string(r.ping_ms) + " ms";
        if (r.status == LP_PROBE_DNS_FAILED) return "查不到位址";
        return "沒回應";
    }

    /* index in g_cfg.servers with the same host:port, or -1 */
    int FindSaved(const char *host, uint16_t port) {
        for (int i = 0; i < g_cfg.count; i++) {
            if (g_cfg.servers[i].port == port && !strcmp(g_cfg.servers[i].host, host)) return i;
        }
        return -1;
    }

    std::string ProbeText(int idx) {
        if (!g_probeValid || idx < 0 || idx >= g_probeCount) return "";
        const lp_probe_result_t &r = g_probe[idx];
        if (r.status == LP_PROBE_OK) return std::to_string(r.ping_ms) + " ms";
        if (r.status == LP_PROBE_DNS_FAILED) return "查不到位址";
        return "沒回應";
    }

    void DoRoomScan() {
        g_roomCount = 0;
        g_roomResult = ldnMitmScanRooms(&g_ldnConfig, g_rooms, sizeof(g_rooms), &g_roomCount);
        if (g_roomCount > LP_ROOM_MAX) g_roomCount = LP_ROOM_MAX;
    }

    std::string Dots(const std::string &s) {
        std::string r;
        for (size_t i = 0; i < s.size(); i++) {
            r += "●";
        }
        return r;
    }

    bool ProcessPendingNav() {
        if (g_pendingBack > 0) {
            g_pendingBack--;
            tsl::goBack();
            return true;
        }
        return false;
    }

    /* --------------------------------------------------------------- keyboard */

    enum class KbMode { Text, Number, Password };

    class KeyboardGui : public tsl::Gui {
    public:
        KeyboardGui(const std::string &title, std::string *target, size_t maxLen, KbMode mode)
            : m_title(title), m_target(target), m_text(*target), m_maxLen(maxLen), m_mode(mode) {
            if (mode == KbMode::Number) {
                m_rows = { "123", "456", "789", "0" };
            } else {
                m_rows = { "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._@" };
            }
        }

        tsl::elm::Element *createUI() override {
            auto frame = new tsl::elm::OverlayFrame(m_title, "輸入 刪除 切換 完成 取消");
            auto drawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32 h) {
                this->Draw(r, x, y, w, h);
            });
            frame->setContent(drawer);
            return frame;
        }

        void update() override {
            ProcessPendingNav();
        }

        bool handleInput(u64 down, u64 held, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override {
            if (g_pendingBack > 0) {
                return true;
            }
            /* d-pad with key repeat */
            const u64 dirs = HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_AnyLeft | HidNpadButton_AnyRight;
            u64 move = down & dirs;
            if (held & dirs) {
                if (++m_repeat > 20 && (m_repeat % 4) == 0) {
                    move = held & dirs;
                }
            } else {
                m_repeat = 0;
            }
            const int rows = static_cast<int>(m_rows.size()) + 1;   /* + special row */
            if (move & HidNpadButton_AnyUp)    m_row = (m_row + rows - 1) % rows;
            if (move & HidNpadButton_AnyDown)  m_row = (m_row + 1) % rows;
            const int cols = RowLen(m_row);
            if (m_col >= cols) m_col = cols - 1;
            if (move & HidNpadButton_AnyLeft)  m_col = (m_col + cols - 1) % cols;
            if (move & HidNpadButton_AnyRight) m_col = (m_col + 1) % cols;

            if (down & HidNpadButton_A)    Press();
            if (down & HidNpadButton_Y)    Backspace();
            if (down & HidNpadButton_X)    Toggle();
            if (down & HidNpadButton_Plus) Done();
            if (down & HidNpadButton_B)    g_pendingBack++;
            /* consume everything so libtesla does not navigate on its own */
            return true;
        }

    private:
        static constexpr int SpecialCount = 4;   /* 切換, 空白, 刪除, 完成 */

        int RowLen(int row) const {
            if (row < static_cast<int>(m_rows.size())) {
                return static_cast<int>(m_rows[row].size());
            }
            return SpecialCount;
        }

        void Insert(char c) {
            if (m_text.size() < m_maxLen) {
                m_text.push_back(c);
            }
        }

        void Backspace() {
            if (!m_text.empty()) {
                m_text.pop_back();
            }
        }

        void Toggle() {
            if (m_mode == KbMode::Password) m_show = !m_show; else m_upper = !m_upper;
        }

        void Done() {
            *m_target = m_text;
            g_pendingBack++;
        }

        void Press() {
            if (m_row < static_cast<int>(m_rows.size())) {
                char c = m_rows[m_row][m_col];
                if (m_upper && c >= 'a' && c <= 'z') {
                    c = static_cast<char>(c - 'a' + 'A');
                }
                Insert(c);
                return;
            }
            switch (m_col) {
                case 0: Toggle(); break;
                case 1: if (m_mode != KbMode::Number) Insert(' '); break;
                case 2: Backspace(); break;
                case 3: Done(); break;
            }
        }

        void Draw(tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32 h) {
            (void)h;
            /* text box */
            r->drawRect(x, y + 10, w, 50, r->a(tsl::Color(0x2, 0x2, 0x2, 0xF)));
            std::string shown = (m_mode == KbMode::Password && !m_show) ? Dots(m_text) : m_text;
            shown += "_";
            r->drawString(shown.c_str(), false, x + 10, y + 45, 24, r->a(tsl::style::color::ColorText), w - 20);
            char cnt[24];
            snprintf(cnt, sizeof(cnt), "%u/%u", static_cast<unsigned>(m_text.size()), static_cast<unsigned>(m_maxLen));
            r->drawString(cnt, false, x + w - 70, y + 82, 15, r->a(tsl::style::color::ColorDescription));

            const s32 gap = 4;
            const s32 top = y + 95;
            const s32 kw = (w - gap * 9) / 10;
            const s32 kh = 48;
            const int letterRows = static_cast<int>(m_rows.size());
            for (int row = 0; row < letterRows + 1; row++) {
                const int n = RowLen(row);
                const s32 ky = top + row * (kh + gap);
                for (int col = 0; col < n; col++) {
                    s32 kx, kwid;
                    std::string label;
                    if (row < letterRows) {
                        const s32 rowW = n * kw + (n - 1) * gap;
                        kx = x + (w - rowW) / 2 + col * (kw + gap);
                        kwid = kw;
                        char c = m_rows[row][col];
                        if (m_upper && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                        label = std::string(1, c);
                    } else {
                        kwid = (w - gap * (SpecialCount - 1)) / SpecialCount;
                        kx = x + col * (kwid + gap);
                        static const char *special[SpecialCount] = { "大寫", "空白", "刪除", "完成" };
                        label = special[col];
                        if (col == 0 && m_mode == KbMode::Password) label = m_show ? "隱藏" : "顯示";
                        else if (col == 0 && m_upper) label = "小寫";
                        else if (col == 0 && m_mode == KbMode::Number) label = "—";
                    }
                    const bool sel = row == m_row && col == m_col;
                    r->drawRect(kx, ky, kwid, kh, r->a(sel ? tsl::Color(0x0, 0x8, 0xC, 0xF) : tsl::Color(0x3, 0x3, 0x3, 0xF)));
                    auto dim = r->drawString(label.c_str(), false, 0, 0, 22, tsl::style::color::ColorTransparent);
                    r->drawString(label.c_str(), false, kx + (kwid - static_cast<s32>(dim.first)) / 2, ky + 33, 22, r->a(tsl::style::color::ColorText));
                }
            }
        }

        std::string m_title;
        std::string *m_target;
        std::string m_text;
        size_t m_maxLen;
        KbMode m_mode;
        std::vector<std::string> m_rows;
        int m_row = 0, m_col = 0;
        bool m_upper = false, m_show = false;
        u32 m_repeat = 0;
    };

    /* ------------------------------------------------------------ server edit */

    class EditServerGui : public tsl::Gui {
    public:
        explicit EditServerGui(int index) : m_index(index) {
            if (index >= 0 && index < g_cfg.count) {
                const lp_server_t &s = g_cfg.servers[index];
                m_name = s.name; m_host = s.host; m_user = s.user; m_pass = s.pass;
                m_port = std::to_string(s.port);
            } else {
                m_port = std::to_string(LP_DEFAULT_PORT);
            }
        }

        tsl::elm::Element *createUI() override {
            auto frame = new tsl::elm::OverlayFrame(m_index < 0 ? "新增伺服器" : "編輯伺服器", "lan-play 伺服器");
            auto list = new tsl::elm::List();

            list->addItem(new tsl::elm::CategoryHeader("伺服器資料"));
            m_itName = AddField(list, "名稱", &m_name, 31, KbMode::Text);
            m_itHost = AddField(list, "位址（網域或 IP）", &m_host, 127, KbMode::Text);
            m_itPort = AddField(list, "連接埠", &m_port, 5, KbMode::Number);
            m_itUser = AddField(list, "帳號（沒有就留空）", &m_user, 31, KbMode::Text);
            m_itPass = AddField(list, "密碼（沒有就留空）", &m_pass, 63, KbMode::Password);

            list->addItem(new tsl::elm::CategoryHeader("操作"));
            auto save = new tsl::elm::ListItem("儲存並使用這台伺服器");
            save->setClickListener([this](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                this->Save();
                return true;
            });
            list->addItem(save);

            if (m_index >= 0) {
                m_itDelete = new tsl::elm::ListItem("刪除這台伺服器");
                m_itDelete->setClickListener([this](u64 keys) {
                    if (!(keys & HidNpadButton_A)) return false;
                    if (!m_confirmDelete) {
                        m_confirmDelete = true;
                        m_itDelete->setText("再按一次  確認刪除");
                        return true;
                    }
                    LoadConfig();
                    if (m_index < g_cfg.count) {
                        lp_config_remove_server(&g_cfg, m_index);
                        Apply();
                    }
                    g_pendingBack += 2;   /* back to the main screen */
                    return true;
                });
                list->addItem(m_itDelete);
            }

            frame->setContent(list);
            return frame;
        }

        void update() override {
            if (ProcessPendingNav()) return;
            /* refresh field values (they change after returning from the keyboard) */
            m_itName->setValue(m_name.empty() ? "未填" : m_name, m_name.empty());
            m_itHost->setValue(m_host.empty() ? "未填" : m_host, m_host.empty());
            m_itPort->setValue(m_port);
            m_itUser->setValue(m_user.empty() ? "無" : m_user, m_user.empty());
            m_itPass->setValue(m_pass.empty() ? "無" : Dots(m_pass), m_pass.empty());
        }

    private:
        tsl::elm::ListItem *AddField(tsl::elm::List *list, const char *label, std::string *field, size_t maxLen, KbMode mode) {
            auto it = new tsl::elm::ListItem(label);
            std::string title = label;
            it->setClickListener([title, field, maxLen, mode](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                tsl::changeTo<KeyboardGui>(title, field, maxLen, mode);
                return true;
            });
            list->addItem(it);
            return it;
        }

        void Save() {
            if (m_host.empty()) {
                Toast("請先填寫位址");
                return;
            }
            const int port = atoi(m_port.c_str());
            if (port <= 0 || port > 65535) {
                Toast("連接埠要在 1～65535");
                return;
            }
            LoadConfig();
            lp_server_t s;
            memset(&s, 0, sizeof(s));
            snprintf(s.name, sizeof(s.name), "%s", (m_name.empty() ? m_host : m_name).c_str());
            snprintf(s.host, sizeof(s.host), "%s", m_host.c_str());
            snprintf(s.user, sizeof(s.user), "%s", m_user.c_str());
            snprintf(s.pass, sizeof(s.pass), "%s", m_pass.c_str());
            s.port = static_cast<uint16_t>(port);
            if (m_index >= 0 && m_index < g_cfg.count) {
                g_cfg.servers[m_index] = s;
                g_cfg.current = m_index;
            } else {
                const int idx = lp_config_add_server(&g_cfg, &s);
                if (idx < 0) {
                    Toast("伺服器最多 16 台");
                    return;
                }
                g_cfg.current = idx;
            }
            g_cfg.mode = LP_MODE_INTERNET;   /* picking a server means playing online */
            Apply();
            g_pendingBack += 2;              /* edit -> list -> main */
        }

        int m_index;
        std::string m_name, m_host, m_port, m_user, m_pass;
        tsl::elm::ListItem *m_itName = nullptr, *m_itHost = nullptr, *m_itPort = nullptr, *m_itUser = nullptr, *m_itPass = nullptr, *m_itDelete = nullptr;
        bool m_confirmDelete = false;
    };

    /* ------------------------------------------------------ public servers */

    class PublicServerGui : public tsl::Gui {
    public:
        tsl::elm::Element *createUI() override {
            LoadConfig();
            auto frame = new tsl::elm::OverlayFrame("從公開伺服器挑選", "\uE0E0 加入並使用");
            auto list = new tsl::elm::List();
            list->addItem(new tsl::elm::CategoryHeader("公開伺服器（社群架設，可能隨時下線）"));

            /* responding servers first, by latency; the rest keep list order */
            std::vector<int> order;
            for (int p = 0; p < lp_preset_count; p++) order.push_back(p);
            if (g_pubProbeValid) {
                std::stable_sort(order.begin(), order.end(), [](int a, int b) {
                    const bool oa = g_pubProbe[a].status == LP_PROBE_OK, ob = g_pubProbe[b].status == LP_PROBE_OK;
                    if (oa != ob) return oa;
                    if (oa && ob) return g_pubProbe[a].ping_ms < g_pubProbe[b].ping_ms;
                    return false;
                });
            }
            for (int p : order) {
                const lp_preset_t &ps = lp_presets[p];
                std::string label = std::string(ps.region) + "｜" + ps.name;
                std::string v = PubProbeText(p);
                if (FindSaved(ps.host, ps.port) >= 0) v = v.empty() ? "已加入" : "已加入・" + v;
                auto it = new tsl::elm::ListItem(label, v);
                it->setClickListener([p](u64 keys) {
                    if (!(keys & HidNpadButton_A)) return false;
                    const lp_preset_t &ps = lp_presets[p];
                    LoadConfig();
                    int idx = FindSaved(ps.host, ps.port);
                    if (idx < 0) {
                        lp_server_t s;
                        memset(&s, 0, sizeof(s));
                        snprintf(s.name, sizeof(s.name), "%s｜%s", ps.region, ps.name);
                        snprintf(s.host, sizeof(s.host), "%s", ps.host);
                        s.port = ps.port;
                        idx = lp_config_add_server(&g_cfg, &s);
                        if (idx < 0) {
                            Toast("我的伺服器已滿 16 台，請先刪掉幾台");
                            g_pendingBack += 2;
                            return true;
                        }
                    }
                    g_cfg.current = idx;
                    g_cfg.mode = LP_MODE_INTERNET;
                    Apply();
                    g_pendingBack += 2;   /* public list -> server list -> main */
                    return true;
                });
                list->addItem(it);
            }
            list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                r->drawString("按 \uE0E0 會加進「我的伺服器」並開始使用；\n不要的之後在「我的伺服器」按 \uE0E3 刪除。", false, x + 5, y + 22, 14, r->a(tsl::style::color::ColorDescription));
            }), 55);
            frame->setContent(list);
            return frame;
        }

        void update() override {
            ProcessPendingNav();
        }
    };

    /* ------------------------------------------------------------ server list */

    class ServerListGui : public tsl::Gui {
    public:
        tsl::elm::Element *createUI() override {
            LoadConfig();
            auto frame = new tsl::elm::OverlayFrame("伺服器清單", "\uE0E0 使用   \uE0E3 編輯   \uE0E2 重新測試");
            auto list = new tsl::elm::List();

            list->addItem(new tsl::elm::CategoryHeader("我的伺服器"));
            for (int i = 0; i < g_cfg.count; i++) {
                const lp_server_t &s = g_cfg.servers[i];
                const bool inUse = i == g_cfg.current;
                std::string v = ProbeText(i);
                if (inUse) v = v.empty() ? "使用中" : "使用中・" + v;
                auto it = new tsl::elm::ListItem(s.name, v.empty() ? (std::string(s.host) + ":" + std::to_string(s.port)) : v);
                it->setClickListener([i](u64 keys) {
                    if (keys & HidNpadButton_A) {
                        LoadConfig();
                        if (i < g_cfg.count) {
                            g_cfg.current = i;
                            g_cfg.mode = LP_MODE_INTERNET;
                            Apply();
                        }
                        g_pendingBack++;
                        return true;
                    }
                    if (keys & HidNpadButton_Y) {
                        tsl::changeTo<EditServerGui>(i);
                        return true;
                    }
                    return false;
                });
                list->addItem(it);
            }
            if (g_cfg.count < LP_MAX_SERVERS) {
                auto add = new tsl::elm::ListItem("＋ 新增伺服器");
                add->setClickListener([](u64 keys) {
                    if (!(keys & HidNpadButton_A)) return false;
                    tsl::changeTo<EditServerGui>(-1);
                    return true;
                });
                list->addItem(add);
            }
            m_itPublic = new tsl::elm::ListItem("＋ 從公開伺服器挑選…");
            m_itPublic->setClickListener([](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                g_pubProbeRequested = true;
                return true;
            });
            list->addItem(m_itPublic);

            if (g_cfg.count > 0) {
                list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    r->drawString("「沒回應」或「查不到位址」代表那台伺服器\n目前沒開或位址打錯。延遲越低越好。", false, x + 5, y + 22, 14, r->a(tsl::style::color::ColorDescription));
                }), 55);
            } else {
                list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    r->drawString("還沒有伺服器。按「新增伺服器」輸入\n自己或朋友的 lan-play 伺服器位址。", false, x + 5, y + 22, 15, r->a(tsl::style::color::ColorDescription));
                }), 55);
            }

            frame->setContent(list);
            return frame;
        }

        void update() override {
            if (ProcessPendingNav()) return;
            if (g_pubProbeRequested) {
                /* show "測試中…" for one frame, then probe (blocks ~3 s) and open the pick list */
                if (!m_busyShown) {
                    m_itPublic->setValue("測試中…");
                    m_busyShown = true;
                    return;
                }
                m_busyShown = false;
                g_pubProbeRequested = false;
                DoPublicProbe();
                m_itPublic->setValue("");
                tsl::changeTo<PublicServerGui>();
            }
        }

        bool handleInput(u64 down, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override {
            if (down & HidNpadButton_X) {
                g_probeRequested = true;   /* MainGui tests again and re-opens this screen */
                g_pendingBack++;
                return true;
            }
            return false;
        }

    private:
        tsl::elm::ListItem *m_itPublic = nullptr;
        bool m_busyShown = false;
    };

    /* ------------------------------------------------------------ room list */

    class RoomListGui : public tsl::Gui {
    public:
        tsl::elm::Element *createUI() override {
            const lp_server_t *srv = lp_config_current(&g_cfg);
            std::string sub = g_cfg.mode == LP_MODE_INTERNET ? std::string("伺服器：") + (srv ? srv->name : "未設定") : std::string("同區網");
            sub += "　\uE0E3 重新搜尋";
            auto frame = new tsl::elm::OverlayFrame("房間列表", sub);
            auto list = new tsl::elm::List();

            if (R_FAILED(g_roomResult)) {
                const u32 desc = R_DESCRIPTION(g_roomResult);
                const char *msg = desc == 93 ? "對戰模式是「關閉」，\n請先切到同區網或網際網路。"
                                : desc == 92 ? "還沒連上伺服器。\n等狀態變成「已連線」再搜尋。"
                                : "搜尋失敗，請稍後再試。";
                list->addItem(new tsl::elm::CustomDrawer([msg](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    r->drawString(msg, false, x + 5, y + 30, 19, r->a(tsl::style::color::ColorText));
                }), 90);
            } else if (g_roomCount == 0) {
                list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    r->drawString("目前沒有人開房。", false, x + 5, y + 30, 20, r->a(tsl::style::color::ColorText));
                    r->drawString("只會列出「正在等人加入」的房間；\n已經開打的房間不會出現。\n按 \uE0E3 重新搜尋。", false, x + 5, y + 62, 15, r->a(tsl::style::color::ColorDescription));
                }), 130);
            } else {
                char head[48];
                snprintf(head, sizeof(head), "找到 %u 個房間", static_cast<unsigned>(g_roomCount));
                list->addItem(new tsl::elm::CategoryHeader(head));
                for (u32 i = 0; i < g_roomCount; i++) {
                    const lp_room_t room = g_rooms[i];
                    char cnt[48];
                    snprintf(cnt, sizeof(cnt), "%u/%u 人%s", room.node_count, room.node_max, room.is_own ? "（你的房間）" : "");
                    auto item = new tsl::elm::ListItem(GameName(room.local_comm_id), cnt);
                    list->addItem(item);

                    std::string host = std::string("房主：") + (room.players[0][0] ? room.players[0] : "?");
                    std::string members;
                    for (int p = 1; p < LP_ROOM_PLAYERS; p++) {
                        if (!room.players[p][0]) continue;
                        if (!members.empty()) members += "、";
                        members += room.players[p];
                    }
                    std::string ipline = "IP " + Ip(room.host_ip);
                    list->addItem(new tsl::elm::CustomDrawer([host, members, ipline](tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32) {
                        r->drawString(host.c_str(), false, x + 20, y + 20, 16, r->a(tsl::style::color::ColorText), w - 30);
                        if (!members.empty()) {
                            std::string m = "成員：" + members;
                            r->drawString(m.c_str(), false, x + 20, y + 42, 15, r->a(tsl::style::color::ColorDescription), w - 30);
                        }
                        r->drawString(ipline.c_str(), false, x + 20, y + (members.empty() ? 42 : 62), 12, r->a(tsl::style::color::ColorDescription));
                    }), members.empty() ? 52 : 72);
                }
                list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    r->drawString("在遊戲裡進入本地通訊畫面搜尋，就能加入這些房間。", false, x + 5, y + 25, 14, r->a(tsl::style::color::ColorDescription));
                }), 40);
            }

            frame->setContent(list);
            return frame;
        }

        void update() override {
            ProcessPendingNav();
        }

        bool handleInput(u64 down, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override {
            if (down & HidNpadButton_Y) {
                g_roomScanRequested = true;   /* MainGui scans again and re-opens this screen */
                g_pendingBack++;
                return true;
            }
            return false;
        }
    };

    /* ----------------------------------------------------------------- main */

    class MainGui : public tsl::Gui {
    public:
        tsl::elm::Element *createUI() override {
            std::string sub = std::string("ldn_mitm-lanplay ") + g_version;
            auto frame = new tsl::elm::OverlayFrame("區網連線對戰", sub);
            auto list = new tsl::elm::List();

            if (!g_serviceOk) {
                list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    r->drawString("找不到 ldn_mitm。\n請確認已把整包檔案複製到 SD 卡，\n並重新開機進入大氣層。", false, x + 5, y + 30, 19, r->a(tsl::style::color::ColorText));
                }), 110);
                frame->setContent(list);
                return frame;
            }
            if (!g_lanplayOk) {
                list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    r->drawString("目前執行的是原版 ldn_mitm，\n沒有網際網路對戰功能。\n請安裝 ldn_mitm-lanplay 並重新開機。", false, x + 5, y + 30, 19, r->a(tsl::style::color::ColorText));
                }), 110);
            }

            list->addItem(new tsl::elm::CategoryHeader("狀態"));
            list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32 w, s32) {
                DrawStatus(r, x, y, w);
            }), 205);

            list->addItem(new tsl::elm::CategoryHeader("設定"));

            m_itMode = new tsl::elm::ListItem("對戰模式", ModeText(g_cfg.mode));
            m_itMode->setClickListener([this](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                LoadConfig();
                u32 next = g_cfg.mode == LP_MODE_OFF ? LP_MODE_LAN : (g_cfg.mode == LP_MODE_LAN ? LP_MODE_INTERNET : LP_MODE_OFF);
                if (next == LP_MODE_INTERNET && !g_lanplayOk) next = LP_MODE_OFF;
                g_cfg.mode = next;
                Apply();
                m_itMode->setValue(ModeText(g_cfg.mode));
                return true;
            });
            list->addItem(m_itMode);

            if (g_lanplayOk) {
                m_itServer = new tsl::elm::ListItem("網際網路伺服器", "");
                m_itServer->setClickListener([](u64 keys) {
                    if (!(keys & HidNpadButton_A)) return false;
                    g_probeRequested = true;
                    return true;
                });
                list->addItem(m_itServer);

            }

            if (g_lanplayOk) {
                auto rooms = new tsl::elm::ListItem("房間列表", "看誰正在開房");
                rooms->setClickListener([](u64 keys) {
                    if (!(keys & HidNpadButton_A)) return false;
                    g_roomScanRequested = true;
                    return true;
                });
                list->addItem(rooms);

                auto reconnect = new tsl::elm::ListItem("重新連線");
                reconnect->setClickListener([](u64 keys) {
                    if (!(keys & HidNpadButton_A)) return false;
                    if (R_SUCCEEDED(ldnMitmReloadLanPlayConfig(&g_ldnConfig))) Toast("重新連線中…");
                    return true;
                });
                list->addItem(reconnect);
            }

            u32 logging = 0;
            ldnMitmGetLogging(&g_ldnConfig, &logging);
            auto log = new tsl::elm::ToggleListItem("除錯紀錄", logging != 0, "開", "關");
            log->setStateChangedListener([](bool on) { ldnMitmSetLogging(&g_ldnConfig, on); });
            list->addItem(log);

            list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                r->drawString("切換模式或伺服器後，請在遊戲裡重新進入\n「本地通訊／區域連線」畫面才會生效。\n網際網路模式：大家選同一台伺服器就能同房。", false, x + 5, y + 22, 15, r->a(tsl::style::color::ColorDescription));
            }), 75);

            frame->setContent(list);
            return frame;
        }

        void update() override {
            if (ProcessPendingNav()) return;
            if (g_probeRequested) {
                if (!m_scanToastShown) {
                    Toast("正在測試伺服器…");
                    m_scanToastShown = true;
                    return;
                }
                m_scanToastShown = false;
                g_probeRequested = false;
                LoadConfig();
                DoProbe();
                g_toastUntil = 0;
                tsl::changeTo<ServerListGui>();
                return;
            }
            if (g_roomScanRequested) {
                /* draw one frame with the "searching" toast first, then scan (blocks ~1.5 s) */
                if (!m_scanToastShown) {
                    Toast("正在搜尋房間…");
                    m_scanToastShown = true;
                    return;
                }
                m_scanToastShown = false;
                g_roomScanRequested = false;
                DoRoomScan();
                g_toastUntil = 0;
                tsl::changeTo<RoomListGui>();
                return;
            }
            if (m_tick++ % 15 == 0) {   /* ~4 times per second */
                RefreshStatus();
                if (m_itMode) m_itMode->setValue(ModeText(g_cfg.mode));
                if (m_itServer) {
                    const lp_server_t *s = lp_config_current(&g_cfg);
                    m_itServer->setValue(s ? s->name : "未設定", s == nullptr);
                }
            }
        }

    private:
        static void DrawStatus(tsl::gfx::Renderer *r, s32 x, s32 y, s32 w) {
            const auto text = r->a(tsl::style::color::ColorText);
            const auto desc = r->a(tsl::style::color::ColorDescription);
            char line[192];
            s32 ly = y + 25;
            const s32 lh = 27;

            snprintf(line, sizeof(line), "模式：%s", ModeText(g_cfg.mode));
            r->drawString(line, false, x + 5, ly, 19, text);
            ly += lh;

            if (g_cfg.mode != LP_MODE_INTERNET) {
                r->drawString(g_cfg.mode == LP_MODE_LAN ? "和同一個 Wi-Fi 底下的 Switch 對戰" : "遊戲使用原本的本機無線通訊", false, x + 5, ly, 17, desc);
                ly += lh;
            } else {
                tsl::Color c(0xF, 0xF, 0xF, 0xF);
                const char *st = StateText(g_status.state, &c);
                r->drawString("連線：", false, x + 5, ly, 19, text);
                r->drawString(st, false, x + 65, ly, 19, r->a(c), w - 70);
                ly += lh;

                const lp_server_t *s = lp_config_current(&g_cfg);
                snprintf(line, sizeof(line), "伺服器：%s", s ? s->name : "未設定");
                r->drawString(line, false, x + 5, ly, 17, text, w - 10);
                ly += lh - 3;

                char ping[24], online[24];
                if (g_status.ping_ms >= 0) snprintf(ping, sizeof(ping), "%d ms", static_cast<int>(g_status.ping_ms)); else snprintf(ping, sizeof(ping), "—");
                if (g_status.online >= 0) snprintf(online, sizeof(online), "%d 人", static_cast<int>(g_status.online)); else snprintf(online, sizeof(online), "—");
                snprintf(line, sizeof(line), "延遲：%s     在線：%s", ping, online);
                r->drawString(line, false, x + 5, ly, 17, text);
                ly += lh - 3;

                snprintf(line, sizeof(line), "虛擬 IP：%s", Ip(g_status.virt_ip ? g_status.virt_ip : g_cfg.virt_ip).c_str());
                r->drawString(line, false, x + 5, ly, 17, desc);
                ly += lh - 3;

                if (g_status.server_msg[0]) {
                    snprintf(line, sizeof(line), "伺服器訊息：%s", g_status.server_msg);
                    r->drawString(line, false, x + 5, ly, 15, desc, w - 10);
                    ly += lh - 5;
                }
            }

            if (g_lanplayOk && g_status.ams_running[0] != 0 && memcmp(g_status.ams_built, g_status.ams_running, sizeof(g_status.ams_built)) != 0) {
                snprintf(line, sizeof(line), "注意：本版為 AMS %u.%u.%u 編譯，主機是 %u.%u.%u",
                         g_status.ams_built[0], g_status.ams_built[1], g_status.ams_built[2],
                         g_status.ams_running[0], g_status.ams_running[1], g_status.ams_running[2]);
                r->drawString(line, false, x + 5, ly, 14, r->a(tsl::Color(0xF, 0xC, 0x0, 0xF)), w - 10);
                ly += 20;
            }

            if (!g_toast.empty() && NowMs() < g_toastUntil) {
                r->drawString(g_toast.c_str(), false, x + 5, ly + 5, 16, r->a(tsl::Color(0x0, 0xC, 0xF, 0xF)));
            }
        }

        tsl::elm::ListItem *m_itMode = nullptr;
        tsl::elm::ListItem *m_itServer = nullptr;
        u32 m_tick = 0;
        bool m_scanToastShown = false;
    };

}

/* Only the config sub-session is kept, and only while the overlay is shown; the
 * sysmodule has a small fixed number of IPC sessions that games need too. */
static void ConnectLdn() {
    if (g_serviceOk) return;
    tsl::hlp::doWithSmSession([] {
        if (R_FAILED(smGetService(&g_ldnSrv, "ldn:u"))) return;
        const bool ok = R_SUCCEEDED(ldnMitmGetConfigFromService(&g_ldnSrv, &g_ldnConfig));
        serviceClose(&g_ldnSrv);              /* the config session lives on its own */
        g_serviceOk = ok;
    });
}

static void DisconnectLdn() {
    if (!g_serviceOk) return;
    serviceClose(&g_ldnConfig.s);
    g_serviceOk = false;
}

class LanPlayOverlay : public tsl::Overlay {
public:
    void initServices() override {
        fsdevMountSdmc();
        memset(&g_status, 0, sizeof(g_status));
        g_status.ping_ms = -1;
        g_status.online = -1;
        LoadConfig();

        tsl::hlp::doWithSmSession([] {
            g_nsOk = R_SUCCEEDED(nsInitialize());
        });
        ConnectLdn();
        if (g_serviceOk) {
            if (R_FAILED(ldnMitmGetVersion(&g_ldnConfig, g_version))) strcpy(g_version, "?");
            lp_status_t st;
            g_lanplayOk = R_SUCCEEDED(ldnMitmGetLanPlayStatus(&g_ldnConfig, &st));
            if (g_lanplayOk) g_status = st;
        }
    }

    void exitServices() override {
        DisconnectLdn();
        if (g_nsOk) nsExit();
        fsdevUnmountDevice("sdmc");
    }

    void onShow() override {
        ConnectLdn();
        LoadConfig();
        RefreshStatus();
    }

    /* Tesla keeps the overlay loaded while hidden: give the sysmodule its session back. */
    void onHide() override { DisconnectLdn(); }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<LanPlayOverlay>(argc, argv);
}
