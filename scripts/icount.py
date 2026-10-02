#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Deterministic instruction-count ratchet for every engine.

Runs every workload binary of one engine in a build directory under QEMU
with the instruction-counting plugin (tools/qemu_insn_plugin), then
compares against that engine's committed baselines
(async/docs/PERFORMANCE.md, bridge/PLAN.md section 7).

  icount.py --target {hexagon,m55,m33} --build-dir DIR --plugin LIB
            [--engine {async,bridge,rational}] [--baselines FILE] [--tolerance 0.03]
            [--exact] [--update] [--json-out FILE] [--compare-json FILE]

--engine (default async) selects the workload binaries, the guest's
completion marker and the default baselines file (<engine>/bench/
baselines.json). A build directory may hold both engines' workloads; each
run measures only its engine's.

The gate is two-sided: exit nonzero if any scenario regresses beyond
tolerance, improves beyond tolerance (the baseline must be re-recorded so
the gate stays tight), or has no recorded baseline. The measured scenarios
must also be exactly the recorded ones: a baseline with no binary is a
failure, so a workload cannot silently drop out of the gate. --update
rewrites the target's entry to exactly the measured scenarios instead.

--exact demands integer equality (tolerance 0). --json-out records each
scenario's count and the workload's printed checksum; --compare-json gates
exactly against such a file (counts and checksums) instead of the committed
baselines. That is the same-job A/B mode: measure a reference tree and a
candidate tree with one toolchain, then compare the two measurements, so
toolchain drift on the runner cannot masquerade as a code change.
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

# Per engine: the workload binary prefix and the completion marker the
# guest prints. The guest markers are kept byte-identical to the two
# repositories' originals on purpose (rational's from its first baseline):
# they are part of what the counted binaries execute.
ENGINES = {
    "async": {"prefix": "tap_sr_async_icount_", "done": "SRT_ICOUNT_DONE"},
    "bridge": {"prefix": "tap_sr_bridge_icount_", "done": "RATIO_ICOUNT_DONE"},
    "rational": {"prefix": "tap_sr_rational_icount_", "done": "RATIONAL_ICOUNT_DONE"},
}
# Printed by the host-side plugin; never affects the guest's count.
COUNT_MARKER = "TAP_SR_INSN_COUNT"

# qemu-hexagon is a user-mode emulator: the guest's argv[0], its exec path
# (AT_EXECFN) and the host environment are copied onto the guest stack, and
# static musl's startup walks them, so their lengths leak into the count.
# Every workload therefore runs from one fixed path, with a fixed argv[0] and
# an empty environment. The system-mode Arm targets get none of these
# (main(0, NULL) under -nostartfiles).
HEXAGON_RUN_DIR = "/tmp/tap-icount"
HEXAGON_ARGV0 = "w"


def qemu_cmd(target: str, plugin: str, binary: str) -> list[str]:
    # "-d plugin" routes qemu_plugin_outs() to stderr; without it the count
    # line is silently dropped.
    if target == "hexagon":
        # Resolve before the environment is cleared: with env={} there is no
        # PATH, and a bare name would fail (or find a plugin-less qemu).
        qemu = shutil.which("qemu-hexagon")
        if qemu is None:
            raise SystemExit("qemu-hexagon not found on PATH")
        return [qemu, "-0", HEXAGON_ARGV0, "-d", "plugin", "-plugin", plugin, binary]
    if target == "m55":
        return ["qemu-system-arm", "-M", "mps3-an547", "-nographic",
                "-semihosting", "-d", "plugin", "-plugin", plugin,
                "-kernel", binary]
    if target == "m33":
        return ["qemu-system-arm", "-M", "mps2-an505", "-nographic",
                "-semihosting", "-d", "plugin", "-plugin", plugin,
                "-kernel", binary]
    raise SystemExit(f"unknown target {target}")


