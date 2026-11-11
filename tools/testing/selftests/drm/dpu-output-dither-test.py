#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile the actual DPU dither helpers with fake bridge/MMIO boundaries.

This covers opt-in/refcount handling and output configuration, not panel
electrical operation or displayed colour accuracy. No GPU is required.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"static\s+(?:bool|void)\s+" + name + r"\s*\(", source)
    if not match:
        raise ValueError(f"Missing function {name}")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=(Path(__file__).resolve().parents[4]
                    / "drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c"))
parser.add_argument("--cc", default="cc")
args = parser.parse_args()
source = args.source.read_text()
matrix = re.search(r"static u32 dither_matrix\[DITHER_MATRIX_SZ\] = \{.*?\};", source, re.S)
if matrix is None:
    raise ValueError("Missing dither matrix")
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t u32;
#define DITHER_MATRIX_SZ 16
struct device_node { bool enabled; };
struct drm_bridge { struct device_node *of_node; unsigned refs; };
struct drm_encoder { struct drm_bridge *last; };
static struct drm_bridge *drm_bridge_chain_get_last_bridge(struct drm_encoder *e)
{ if (e->last) ++e->last->refs; return e->last; }
static void drm_bridge_put(struct drm_bridge *b)
{ assert(b->refs == 2); --b->refs; }
static bool of_property_read_bool(struct device_node *node, const char *name)
{ assert(!strcmp(name, "qcom,dpu-8bpc-dither")); return node->enabled; }
struct dpu_hw_dither_cfg {
 unsigned c0_bitdepth, c1_bitdepth, c2_bitdepth, c3_bitdepth, temporal_en;
 u32 matrix[DITHER_MATRIX_SZ];
};
struct dpu_hw_pingpong {
 struct { void (*setup_dither)(struct dpu_hw_pingpong *, struct dpu_hw_dither_cfg *); } ops;
};
static unsigned calls;
static bool disabled;
static struct dpu_hw_dither_cfg applied;
static void setup(struct dpu_hw_pingpong *pp, struct dpu_hw_dither_cfg *cfg)
{ (void)pp; ++calls; disabled = !cfg; if (cfg) applied = *cfg; }
'''
suffix = r'''
static void check_depth(unsigned depth)
{
 assert(!disabled);
 assert(applied.c0_bitdepth == depth && applied.c1_bitdepth == depth);
 assert(applied.c2_bitdepth == depth && applied.c3_bitdepth == depth);
 assert(applied.temporal_en == 0);
 assert(!memcmp(applied.matrix, dither_matrix, sizeof(dither_matrix)));
 unsigned mask = 0;
 for (unsigned i = 0; i < DITHER_MATRIX_SZ; ++i) {
   assert(applied.matrix[i] < 16); mask |= 1U << applied.matrix[i];
 }
 assert(mask == 0xffff);
}
int main(void)
{
 struct drm_encoder enc = {0};
 struct drm_bridge bridge = {.refs = 1};
 struct device_node node = {0};
 assert(!dpu_encoder_panel_dithers_8bpc(&enc));
 enc.last = &bridge;
 assert(!dpu_encoder_panel_dithers_8bpc(&enc) && bridge.refs == 1);
 bridge.of_node = &node;
 assert(!dpu_encoder_panel_dithers_8bpc(&enc) && bridge.refs == 1);
 node.enabled = true;
 assert(dpu_encoder_panel_dithers_8bpc(&enc) && bridge.refs == 1);
 struct dpu_hw_pingpong pp = {.ops.setup_dither = setup};
 _dpu_encoder_setup_dither(&pp, 6, false); check_depth(6);
 _dpu_encoder_setup_dither(&pp, 8, false); assert(disabled);
 _dpu_encoder_setup_dither(&pp, 8, true); check_depth(8);
 _dpu_encoder_setup_dither(&pp, 10, true); assert(disabled);
 _dpu_encoder_setup_dither(&pp, 0, true); assert(disabled);
 assert(calls == 5);
 pp.ops.setup_dither = NULL;
 _dpu_encoder_setup_dither(&pp, 8, true); assert(calls == 5);
 puts("PASS: panel opt-in, bridge references, 6/8/10-bpc policy, matrix and temporal-disable");
}
'''
with tempfile.TemporaryDirectory(prefix="dpu-dither-test-") as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(prefix + matrix.group() + "\n" + function(source, "dpu_encoder_panel_dithers_8bpc")
                 + "\n" + function(source, "_dpu_encoder_setup_dither") + suffix)
    subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
