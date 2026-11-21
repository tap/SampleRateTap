#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Interleaved against planar: informational instruction counts, never a gate.

Counts every tools/layout workload (tap_sr_layout_<engine>_ch<C>_<i|p>) in a
build directory and prints each count, then for every pair the planar /
interleaved ratio and whether the two outputs are bit-identical (their
printed checksums agree). Exits nonzero only when a binary yields no count.

  layout.py --target {hexagon,m55,m33} --build-dir DIR --plugin LIB [--json-out FILE]
  layout.py --target host --build-dir DIR [--json-out FILE]

What is counted is tap_sr_layout_block() alone, one 128-frame block through
every channel, summed over the run: on the host callgrind toggles collection
on it, in icount.py's pinned environment; on QEMU each call is bracketed by
tools/probe/call_marker.h's markers and the plugin's TAP_SR_CALL_MAX
"total=" field is the sum. Construction, the fixture and the checksum stay
outside, so the two layouts of a pair are comparable even though the planar
one constructs Ch converters.
"""
import argparse
import glob
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icount  # noqa: E402  (the shared QEMU command lines and host environment)

PREFIX = "tap_sr_layout_"
DONE = re.compile(r"TAP_SR_LAYOUT_DONE ok=1 checksum=(\S+)")
COUNTED = "tap_sr_layout_block"


def measure_host(binary: str) -> tuple[int, str]:
    valgrind = shutil.which("valgrind")
    if valgrind is None:
        raise SystemExit("valgrind not found on PATH")
    cmd = [valgrind, "--tool=callgrind", f"--toggle-collect={COUNTED}",
           "--callgrind-out-file=/dev/null", binary]
    proc = subprocess.run(cmd, timeout=1800, capture_output=True, text=True, env=icount.HOST_ENV)
    out = proc.stdout + proc.stderr
    done = DONE.search(out)
    m = icount.HOST_COLLECTED.search(out)
    if proc.returncode != 0 or not done or not m or int(m.group(1)) == 0:
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: no count")
    return int(m.group(1)), done.group(1)


def measure_qemu(target: str, plugin: str, binary: str) -> tuple[int, str]:
    env = None
    if target == "hexagon":
        os.makedirs(icount.HEXAGON_RUN_DIR, exist_ok=True)
        fixed = os.path.join(icount.HEXAGON_RUN_DIR, icount.HEXAGON_ARGV0)
        shutil.copyfile(binary, fixed)
        os.chmod(fixed, 0o755)
        binary, env = fixed, {}
    proc = subprocess.run(icount.qemu_cmd(target, plugin, binary), timeout=1800,
                          capture_output=True, text=True, env=env)
    out = proc.stdout + proc.stderr
    done = DONE.search(out)
    c = re.search(icount.CALL_MAX_MARKER + r" (\d+) call=(\d+) calls=(\d+) total=(\d+)", out)
    if not done or not c:
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: no count (markers or DONE line missing)")
    return int(c.group(4)), done.group(1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True, choices=["hexagon", "m55", "m33", "host"])
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--plugin")
    ap.add_argument("--json-out")
    args = ap.parse_args()
    if args.target != "host" and not args.plugin:
        raise SystemExit(f"--plugin is required for --target {args.target}")
    binaries = sorted(b for b in glob.glob(os.path.join(args.build_dir, "**", PREFIX + "*"), recursive=True)
                      if os.path.isfile(b) and os.access(b, os.X_OK))
    if not binaries:
        raise SystemExit(f"no {PREFIX}* binaries under {args.build_dir}")
    counts, sums = {}, {}
    for b in binaries:
        name = os.path.basename(b).removeprefix(PREFIX)
        counts[name], sums[name] = (measure_host(b) if args.target == "host"
                                    else measure_qemu(args.target, args.plugin, b))
        print(f"{name}: {counts[name]} insns", flush=True)

    def order(name: str):
        m = re.match(r"(.*)_ch(\d+)_", name)
        return (m.group(1), int(m.group(2))) if m else (name, 0)

    print()
    print(f"{'pair':<16} {'interleaved':>13} {'planar':>13} {'p / i':>7}  output")
    for name in sorted(counts, key=order):
        if not name.endswith("_p") or name[:-2] + "_i" not in counts:
            continue
        i, p = counts[name[:-2] + "_i"], counts[name]
        same = sums[name] == sums[name[:-2] + "_i"]
        print(f"{name[:-2]:<16} {i:>13} {p:>13} {p / i:>7.3f}  {'bit-identical' if same else 'DIFFERENT'}")
    if args.json_out:
        pathlib.Path(args.json_out).write_text(json.dumps(
            {args.target: {k: {"insns": counts[k], "checksum": sums[k]} for k in counts}},
            indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
