#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Run the actual ath12k range helpers and cleanup worker with firmware mocks."""
import argparse
from pathlib import Path
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
parser.add_argument("--cc", default="cc")
args = parser.parse_args()
source = (args.source / "drivers/net/wireless/ath/ath12k/mac.c").read_text()
signatures = [
    "static u32 ath12k_mac_scan_split_freq(",
    "static u32 ath12k_mac_scan_range_class(",
    "static u32 ath12k_mac_scan_range_count(",
    "static void ath12k_mac_scan_range_tx_resume(",
    "static int ath12k_mac_scan_range_tx_pause(",
    "static void ath12k_mac_scan_range_free(",
    "static int ath12k_mac_scan_range_start(",
    "void __ath12k_mac_scan_finish(",
    "void ath12k_mac_scan_finish(",
    "static void ath12k_scan_abort(",
    "static void ath12k_scan_timeout_work(",
    "static void ath12k_scan_vdev_clean_work(",
]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
typedef uint32_t u32;
#define ARRAY_SIZE(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define fallthrough __attribute__((fallthrough))
#define ATH12K_HW_WCN7860_HW20 1
#define WMI_HOST_HW_MODE_DBS_OR_SBS 5
#define WMI_HOST_WLAN_5GHZ_CAP 2
#define ATH12K_FLUSH_TIMEOUT 3
#define spin_lock_bh(p) ((void)(p))
#define spin_unlock_bh(p) ((void)(p))
#define lockdep_assert_wiphy(p) ((void)(p))
#define lockdep_assert_held(p) ((void)(p))
#define synchronize_net() ((void)0)
#define atomic_read(p) (*(p))
#define wait_event_timeout(q,condition,t) ((void)(q), (void)(t), !!(condition))
#define ath12k_warn(ab, ...) ((void)(ab))
#define ath12k_dbg(ab, ...) ((void)(ab))
static const char *machine = "oneplus,opd2413";
static bool of_machine_is_compatible(const char *s) { return !strcmp(machine,s); }
struct wiphy { int unused; };
struct wiphy_work { int queued; };
struct wiphy_delayed_work { struct wiphy_work work; bool armed; };
struct ieee80211_hw { struct wiphy *wiphy; bool stopped; };
struct ath12k_hw { struct ieee80211_hw *hw; int num_radio; };
struct ath12k_link_vif { bool is_up, is_started; unsigned link_id; };
struct ath12k_wmi_scan_req_arg {
 unsigned num_chan, min_rest_time;
 struct { void *ptr; unsigned len; } extraie;
 unsigned ssids[3], flags;
 u32 chan_list[];
};
struct ath12k_svc_ext_mac_phy_info {
 unsigned phy_id, hw_mode_config_type, supported_bands;
 struct { u32 low_5ghz_freq, high_5ghz_freq; } hw_freq_range;
};
struct ath12k_base {
 int hw_rev, num_radios;
 struct { bool single_pdev_only; } *hw_params;
 struct {
  int preferred_hw_mode;
  struct { struct ath12k_svc_ext_mac_phy_info mac_phy_info[4]; } svc_ext_info;
 } wmi_ab;
};
enum { ATH12K_SCAN_IDLE, ATH12K_SCAN_STARTING, ATH12K_SCAN_RUNNING,
       ATH12K_SCAN_ABORTING };
