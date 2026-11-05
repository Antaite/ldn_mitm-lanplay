/*
 * Public lan-play servers the overlay offers under 「從公開伺服器挑選」.
 * They are only a pick list: choosing one copies it into the user's own
 * server list. Sources (all unverified, community run, may go offline):
 *   - github.com/GreatWizard/lan-play-status data/servers.json (2026-07)
 *   - nswiki.cn/switch1/tools/lanplay (2026-06)
 *   - carlzeng.com lan-play guide (2024-07)
 * The overlay pings every entry, so dead ones are easy to spot.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "lanplay/lp_presets.h"
#include <string.h>

const lp_preset_t lp_presets[] = {
    /* Taiwan */
    { "calvin991",          "台灣", "calvin991.ddns.net",          11451, "" },
    { "switchlanplay-tw",   "台灣", "switchlanplay-tw.ddns.net",   11451, "" },
    { "cpalm",              "台灣", "lanplay.cpalm.org",           11451, "" },
    { "cpalm 1",            "台灣", "lp1.cpalm.org",               11451, "" },
    { "cpalm 2",            "台灣", "lp2.cpalm.org",               11456, "" },
    /* Hong Kong */
    { "tgceo 魔物獵人 1",   "香港", "tgceo.cn",                    11451, "魔物獵人 崛起" },
    { "tgceo 魔物獵人 2",   "香港", "tgceo.cn",                    11452, "魔物獵人 崛起" },
    { "tgceo 斯普拉遁 2",   "香港", "tgceo.cn",                    11458, "斯普拉遁 2" },
    { "tgceo 瑪利歐賽車 8", "香港", "tgceo.cn",                    11460, "瑪利歐賽車 8 豪華版" },
    { "z5z",                "香港", "switch.z5z.info",             11451, "" },
    /* Japan */
    { "346lab",             "日本", "ldn.346lab.org",              11451, "" },
    /* Mainland China (mostly privately hosted) */
    { "f-ever",             "中國", "lanplay.f-ever.cn",           11451, "" },
    { "kyle666",            "中國", "www.kyle666.top",             11451, "" },
    { "jaxlewis",           "中國", "srv.jaxlewis.top",            11451, "" },
    { "wang54188",          "中國", "wang54188.iask.in",           11451, "" },
    { "carlzeng",           "中國", "switch.carlzeng.com",         11451, "" },
    { "verdandi",           "中國", "lanplay.verdandi.icu",        11451, "" },
    { "ix.tc",              "中國", "ns.ix.tc",                    11451, "" },
    { "asypost",            "中國", "asypost.kmdns.net",           11452, "" },
    { "139.155.72.235",     "中國", "139.155.72.235",              11451, "" },
    { "122.112.211.64",     "中國", "122.112.211.64",              11451, "" },
    { "175.178.187.22",     "中國", "175.178.187.22",              11451, "" },
    { "124.223.87.139",     "中國", "124.223.87.139",              11451, "" },
    /* Southeast Asia / Oceania */
    { "joinsg",             "新加坡", "joinsg.net",                11453, "" },
    { "nplay.asia",         "越南", "switch-la.nplay.asia",        11451, "" },
    { "0mn1b0x",            "澳洲", "switch.0mn1b0x.com",          11453, "" },
    /* Europe / America */
    { "lan-play.com",       "法國", "switch.lan-play.com",         11451, "" },
    { "frog-skins",         "美國", "frog-skins.com",              11451, "" },
    { "teknik",             "美國", "lan.teknik.app",              11451, "" },
};
const int lp_preset_count = (int)(sizeof(lp_presets) / sizeof(lp_presets[0]));

int lp_preset_find(const char *host, uint16_t port) {
    for (int i = 0; i < lp_preset_count; i++) {
        if (lp_presets[i].port == port && !strcmp(lp_presets[i].host, host)) return i;
    }
    return -1;
}
