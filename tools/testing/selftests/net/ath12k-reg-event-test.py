#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Exercise actual ath12k regulatory validation/event control flow on the host."""
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
parser.add_argument("--source", type=Path,
                    default=Path(__file__).resolve().parents[4])
parser.add_argument("--cc", default="cc")
args = parser.parse_args()
driver = args.source / "drivers/net/wireless/ath/ath12k"
status = re.search(r"enum ath12k_reg_status \{.*?\};",
                   (driver / "reg.h").read_text(), re.S).group()
validate = function((driver / "reg.c").read_text(),
                    "enum ath12k_reg_status ath12k_reg_validate_reg_info(")
event = function((driver / "wmi.c").read_text(),
                 "static int ath12k_reg_chan_list_event(")
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
#define GFP_ATOMIC 0
#define REG_SET_CC_STATUS_PASS 0
#define WMI_VDEV_TYPE_UNSPEC 0
#define IEEE80211_REG_UNSET_AP 0
#define ath12k_warn(ab, ...) ((void)(ab))
static unsigned warnings;
#define WARN_ON(x) (warnings += !!(x))
static bool allocation_failure;
static int parse_error, handle_error;
static void *zero_alloc(size_t n)
{ return allocation_failure ? NULL : calloc(1, n); }
#define kzalloc_obj(x, flags) zero_alloc(sizeof(x))
#define kfree free
struct ath12k_reg_info { u8 phy_id; int status_code; char alpha2[2]; };
struct regdomain { char alpha2[2]; };
struct ath12k { unsigned regd_update_completed; };
struct hw_params { bool single_pdev_only; int num_rxdma_per_pdev; };
struct ath12k_base {
 int num_radios;
 struct hw_params *hw_params;
 struct regdomain *default_regd[2], *new_regd[2];
 struct ath12k_reg_info *reg_info[2];
 struct { struct ath12k *ar; } pdevs[2];
};
struct sk_buff { struct ath12k_reg_info info; };
static int ath12k_pull_reg_chan_list_ext_update_ev(struct ath12k_base *ab,
 struct sk_buff *skb, struct ath12k_reg_info *info)
{ (void)ab; if (parse_error) return parse_error; *info = skb->info; return 0; }
static void ath12k_reg_reset_reg_info(struct ath12k_reg_info *info)
{ (void)info; }
static int ath12k_reg_handle_chan_list(struct ath12k_base *ab,
 struct ath12k_reg_info *info, int type, int power)
{ (void)ab; (void)info; (void)type; (void)power; return handle_error; }
static void complete_all(unsigned *completion) { ++*completion; }
'''
suffix = r'''
int main(void)
{
 enum scenario { ACCEPT, REJECT, DUPLICATE, OTHER_RADIO, INVALID_REJECT,
                 INVALID_VALID, BEFORE_ALLOC, PARSE_ERROR, ALLOC_ERROR,
                 SECOND_RADIO, HANDLE_ERROR };
 for (int scenario = ACCEPT; scenario <= HANDLE_ERROR; scenario++) {
  struct ath12k radios[2] = {0};
  struct hw_params params = {.single_pdev_only=true, .num_rxdma_per_pdev=3};
  struct regdomain initial = {.alpha2={'U','S'}};
  struct ath12k_base ab = {.num_radios=1, .hw_params=&params};
  struct sk_buff skb = {.info={.alpha2={'C','A'}}};
  struct ath12k_reg_info *previous = calloc(1, sizeof(*previous));
  assert(previous);
  ab.reg_info[0] = previous;
  ab.default_regd[0] = &initial;
  ab.pdevs[0].ar = &radios[0]; ab.pdevs[1].ar = &radios[1];
  parse_error = handle_error = 0; warnings = 0; allocation_failure = false;
  unsigned expected0=1, expected1=0;
  int expected_ret=ATH12K_REG_STATUS_VALID;
  switch (scenario) {
  case REJECT: skb.info.status_code=1; break;
  case DUPLICATE: memcpy(skb.info.alpha2, "US", 2); break;
  case OTHER_RADIO: skb.info.phy_id=1; expected0=0; break;
  case INVALID_REJECT:
   skb.info.phy_id=255; skb.info.status_code=1; expected0=0; break;
  case INVALID_VALID:
   skb.info.phy_id=255; expected0=0;
   expected_ret=ATH12K_REG_STATUS_FALLBACK; break;
  case BEFORE_ALLOC:
   ab.pdevs[0].ar=NULL; skb.info.status_code=1; expected0=0; break;
  case PARSE_ERROR: parse_error=-EINVAL; expected_ret=-EINVAL; expected0=0; break;
  case ALLOC_ERROR: allocation_failure=true; expected_ret=-ENOMEM; expected0=0; break;
  case SECOND_RADIO:
   ab.num_radios=2; skb.info.phy_id=1; expected0=0; expected1=1; break;
  case HANDLE_ERROR: handle_error=-EINVAL; expected_ret=-EINVAL; break;
  default: break;
  }
  int ret = ath12k_reg_chan_list_event(&ab, &skb);
  if (ret != expected_ret || radios[0].regd_update_completed != expected0 ||
      radios[1].regd_update_completed != expected1) {
   fprintf(stderr, "scenario %d: ret=%d completions=%u,%u expected=%d/%u,%u\n",
           scenario, ret, radios[0].regd_update_completed,
           radios[1].regd_update_completed, expected_ret, expected0, expected1);
   free(ab.reg_info[0]); free(ab.reg_info[1]);
   return 1;
  }
  if (scenario == REJECT || scenario == DUPLICATE || scenario == OTHER_RADIO ||
      scenario == INVALID_REJECT || scenario == BEFORE_ALLOC) {
   assert(ab.reg_info[0] == previous && ab.default_regd[0] == &initial);
   assert(!warnings);
  }
  free(ab.reg_info[0]); free(ab.reg_info[1]);
 }
 puts("PASS: 11 actual ath12k regulatory event cases, including rejected and duplicate replies, invalid radios and pre-allocation");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="ath12k-reg-event-") as directory:
    c = Path(directory) / "test.c"
    exe = Path(directory) / "test"
    c.write_text(prefix + status + "\n" + validate + "\n" + event + suffix)
    subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
