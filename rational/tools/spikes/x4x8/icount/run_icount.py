#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Spike X-0 (X3): scratch instruction counts of the x8 chains on the M33,
the M55 and Hexagon, built and counted as the ratchet does.

For each target: configure the repository root with the target's toolchain
file, Release, the ratchet's options (TAP_SR_BUILD_ICOUNT_BENCH=ON, tests and
examples off) and this directory's hook.cmake, build, then run every
x0_icount_* binary under QEMU with the counting plugin through
scripts/icount.py's own qemu_cmd (Hexagon from its fixed path, empty
environment). A scenario's figure is (count at 3000 blocks - count at 1000
blocks) / (output frames at 3000 - at 1000): the streaming loop per output
frame, two channels, construction and start-up cancelled.

    icount/run_icount.py --target m33|m55|hexagon --plugin LIB [--noexc] [--check-ratchet]

--check-ratchet also runs scripts/icount.py --engine rational on the same
build against rational/bench/baselines.json (never --update), so the
numbers are known to come from the ratchet's own toolchain and flags.
--noexc builds with TAP_SR_NO_EXCEPTIONS_RTTI=ON, the ratchet's second
configuration. Writes icount/<target>[_noexc].txt.
"""
import argparse
import importlib.util
import os
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[4]
SPIKE = HERE.parent
TOOLCHAINS = {"m33": "cmake/arm-cortex-m33-mps2.cmake", "m55": "cmake/arm-cortex-m55-mps3.cmake",
              "hexagon": "cmake/hexagon-linux-musl.cmake"}
CHAINS = ["down4_down2", "down3_ratio34_down2", "down2_down2_down2", "ratio38_down3",
          "up2_up4", "up2_ratio43_up3", "up2_up2_up2", "up3_ratio83"]
LABEL = {"down4_down2": "↓4 · ↓2", "down3_ratio34_down2": "↓3 · 3/4 · ↓2 (48 matrix today)",
         "down2_down2_down2": "↓2 · ↓2 · ↓2", "ratio38_down3": "3/8 · ↓3 (44.1 matrix today)",
         "up2_up4": "↑2 · ↑4", "up2_ratio43_up3": "↑2 · 4/3 · ↑3 (48 matrix today)",
         "up2_up2_up2": "↑2 · ↑2 · ↑2", "up3_ratio83": "↑3 · 8/3 (44.1 matrix today)"}

_spec = importlib.util.spec_from_file_location("icount", ROOT / "scripts" / "icount.py")
icount = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(icount)


def count(target, plugin, binary):
    """(instructions, output frames) of one run, as icount.measure() runs it."""
    env = None
    if target == "hexagon":
        os.makedirs(icount.HEXAGON_RUN_DIR, exist_ok=True)
        fixed = os.path.join(icount.HEXAGON_RUN_DIR, icount.HEXAGON_ARGV0)
        shutil.copyfile(binary, fixed)
        os.chmod(fixed, 0o755)
        binary, env = fixed, {}
    proc = subprocess.run(icount.qemu_cmd(target, plugin, binary), timeout=900, capture_output=True, text=True,
                          env=env)
    out = proc.stdout + proc.stderr
    done = re.search(icount.ENGINES["rational"]["done"] + r" ok=1 checksum=(\S+)", out)
    n = re.search(icount.COUNT_MARKER + r" (\d+)", out)
    frames = re.search(r"X0_FRAMES (\d+)", out)
    if not (done and n and frames):
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: no count")
    return int(n.group(1)), int(frames.group(1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True, choices=sorted(TOOLCHAINS))
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--noexc", action="store_true")
    ap.add_argument("--check-ratchet", action="store_true")
    args = ap.parse_args()
    plugin = str(pathlib.Path(args.plugin).resolve())
    tag = args.target + ("_noexc" if args.noexc else "")
    build = SPIKE / "build" / f"icount-{tag}"
    cfg = ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
           f"-DCMAKE_TOOLCHAIN_FILE={ROOT / TOOLCHAINS[args.target]}", "-DTAP_SR_BUILD_TESTS=OFF",
           "-DTAP_SR_BUILD_EXAMPLES=OFF", "-DTAP_SR_BUILD_ICOUNT_BENCH=ON",
           f"-DCMAKE_PROJECT_SampleRateTap_INCLUDE={HERE / 'hook.cmake'}"]
    if args.noexc:
        cfg.append("-DTAP_SR_NO_EXCEPTIONS_RTTI=ON")
    subprocess.run(cfg, check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(build), "-j", "4"], check=True, capture_output=True)
    lines = [f"# x8 chains at economy, two channels, 32-frame blocks: instructions per output frame",
             f"# target {args.target}{' (TAP_SR_NO_EXCEPTIONS_RTTI=ON)' if args.noexc else ''}; "
             f"(count at 3000 blocks - count at 1000) / (frames at 3000 - frames at 1000)",
             "| chain | format | insns / output frame | frames (3000 blocks) | count (3000 blocks) |",
             "|---|---|---:|---:|---:|"]
    for fmt in ("float", "q15"):
        for chain in CHAINS:
            c1, f1 = count(args.target, plugin, str(build / f"x0_icount_{chain}_{fmt}_1000"))
            c3, f3 = count(args.target, plugin, str(build / f"x0_icount_{chain}_{fmt}_3000"))
            per = (c3 - c1) / (f3 - f1)
            lines.append(f"| {LABEL[chain]} | {fmt} | {per:.2f} | {f3} | {c3} |")
            print(lines[-1], flush=True)
    if args.check_ratchet:
        r = subprocess.run([sys.executable, str(ROOT / "scripts" / "icount.py"), "--engine", "rational",
                            "--target", args.target, "--build-dir", str(build), "--plugin", plugin,
                            "--baselines", str(ROOT / "rational" / "bench" / "baselines.json")],
                           capture_output=True, text=True, cwd=ROOT)
        lines.append("")
        lines.append(f"# scripts/icount.py --engine rational --target {args.target} on this build, against "
                     f"rational/bench/baselines.json: exit {r.returncode}")
        lines.extend("#   " + ln for ln in r.stdout.splitlines() if "insns" in ln or "FAIL" in ln or "ok" in ln.lower())
        print("\n".join(lines[-3:]))
    (HERE / f"{tag}.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
