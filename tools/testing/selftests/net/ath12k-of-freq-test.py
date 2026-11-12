#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Check OPD2413 DT frequency limits and actual cfg80211 regulatory behavior."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    body = source.index("{", start)
    end, depth = body + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[4])
parser.add_argument("--dtb", type=Path, required=True)
parser.add_argument("--cc", default="cc")
args = parser.parse_args()
mac = (args.source / "drivers/net/wireless/ath/ath12k/mac.c").read_text()
register = function(mac, "static int ath12k_mac_hw_register(")
assert (register.index("SET_IEEE80211_DEV(hw, ab->dev)") <
        register.index("wiphy_read_of_freq_limits(wiphy)") <
        register.index("ieee80211_register_hw(hw)")), "Board limits must precede registration"
assert "kmemdup(ath12k_6ghz_channels" in mac, "Channel arrays must remain per-device copies"
dump = subprocess.check_output(["dtc", "-I", "dtb", "-O", "dts", str(args.dtb)],
                               stderr=subprocess.DEVNULL, text=True)
node = next(n for n in re.findall(r"wifi@0\s*\{(.*?)\};", dump, re.S)
            if '"pci17cb,110e"' in n)
prop = re.search(r"ieee80211-freq-limit\s*=\s*([^;]+);", node).group(1)
limits = [int(v, 16) for v in re.findall(r"0x[0-9a-f]+", prop)]
assert limits == [2400000, 2500000, 4900000, 5925000], limits
of = (args.source / "net/wireless/of.c").read_text()
util = (args.source / "net/wireless/util.c").read_text()
reg = (args.source / "net/wireless/reg.c").read_text()
actual = "\n".join([
    function(util, "bool cfg80211_does_bw_fit_range("),
    function(of, "static bool wiphy_freq_limits_valid_chan("),
    function(of, "static void wiphy_freq_limits_apply("),
    function(reg, "static void handle_channel_custom("),
])
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#define MHZ_TO_KHZ(x) ((x)*1000)
#define MBI_TO_DBI(x) ((x)/100)
#define MBM_TO_DBM(x) ((x)/100)
#define IEEE80211_CHAN_DISABLED 1
#define IEEE80211_CHAN_RADAR 2
#define IEEE80211_CHAN_PSD 4
#define REGULATORY_WIPHY_SELF_MANAGED 1
#define NL80211_DFS_USABLE 0
#define IEEE80211_DFS_MIN_CAC_TIME_MS 60000
#define WARN_ON(x) (!!(x))
#define IS_ERR(p) false
#define IS_ERR_OR_NULL(p) (!(p))
#define pr_debug(...) ((void)0)
enum nl80211_band { BAND_2G, BAND_5G, BAND_6G, NUM_NL80211_BANDS };
struct ieee80211_freq_range { u32 start_freq_khz, end_freq_khz; };
struct ieee80211_channel {
 unsigned center_freq, freq_offset, flags, orig_flags;
 unsigned dfs_state_entered, dfs_state, dfs_cac_ms;
 bool beacon_found;
 int max_antenna_gain, max_reg_power, max_power, psd;
};
struct ieee80211_supported_band { int n_channels; struct ieee80211_channel *channels; };
struct wiphy { struct ieee80211_supported_band *bands[NUM_NL80211_BANDS]; unsigned regulatory_flags; };
struct ieee80211_power_rule { int max_antenna_gain, max_eirp; };
struct ieee80211_reg_rule { struct ieee80211_power_rule power_rule; unsigned flags, dfs_cac_ms; int psd; };
struct ieee80211_regdomain { unsigned flags; };
static unsigned jiffies = 1;
static struct ieee80211_reg_rule rule = {.power_rule={600,2400}};
static u32 ieee80211_channel_to_khz(struct ieee80211_channel *c) { return c->center_freq*1000; }
/* A firmware country update that permits every tested frequency. */
static const struct ieee80211_reg_rule *freq_reg_info_regd(u32 f,const struct ieee80211_regdomain *r,u32 bw) {
 (void)f;(void)bw;rule.flags=r->flags;return &rule;
}
static u32 reg_rule_to_chan_bw_flags(const struct ieee80211_regdomain *r,const struct ieee80211_reg_rule *rr,struct ieee80211_channel *c) {
 (void)r;(void)rr;(void)c;return 0;
}
static u32 map_regdom_flags(unsigned flags) { return flags; }
'''
suffix = r'''
int main(void) {
 struct ieee80211_freq_range limits[2] = {LIMITS};
 struct ieee80211_channel ch[] = {
  {.center_freq=2412}, {.center_freq=2484}, {.center_freq=2405},
  {.center_freq=5200}, {.center_freq=5745}, {.center_freq=5855,.flags=IEEE80211_CHAN_DISABLED}, {.center_freq=5915},
  {.center_freq=5935}, {.center_freq=5955}, {.center_freq=7115}
 };
 struct ieee80211_channel peer[sizeof(ch)/sizeof(*ch)];memcpy(peer,ch,sizeof(ch));
 struct ieee80211_supported_band bands[3] = {{3,ch},{4,ch+3},{3,ch+7}};
 struct wiphy hw = {.bands={bands,bands+1,bands+2},.regulatory_flags=REGULATORY_WIPHY_SELF_MANAGED};
 wiphy_freq_limits_apply(&hw,limits,2);
 for (unsigned i=0;i<sizeof(ch)/sizeof(*ch);i++) {
  bool disabled=i==2 || i==5 || i>=7;
  assert(!!(ch[i].flags&IEEE80211_CHAN_DISABLED)==disabled);
  /* wiphy_register() records the board-filtered flags as orig_flags. */
  ch[i].orig_flags=ch[i].flags;
 }
 assert(peer[7].flags==0 && peer[8].flags==0 && peer[9].flags==0);
 for (int pass=0;pass<3;pass++) {
  struct ieee80211_regdomain country={.flags=pass==1?IEEE80211_CHAN_RADAR:0};
  for (unsigned i=0;i<sizeof(ch)/sizeof(*ch);i++) {
   handle_channel_custom(&hw,&ch[i],&country,20000);
   assert(!!(ch[i].flags&IEEE80211_CHAN_DISABLED)==(i==2 || i==5 || i>=7));
  }
 }
 assert(ch[3].max_power==24 && !(ch[3].flags&IEEE80211_CHAN_DISABLED));
 puts("PASS: compiled OPD2413 limits, 20 MHz edges, private channels, retained firmware restrictions, and 6 GHz stays disabled across actual self-managed regulatory updates");
 return 0;
}
'''.replace("LIMITS", "{" + str(limits[0]) + "," + str(limits[1]) + "},{" +
            str(limits[2]) + "," + str(limits[3]) + "}")
with tempfile.TemporaryDirectory(prefix="ath12k-of-freq-") as directory:
    c, exe = Path(directory) / "test.c", Path(directory) / "test"
    c.write_text(prefix + actual + suffix)
    subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-sign-compare",
                    "-fsanitize=address,undefined",
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
