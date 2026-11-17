#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Gate 0's variant tree (docs/CHANNELS_PLAN.md section 3, item 3): a copy of
the tree at the reference commit with each engine's channel count a
compile-time constant, pinned to the count each workload runs, and rational's
count-independent code in K12's shared base. Not shipping code.

Usage: make_variant.py <source tree> <variant tree> [--bridge-walk inline|out]
                       [--rational-block inline|out|decimators]

- rational: docs/channels/stage_state_prototype.patch (K12 and the ChT
  parameter), with ChT defaulting to the pinned count.
- bridge, async: the runtime count reads replaced by the pinned constant. With
  one count per build, their cold code is already compiled once, as K12 would
  have it.
Each engine aborts at construction if a workload's count differs from the pin,
so a mis-pinned build cannot pass silently (the audit's gate-2 trap).

The placements gate 0 measured (CHANNELS_PLAN.md section 3): by default the
blocks are placed as the reference places them (bridge's walks and rational's
blocks left to GCC). --bridge-walk out puts bridge's process_walk out of line
(TAP_SR_BRIDGE_NOINLINE); --rational-block out puts every rational block out of
line, and decimators only a decimator's (process_block_dec, the attribute on
the body itself: an out-of-line wrapper that inlines the body spilled again).
"""
import argparse
import pathlib, re, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("src")
ap.add_argument("dst")
ap.add_argument("--bridge-walk", choices=["inline", "out"], default="inline")
ap.add_argument("--rational-block", choices=["inline", "out", "decimators"], default="inline")
args = ap.parse_args()
src = pathlib.Path(args.src).resolve()
dst = pathlib.Path(args.dst).resolve()
if dst.exists():
    shutil.rmtree(dst)
dst.mkdir(parents=True)
subprocess.run(f"tar -C {src} --exclude='./build*' --exclude='./.git' -cf - . | tar -C {dst} -xf -", shell=True, check=True)
subprocess.run(["git", "apply", str(src / "docs/channels/stage_state_prototype.patch")], cwd=dst, check=True)

PIN = """
// Gate 0 (docs/CHANNELS_PLAN.md section 3): the channel count this build pins.
#ifndef TAP_SR_GATE0_CH
#if defined({sc})
#define TAP_SR_GATE0_CH {sc}
#elif defined({hc})
#define TAP_SR_GATE0_CH {hc}
#else
#define TAP_SR_GATE0_CH 2
#endif
#endif
"""

def edit(rel, pairs, pin=None, after=None):
    p = dst / rel
    s = p.read_text()
    if pin:
        i = s.index(after) + len(after)
        s = s[:i] + pin + s[i:]
    for a, b, n in pairs:
        c = s.count(a)
        if c != n:
            raise SystemExit(f"{rel}: expected {n} of {a!r}, found {c}")
        s = s.replace(a, b)
    p.write_text(s)

# rational: ChT defaults to the pin; the derived constructor checks the count.
edit("rational/include/tap/sr/rational/stage.h", [
    ("template <tap::dsp::sample_type S, rational_ratio R, std::size_t ChT = 0>",
     "template <tap::dsp::sample_type S, rational_ratio R, std::size_t ChT = TAP_SR_GATE0_CH>", 1),
    ("            : stage_state<S, R>(channels, p) {}",
     "            : stage_state<S, R>(channels, p) {\n            if (ChT != 0 && channels != ChT) {\n                std::abort();\n            }\n        }", 1),
], pin=PIN.format(sc="TAP_SR_RATIONAL_SC_CH", hc="TAP_SR_RATIONAL_HC_CH") + "#include <cstdlib>\n",
   after='#include "tap/sr/rational/ratio.h"\n')

# rational: where the per-count block is compiled.
p = dst / "rational/include/tap/sr/rational/stage.h"
s = p.read_text()
head = "        template <std::size_t Ch>\n        std::size_t process_block("
if args.rational_block == "out":
    assert s.count(head) == 1
    s = s.replace(head, "        template <std::size_t Ch>\n        TAP_SR_RATIONAL_NOINLINE std::size_t process_block(")
elif args.rational_block == "decimators":
    i = s.index(head)
    j = s.index("{", i)
    depth, k = 0, j
    while True:
        depth += {"{": 1, "}": -1}.get(s[k], 0)
        if depth == 0:
            break
        k += 1
    fn = s[i:k + 1]
    dec = fn.replace("std::size_t process_block(", "TAP_SR_RATIONAL_NOINLINE std::size_t process_block_dec(", 1)
    s = s[:i] + dec + "\n\n" + fn + s[k + 1:]
    a = "if constexpr (ChT != 0) { return process_block<ChT>(in, in_frames, out); } else"
    assert s.count(a) == 1
    s = s.replace(a, "if constexpr (ChT != 0 && k_is_decimator) { return process_block_dec<ChT>(in, in_frames, out); } "
                     "else if constexpr (ChT != 0) { return process_block<ChT>(in, in_frames, out); } else")
p.write_text(s)

# bridge: every runtime count read becomes the pin.
edit("bridge/include/tap/sr/bridge/converter.h", [
    ("""            if (m_channels == 1) {
                return process_walk<1, T>(in, in_frames, out);
            }
            if (m_channels == 2) {
                return process_walk<2, T>(in, in_frames, out);
            }
            return process_walk<0, T>(in, in_frames, out);""",
     """            return process_walk<k_pin, T>(in, in_frames, out);""", 1),
    ("    class basic_converter {\n      public:\n",
     "    class basic_converter {\n      public:\n        static constexpr std::size_t k_pin = TAP_SR_GATE0_CH;\n", 1),
], pin=PIN.format(sc="TAP_SR_BRIDGE_SC_CH", hc="TAP_SR_BRIDGE_HC_CH") + "#include <cstdlib>\n",
   after="#include <vector>\n")
p = dst / "bridge/include/tap/sr/bridge/converter.h"
s = p.read_text()
if args.bridge_walk == "out":
    w = "        template <std::size_t CH, std::size_t T>\n        std::size_t process_walk("
    assert s.count(w) == 1
    s = s.replace(w, "        template <std::size_t CH, std::size_t T>\n        TAP_SR_BRIDGE_NOINLINE std::size_t process_walk(")
head, sep, rest = s.partition("            , m_channels(channels)\n")
assert sep
rest = rest.replace("std::size_t                 m_channels;", "@@DECL@@")
n = rest.count("m_channels")
rest = rest.replace("m_channels", "k_pin").replace("@@DECL@@", "std::size_t                 m_channels;")
# the constructor body: check the count first
anchor = '                tap::dsp::raise<std::invalid_argument>("tap::sr::bridge::basic_converter: channels == 0");\n            }\n'
assert rest.count(anchor) == 1
rest = rest.replace(anchor, anchor + "            if (channels != k_pin) {\n                std::abort();\n            }\n")
p.write_text(head + sep + rest)
print(f"bridge: {n} runtime count reads pinned")

# async: the resampler and the converter read the pin; the layout is a constant.
p = dst / "async/include/tap/sr/async/polyphase_filter.h"
s = p.read_text()
i = s.index("    class fractional_resampler {")
j = s.index("\n    };", i)
cls = s[i:j]
cls = cls.replace("        static constexpr bool k_channel_parallel = TAP_DSP_CHANNEL_PARALLEL != 0 && std::is_floating_point_v<S>;",
                  "        static constexpr bool k_channel_parallel = TAP_DSP_CHANNEL_PARALLEL != 0 && std::is_floating_point_v<S>;\n"
                  "        static constexpr std::size_t k_pin = TAP_SR_GATE0_CH;\n"
                  "        static constexpr bool k_frame_major = k_channel_parallel && k_pin >= TAP_SR_ASYNC_CP_MIN_CHANNELS;", 1)
init_ch = "            , m_channels(channels)\n"
init_fm = "            , m_frame_major(k_channel_parallel && channels >= TAP_SR_ASYNC_CP_MIN_CHANNELS)\n"
assert init_ch in cls and init_fm in cls
cls = cls.replace(init_ch, "@@INITCH@@").replace(init_fm, "@@INITFM@@")
cls = re.sub(r"(std::size_t\s+)m_channels;", r"\1@@DECLCH@@;", cls)
cls = re.sub(r"(bool\s+)m_frame_major;", r"\1@@DECLFM@@;", cls)
na, nb = cls.count("m_channels"), cls.count("m_frame_major")
cls = cls.replace("m_channels", "k_pin").replace("m_frame_major", "k_frame_major")
cls = cls.replace("@@INITCH@@", init_ch).replace("@@INITFM@@", init_fm).replace("@@DECLCH@@", "m_channels").replace("@@DECLFM@@", "m_frame_major")
bad = '                tap::dsp::raise<std::invalid_argument>("fractional_resampler: bad config");\n            }\n'
assert cls.count(bad) == 1
cls = cls.replace(bad, bad + "            if (channels != k_pin) {\n                std::abort();\n            }\n")
s = s[:i] + cls + s[j:]
k = s.index("#ifndef TAP_SR_ASYNC_CP_MIN_CHANNELS")
s = s[:k] + PIN.format(sc="TAP_SR_ASYNC_SC_CH", hc="TAP_SR_ASYNC_HC_CH") + "#include <cstdlib>\n" + s[k:]
p.write_text(s)
print(f"async resampler: {na} count and {nb} layout reads pinned")

p = dst / "async/include/tap/sr/async/converter.h"
s = p.read_text()
i = s.index("    class basic_converter {")
j = s.index("\n    };", i)
cls = s[i:j]
keep = "            , m_resampler(m_bank, m_cfg.channels, k_pop_chunk_frames)\n"
assert keep in cls
cls = cls.replace(keep, "@@KEEP@@")
n = cls.count("m_cfg.channels")
cls = cls.replace("m_cfg.channels", "k_pin").replace("@@KEEP@@", keep)
cls = cls.replace("    class basic_converter {\n      public:\n",
                  "    class basic_converter {\n      public:\n        static constexpr std::size_t k_pin = TAP_SR_GATE0_CH;\n", 1)
first = "            if (m_ring.capacity() / k_pin <= m_high_water_frames) {"
assert cls.count(first) == 1
cls = cls.replace(first, "            if (m_cfg.channels != k_pin) {\n                std::abort();\n            }\n" + first)
s = s[:i] + cls + s[j:]
p.write_text(s)
print(f"async converter: {n} count reads pinned")
