/*
 * Built-in public lan-play servers, taken from the community list behind
 * lan-play.com (github.com/GreatWizard/lan-play-status, data/servers.json,
 * updated 2026-07). Asia first. Many community servers come and go; the
 * overlay pings every entry so dead ones are easy to spot.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "lanplay/lp_presets.h"
#include <string.h>

const lp_preset_t lp_presets[] = {
    { "calvin991",          "台灣", "calvin991.ddns.net",          11451, "" },
    { "switchlanplay-tw",   "台灣", "switchlanplay-tw.ddns.net",   11451, "" },
    { "cpalm 1",            "台灣", "lp1.cpalm.org",               11451, "" },
    { "cpalm 2",            "台灣", "lp2.cpalm.org",               11456, "" },
    { "tgceo 魔物獵人 1",   "香港", "tgceo.cn",                    11451, "魔物獵人 崛起" },
    { "tgceo 魔物獵人 2",   "香港", "tgceo.cn",                    11452, "魔物獵人 崛起" },
    { "tgceo 斯普拉遁 2",   "香港", "tgceo.cn",                    11458, "斯普拉遁 2" },
    { "tgceo 瑪利歐賽車 8", "香港", "tgceo.cn",                    11460, "瑪利歐賽車 8 豪華版" },
    { "z5z",                "香港", "switch.z5z.info",             11451, "" },
    { "346lab",             "日本", "ldn.346lab.org",              11451, "" },
    { "verdandi",           "中國", "lanplay.verdandi.icu",        11451, "" },
    { "ix.tc",              "中國", "ns.ix.tc",                    11451, "" },
    { "asypost",            "中國", "asypost.kmdns.net",           11452, "" },
    { "nplay.asia",         "越南", "switch-la.nplay.asia",        11451, "" },
    { "0mn1b0x",            "澳洲", "switch.0mn1b0x.com",          11453, "" },
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
