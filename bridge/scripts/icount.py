#!/usr/bin/env python3
"""Deterministic instruction-count ratchet (PLAN.md section 7).

Runs every ratio_icount_* binary in a build directory under QEMU with the
instruction-counting plugin, then compares against bench/baselines.json.

  icount.py --target {hexagon,m55,m33} --build-dir DIR --plugin LIB [--update]
            [--baselines bench/baselines.json] [--tolerance 0.03]

The gate is two-sided: exit nonzero if any scenario regresses beyond
tolerance, improves beyond tolerance (the baseline must be re-recorded so
the gate stays tight), or has no recorded baseline. --update rewrites the
target's entry to exactly the measured scenarios instead.

Ported from SampleRateTap's scripts/icount.py (marker strings and binary
prefix renamed for this repo).
"""
import argparse
import glob
import json
import os
import pathlib
import re
import subprocess
import sys


def qemu_cmd(target: str, plugin: str, binary: str) -> list[str]:
    # "-d plugin" routes qemu_plugin_outs() to stderr; without it the count
    # line is silently dropped.
    if target == "hexagon":
        return ["qemu-hexagon", "-d", "plugin", "-plugin", plugin, binary]
    if target == "m55":
        return ["qemu-system-arm", "-M", "mps3-an547", "-nographic",
                "-semihosting", "-d", "plugin", "-plugin", plugin,
                "-kernel", binary]
    if target == "m33":
        return ["qemu-system-arm", "-M", "mps2-an505", "-nographic",
                "-semihosting", "-d", "plugin", "-plugin", plugin,
                "-kernel", binary]
    raise SystemExit(f"unknown target {target}")


def measure(target: str, plugin: str, binary: str) -> int:
    try:
        proc = subprocess.run(qemu_cmd(target, plugin, binary), timeout=600,
                              capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        raise SystemExit(f"{binary}: timed out after 600 s under QEMU")
    out = proc.stdout + proc.stderr
    if "RATIO_ICOUNT_DONE ok=1" not in out:
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: workload did not complete cleanly")
    m = re.search(r"RATIO_INSN_COUNT (\d+)", out)
    if not m:
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: no RATIO_INSN_COUNT (plugin not loaded?)")
    return int(m.group(1))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True, choices=["hexagon", "m55", "m33"])
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--baselines", default="bench/baselines.json")
    ap.add_argument("--tolerance", type=float, default=0.03)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()

    binaries = sorted(glob.glob(os.path.join(args.build_dir, "**", "ratio_icount_*"),
                                recursive=True))
    binaries = [b for b in binaries if os.access(b, os.X_OK) and os.path.isfile(b)]
    if not binaries:
        raise SystemExit(f"no ratio_icount_* binaries under {args.build_dir}")

    path = pathlib.Path(args.baselines)
    baselines = json.loads(path.read_text()) if path.exists() else {}
    base = baselines.get(args.target, {})

    failures = []
    measured = {}
    for binary in binaries:
        scenario = os.path.basename(binary).removeprefix("ratio_icount_")
        count = measure(args.target, args.plugin, binary)
        measured[scenario] = count
        recorded = base.get(scenario)
        if recorded is None:
            print(f"{scenario}: {count} insns (NO BASELINE — commit this value)")
            if not args.update:
                failures.append(scenario)
        elif recorded == 0:
            print(f"{scenario}: {count} insns vs baseline 0 (INVALID BASELINE)")
            failures.append(scenario)
        else:
            delta = (count - recorded) / recorded
            verdict = "ok"
            if delta > args.tolerance:
                verdict = "REGRESSION"
                failures.append(scenario)
            elif delta < -args.tolerance:
                # Two-sided: a stale (too-high) baseline would let future
                # regressions hide inside the slack, so improvements must be
                # committed too.
                verdict = ("IMPROVED beyond tolerance — run icount.py --update "
                           "and commit bench/baselines.json")
                failures.append(scenario)
            print(f"{scenario}: {count} insns vs baseline {recorded} "
                  f"({delta:+.2%}) {verdict}")

    if args.update:
        # Exactly the measured scenarios: stale keys for renamed/removed
        # workloads must not linger as dead gate entries.
        baselines[args.target] = measured
        path.write_text(json.dumps(baselines, indent=2, sort_keys=True) + "\n")
        print(f"updated {path}")
        return 0
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