struct ath12k {
 struct ath12k_base *ab;
 struct ath12k_hw *ah;
 int data_lock;
 void *scan_channel;
 struct { int tx_empty_waitq, num_tx_pending; } dp;
 struct {
  unsigned started, completed;
  int state;
  bool is_roc, roc_notify, finish_queued;
  unsigned roc_freq;
  struct wiphy_delayed_work timeout;
  struct wiphy_work vdev_clean_wk;
  struct ath12k_link_vif *arvif;
  struct ath12k_wmi_scan_req_arg *range_req;
  u32 range_next, range_split_freq;
  bool range_tx_paused;
 } scan;
};
struct cfg80211_scan_info { bool aborted; };
static unsigned live, allocations, fail_alloc, calls, fail_start, completions;
static unsigned cancels, stops, pauses, wakes, sleeps, removed;
static bool aborted;
static u32 seen[32]; static unsigned seen_n;
static void *allocate(size_t n) {
 if (++allocations == fail_alloc) return NULL;
 void *p=calloc(1,n); assert(p); ++live; return p;
}
static void release(void *p) { if (p) { assert(live); --live; free(p); } }
#define kzalloc_flex(x,m,n) allocate(sizeof(x)+(n)*sizeof((x).m[0]))
#define kfree release
static struct ieee80211_hw *ath12k_ar_to_hw(struct ath12k *ar) { return ar->ah->hw; }
static void msleep(unsigned n) { sleeps += n; }
static bool ieee80211_queue_stopped(struct ieee80211_hw *hw,int n) { (void)n;return hw->stopped; }
static void ieee80211_stop_queues(struct ieee80211_hw *hw) { assert(!hw->stopped);hw->stopped=true;++pauses; }
static void ieee80211_wake_queues(struct ieee80211_hw *hw) { assert(hw->stopped);hw->stopped=false;++wakes; }
static void complete_all(unsigned *p) { ++*p; }
static void reinit_completion(unsigned *p) { *p=0; }
static void wiphy_work_queue(struct wiphy *w,struct wiphy_work *p) { (void)w;p->queued=1; }
static void wiphy_work_cancel(struct wiphy *w,struct wiphy_work *p) { (void)w;p->queued=0; }
static void wiphy_delayed_work_cancel(struct wiphy *w,struct wiphy_delayed_work *p) { (void)w;p->armed=false;++cancels; }
static void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw) { (void)hw; }
static void ath12k_mac_scan_send_complete(struct ath12k *ar,struct cfg80211_scan_info *info) {
 (void)ar; ++completions;aborted=info->aborted;
}
static void ath12k_mac_remove_link_interface(struct ieee80211_hw *hw,struct ath12k_link_vif *vif) { (void)hw;(void)vif;++removed; }
static void ath12k_mac_unassign_link_vif(struct ath12k_link_vif *vif) { (void)vif; }
void __ath12k_mac_scan_finish(struct ath12k *ar);
static int ath12k_scan_stop(struct ath12k *ar) {
 ++stops; __ath12k_mac_scan_finish(ar); return 0;
}
static int ath12k_start_scan(struct ath12k *ar,struct ath12k_wmi_scan_req_arg *arg) {
 ++calls;
 if (calls == fail_start) return -EIO;
 assert(arg->flags==0xa5 && arg->ssids[0]==123 && arg->ssids[2]==789);
 assert(arg->extraie.len==4 && !memcmp(arg->extraie.ptr,"IE!!",4));
 assert(seen_n + arg->num_chan <= ARRAY_SIZE(seen));
 memcpy(seen+seen_n,arg->chan_list,arg->num_chan*sizeof(*seen)); seen_n+=arg->num_chan;
 ar->scan.state=ATH12K_SCAN_RUNNING; return 0;
}
'''
suffix = r'''
static struct ath12k_base ab;
static struct ath12k_hw ah;
static struct ieee80211_hw hw;
static struct wiphy wiphy;
static struct ath12k_link_vif vif;
static struct ath12k ar;
static const u32 channels[]={2412,5200,5320,5500,5745,5955,5200,2417};
static void setup(bool associated) {
 assert(!live);
 memset(&ab,0,sizeof(ab));memset(&ah,0,sizeof(ah));memset(&hw,0,sizeof(hw));
 memset(&vif,0,sizeof(vif));memset(&ar,0,sizeof(ar));
 static typeof(*ab.hw_params) params;params.single_pdev_only=true;
 ab.hw_params=&params;ab.hw_rev=ATH12K_HW_WCN7860_HW20;ab.num_radios=1;
 ab.wmi_ab.preferred_hw_mode=WMI_HOST_HW_MODE_DBS_OR_SBS;
 for (int i=0;i<2;i++) {
  struct ath12k_svc_ext_mac_phy_info *p=&ab.wmi_ab.svc_ext_info.mac_phy_info[i];
  p->phy_id=i;p->hw_mode_config_type=5;p->supported_bands=2;
  p->hw_freq_range.low_5ghz_freq=4900;p->hw_freq_range.high_5ghz_freq=i?7125:5320;
 }
 hw.wiphy=&wiphy;ah.hw=&hw;ah.num_radio=1;ar.ab=&ab;ar.ah=&ah;
 vif.is_up=associated;vif.is_started=associated;ar.scan.arvif=&vif;
 allocations=fail_alloc=calls=fail_start=completions=cancels=stops=pauses=wakes=sleeps=removed=seen_n=0;
 aborted=false;machine="oneplus,opd2413";
 struct ath12k_wmi_scan_req_arg *req=allocate(sizeof(*req)+sizeof(channels));
 req->num_chan=ARRAY_SIZE(channels);req->min_rest_time=5;
 req->extraie.ptr=allocate(4);req->extraie.len=4;memcpy(req->extraie.ptr,"IE!!",4);
 req->ssids[0]=123;req->ssids[2]=789;req->flags=0xa5;memcpy(req->chan_list,channels,sizeof(channels));
 ar.scan.range_req=req;ar.scan.range_split_freq=5320;
 ar.scan.state=ATH12K_SCAN_STARTING;ar.scan.timeout.armed=true;
}
static void worker(void) {
 assert(ar.scan.vdev_clean_wk.queued);ar.scan.vdev_clean_wk.queued=0;
 ath12k_scan_vdev_clean_work(&wiphy,&ar.scan.vdev_clean_wk);
}
static void done(void) {
 assert(ar.scan.state==ATH12K_SCAN_IDLE && !ar.scan.range_req);
 assert(!ar.scan.timeout.armed && !ar.scan.range_tx_paused);
 assert(completions==1 && !live && !hw.stopped);
 assert(pauses==wakes);
}
int main(void) {
 setup(false);
 assert(ath12k_mac_scan_split_freq(&ar)==5320);
 for (int i=0;i<3;i++) {
  machine=i==0?"lenovo,baldur":i==1?"lenovo,elden":"oneplus,opd2413";
  assert(ath12k_mac_scan_split_freq(&ar)==5320);
 }
 machine="other,board";assert(!ath12k_mac_scan_split_freq(&ar));machine="oneplus,opd2413";
 ab.hw_rev=0;assert(!ath12k_mac_scan_split_freq(&ar));ab.hw_rev=1;
 ab.num_radios=2;assert(!ath12k_mac_scan_split_freq(&ar));ab.num_radios=1;
 ah.num_radio=2;assert(!ath12k_mac_scan_split_freq(&ar));ah.num_radio=1;
 ab.hw_params->single_pdev_only=false;assert(!ath12k_mac_scan_split_freq(&ar));ab.hw_params->single_pdev_only=true;
 ab.wmi_ab.preferred_hw_mode=0;assert(!ath12k_mac_scan_split_freq(&ar));ab.wmi_ab.preferred_hw_mode=5;
 ab.wmi_ab.svc_ext_info.mac_phy_info[1].hw_freq_range.high_5ghz_freq=5320;
 assert(!ath12k_mac_scan_split_freq(&ar));
 assert(ath12k_mac_scan_range_count(channels,0,5320)==0);
 ath12k_mac_scan_range_free(&ar);assert(!live);
 for (int associated=0;associated<2;associated++) {
  setup(associated);assert(!ath12k_mac_scan_range_start(&ar));
  while (!completions) {
   __ath12k_mac_scan_finish(&ar);worker();
   if (!completions) { assert(ar.scan.timeout.armed);assert(!ar.scan.finish_queued); }
  }
  assert(!aborted && seen_n==ARRAY_SIZE(channels) && !memcmp(seen,channels,sizeof(channels)));
  assert(calls==(associated?8:6));assert(removed==(associated?0:1));done();
 }
 setup(false);assert(!ath12k_mac_scan_range_start(&ar));
 __ath12k_mac_scan_finish(&ar);worker();
 assert(calls==2 && ar.scan.timeout.armed);
 ath12k_scan_timeout_work(&wiphy,&ar.scan.timeout.work);worker();
 assert(stops==1 && aborted && calls==2);done();
 setup(true);assert(!ath12k_mac_scan_range_start(&ar));
 ath12k_mac_scan_finish(&ar);worker();assert(aborted && calls==1);done();
 setup(false);assert(!ath12k_mac_scan_range_start(&ar));
 fail_start=2;__ath12k_mac_scan_finish(&ar);worker();assert(aborted);done();
 setup(false);assert(!ath12k_mac_scan_range_start(&ar));
 fail_alloc=allocations+1;__ath12k_mac_scan_finish(&ar);worker();assert(aborted);done();
 setup(true);ar.dp.num_tx_pending=1;
 assert(ath12k_mac_scan_range_start(&ar)==-ETIMEDOUT && !hw.stopped && !calls);
 ath12k_mac_scan_finish(&ar);worker();assert(aborted);done();
 setup(true);hw.stopped=true;
 assert(ath12k_mac_scan_range_start(&ar)==-EBUSY && !ar.scan.range_tx_paused && !calls);
 hw.stopped=false;ath12k_mac_scan_finish(&ar);worker();assert(aborted);done();
 setup(false);ar.scan.arvif=NULL;ar.scan.vdev_clean_wk.queued=1;
 worker();assert(aborted);done();
 puts("PASS: actual range guard, ordered batches/IE/SSIDs, associated TX drain, overall timeout after intermediate completion, cancel, start/allocation failure, busy TX and missing vdev");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="ath12k-range-scan-") as directory:
    c, exe = Path(directory) / "test.c", Path(directory) / "test"
    c.write_text(prefix + "\n".join(function(source, s) for s in signatures) + suffix)
    subprocess.run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
