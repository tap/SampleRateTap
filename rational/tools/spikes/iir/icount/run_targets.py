#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""IIR-0: configure, build and run the scratch target programs.

    python3 run_targets.py --plugin /path/to/libinsncount.so [--targets m33,m55,...]
                           [--build-root build-iir-targets]

For each target (host, m4, m4f, m33, m55, hexagon) it configures this folder
with the family's toolchain file (DspTap's for the M4 / M4F), builds it
(Release, as the ratchet), runs every iir_w_* workload under QEMU with the
repository's counting plugin (tools/qemu_insn_plugin) and reads the plugin's
TAP_SR_CALL_MAX line: total over the bracketed calls / their count is the
instructions per call (Hexagon: packets, as the ratchet counts them), per
32-frame output block. It also runs iir_ftz_check on every target and keeps
its text. Writes ../outputs/targets.json and ../outputs/targets.md.

Needs on PATH: arm-none-eabi-gcc and qemu-system-arm; for Hexagon the
toolchain's clang (HEXAGON_BIN, default the CI layout under /root/hexagon)
and a plugin-enabled qemu-hexagon (QEMU_HEXAGON, default
/root/qemu-hexagon-plugins/qemu-hexagon, built as ci.yml's icount job does).
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[4]
OUT = HERE.parent / "outputs"
HEX_BIN = os.environ.get("HEXAGON_BIN", "/root/hexagon/clang+llvm-19.1.5-cross-hexagon-unknown-linux-musl/x86_64-linux-gnu/bin")
QEMU_HEX = os.environ.get("QEMU_HEXAGON", "/root/qemu-hexagon-plugins/qemu-hexagon")

TARGETS = {
    "host": {"tc": None},
    "m4": {"tc": REPO / "submodules/dsptap/cmake/arm-cortex-m4-mps2.cmake", "extra": ["-DTAP_DSP_M4_FPU=OFF"],
           "qemu": ["qemu-system-arm", "-M", "mps2-an386", "-nographic", "-semihosting"]},
    "m4f": {"tc": REPO / "submodules/dsptap/cmake/arm-cortex-m4-mps2.cmake", "extra": ["-DTAP_DSP_M4_FPU=ON"],
            "qemu": ["qemu-system-arm", "-M", "mps2-an386", "-nographic", "-semihosting"]},
    "m33": {"tc": REPO / "cmake/arm-cortex-m33-mps2.cmake",
            "qemu": ["qemu-system-arm", "-M", "mps2-an505", "-nographic", "-semihosting"]},
    "m55": {"tc": REPO / "cmake/arm-cortex-m55-mps3.cmake",
            "qemu": ["qemu-system-arm", "-M", "mps3-an547", "-nographic", "-semihosting"]},
    "hexagon": {"tc": REPO / "cmake/hexagon-linux-musl.cmake"},
}


def run(cmd, env=None, timeout=900):
    p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def qemu_cmd(target, plugin, binary):
    if target == "host":
        return [str(binary)]
    if target == "hexagon":
        return [QEMU_HEX, "-d", "plugin", "-plugin", plugin, str(binary)]
    return TARGETS[target]["qemu"] + ["-d", "plugin", "-plugin", plugin, "-kernel", str(binary)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--targets", default="host,m4,m4f,m33,m55,hexagon")
    ap.add_argument("--build-root", default=str(REPO / "build-iir-targets"))
    args = ap.parse_args()
    env = dict(os.environ)
    env["PATH"] = HEX_BIN + os.pathsep + env["PATH"]
    results = json.loads((OUT / "targets.json").read_text()) if (OUT / "targets.json").exists() else {}
    for t in args.targets.split(","):
        bdir = pathlib.Path(args.build_root) / t
        cfg = ["cmake", "-S", str(HERE), "-B", str(bdir), "-DCMAKE_BUILD_TYPE=Release"]
        if TARGETS[t]["tc"]:
            cfg.append(f"-DCMAKE_TOOLCHAIN_FILE={TARGETS[t]['tc']}")
        cfg += TARGETS[t].get("extra", [])
        rc, out = run(cfg, env)
        if rc:
            print(out)
            raise SystemExit(f"{t}: configure failed")
        rc, out = run(["cmake", "--build", str(bdir), "-j", "8"], env, timeout=1800)
        if rc:
            print(out[-4000:])
            raise SystemExit(f"{t}: build failed")
        res = {"counts": {}, "ftz_check": ""}
        for b in sorted(bdir.glob("iir_w_*")):
            if b.suffix or not os.access(b, os.X_OK):
                continue
            name = b.name[len("iir_w_"):]
            if t == "host":
                rc, out = run([str(b)], env)
                res["counts"][name] = None
                continue
            rc, out = run(qemu_cmd(t, args.plugin, b), env)
            m = re.search(r"TAP_SR_CALL_MAX (\d+) call=(\d+) calls=(\d+) total=(\d+)", out)
            if not m or "IIR_WORKLOAD_DONE ok=1" not in out:
                print(out[-2000:])
                raise SystemExit(f"{t}/{name}: no count")
            calls, total = int(m.group(3)), int(m.group(4))
            res["counts"][name] = total / calls
            print(f"{t} {name}: {total / calls:.1f} per call", flush=True)
        chk = bdir / "iir_ftz_check"
        rc, out = run(qemu_cmd(t, args.plugin, chk) if t != "host" else [str(chk)], env)
        res["ftz_check"] = "\n".join(l for l in out.splitlines() if not l.startswith("TAP_SR_"))
        print(res["ftz_check"], flush=True)
        results[t] = res
    OUT.mkdir(exist_ok=True)
    (OUT / "targets.json").write_text(json.dumps(results, indent=1) + "\n")


if __name__ == "__main__":
    main()