def measure(target: str, plugin: str, binary: str, done_marker: str) -> tuple[int, str]:
    env = None
    if target == "hexagon":
        os.makedirs(HEXAGON_RUN_DIR, exist_ok=True)
        fixed = os.path.join(HEXAGON_RUN_DIR, HEXAGON_ARGV0)
        shutil.copyfile(binary, fixed)
        os.chmod(fixed, 0o755)
        binary, env = fixed, {}
    try:
        proc = subprocess.run(qemu_cmd(target, plugin, binary), timeout=600,
                              capture_output=True, text=True, env=env)
    except subprocess.TimeoutExpired:
        raise SystemExit(f"{binary}: timed out after 600 s under QEMU")
    out = proc.stdout + proc.stderr
    done = re.search(done_marker + r" ok=1 checksum=(\S+)", out)
    if not done:
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: workload did not complete cleanly")
    m = re.search(COUNT_MARKER + r" (\d+)", out)
    if not m:
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: no {COUNT_MARKER} (plugin not loaded?)")
    return int(m.group(1)), done.group(1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True, choices=["hexagon", "m55", "m33"])
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--engine", choices=sorted(ENGINES), default="async")
    ap.add_argument("--baselines", help="default: <engine>/bench/baselines.json")
    ap.add_argument("--tolerance", type=float, default=0.03)
    ap.add_argument("--exact", action="store_true",
                    help="require identical counts (tolerance 0)")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--json-out", help="write measured counts and checksums here")
    ap.add_argument("--compare-json",
                    help="gate exactly against a --json-out file instead of the baselines")
    args = ap.parse_args()
    if args.update and args.compare_json:
        raise SystemExit("--update and --compare-json are mutually exclusive")
    tolerance = 0.0 if (args.exact or args.compare_json) else args.tolerance

    engine = ENGINES[args.engine]
    prefix = engine["prefix"]
    if args.baselines is None:
        args.baselines = f"{args.engine}/bench/baselines.json"
    binaries = sorted(glob.glob(os.path.join(args.build_dir, "**", prefix + "*"),
                                recursive=True))
    binaries = [b for b in binaries if os.access(b, os.X_OK) and os.path.isfile(b)]
    if not binaries:
        raise SystemExit(f"no {prefix}* binaries under {args.build_dir}")

    path = pathlib.Path(args.baselines)
    ref_checksums = {}
    if args.compare_json:
        ref = json.loads(pathlib.Path(args.compare_json).read_text()).get(args.target, {})
        base = {k: v["insns"] for k, v in ref.items()}
        ref_checksums = {k: v["checksum"] for k, v in ref.items()}
        if not base:
            raise SystemExit(f"{args.compare_json} has no {args.target} measurements")
    elif path.exists():
        baselines = json.loads(path.read_text())
        base = baselines.get(args.target, {})
    elif args.update:
        baselines, base = {}, {}
    else:
        # A missing file must not read as "no baselines, nothing to check".
        raise SystemExit(f"baselines file {path} not found")

    failures = []
    measured = {}
    checksums = {}
    for binary in binaries:
        scenario = os.path.basename(binary).removeprefix(prefix)
        count, checksum = measure(args.target, args.plugin, binary, engine["done"])
        measured[scenario] = count
        checksums[scenario] = checksum
        print(f"{scenario}: checksum={checksum}")
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
            if tolerance == 0.0 and count != recorded:
                verdict = "MISMATCH (exact)"
                failures.append(scenario)
            elif delta > tolerance:
                verdict = "REGRESSION"
                failures.append(scenario)
            elif delta < -tolerance:
                # Two-sided: a stale (too-high) baseline would let future
                # regressions hide inside the slack, so improvements must be
                # committed too.
                verdict = ("IMPROVED beyond tolerance — run icount.py --update "
                           "and commit the baselines file")
                failures.append(scenario)
            print(f"{scenario}: {count} insns vs baseline {recorded} "
                  f"({count - recorded:+d}, {delta:+.4%}) {verdict}")
        if scenario in ref_checksums and ref_checksums[scenario] != checksum:
            print(f"{scenario}: checksum {checksum} vs reference "
                  f"{ref_checksums[scenario]} MISMATCH")
            failures.append(scenario)

    # The measured set must be exactly the recorded set: a workload whose
    # binary vanished would otherwise simply stop being gated.
    missing = sorted(set(base) - set(measured))
    for scenario in missing:
        print(f"{scenario}: recorded but NOT MEASURED (binary missing)")
    if not args.update:
        failures.extend(missing)

    if args.json_out:
        out_path = pathlib.Path(args.json_out)
        doc = json.loads(out_path.read_text()) if out_path.exists() else {}
        doc[args.target] = {k: {"insns": measured[k], "checksum": checksums[k]}
                            for k in sorted(measured)}
        out_path.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")

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
