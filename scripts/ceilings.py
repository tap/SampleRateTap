#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Size and stack ceilings for every engine (the roadmap's A5, row P5).

Measures one engine's probes (tools/probe/probes.cmake, built with the
ratchet's workloads) in a build directory and gates each figure against the
engine's <engine>/bench/ceilings.json, one-sided: a figure above its ceiling
fails; one below it passes (a ceiling is a bound, not a pin).

  ceilings.py measure --engine E --target {m33,m55,hexagon} --build-dir DIR
                      [--ceilings FILE] [--update]
  ceilings.py docs --engine E [--check]

Per target, for each format's size probe (tap_sr_<engine>_probe_<fmt>):

  image          the `size -A` rows .text + .rodata of the whole image: the
                 engine, the runtime it pulls in (newlib or musl, libstdc++
                 or libc++, libgcc, libm) and the startup (the Arm linker
                 scripts place .rodata inside .text, so the sum is the same
                 figure on every leg)
  engine_text    the .text* input sections of the probe's own translation
                 unit, from its link map (<binary>.map): the engine's
                 headers as instantiated, as the link kept them
  engine_rodata  that unit's .rodata* inputs, as compiled (before the
                 linker merges duplicate strings and constants across
                 objects)

On m33 also, for each call of each format's stack probe
(tap_sr_<engine>_stack_<fmt>):

  static    the worst path in bytes from the call's noinline root
            (probe_<call>) through the call graph GCC writes under
            -fstack-usage -fcallgraph-info=su: each function's frame, summed
            along the deepest chain. The precompiled routines it reaches
            (libgcc's soft-double helpers, newlib's memmove) have no frame in
            the graph; they are listed, and counted by the next figure.
  measured  the high-water the probe paints and reads under qemu-system-arm
            over a second of stereo blocks (tools/probe/stack_meter.h): the
            path taken, those routines included.

--update records each figure and its ceiling: measured + 3 %, rounded up to
a multiple of 64 bytes for sizes (DspTap's bench.yml rule) and of 8 bytes
(the AAPCS stack alignment) for stack. `docs` rewrites the table between the
ceilings markers in <engine>/README.md from the file; --check fails if that
would change it.
"""
import argparse
import glob
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys

FORMATS = ["f32", "q15", "q31"]
CALLS = {"async": ["push", "pull"], "bridge": ["up", "down"], "rational": ["up", "down"]}
SIZE_GRAIN = 64
STACK_GRAIN = 8
MARGIN = 0.03
BEGIN = "<!-- ceilings:begin (scripts/ceilings.py docs; do not edit by hand) -->"
END = "<!-- ceilings:end -->"


def ceiling(measured: int, grain: int) -> int:
    return int(math.ceil(measured * (1 + MARGIN) / grain)) * grain


def find_binary(build_dir: str, name: str) -> str:
    hits = [p for p in glob.glob(os.path.join(build_dir, "**", name), recursive=True)
            if os.path.isfile(p) and os.access(p, os.X_OK)]
    if len(hits) != 1:
        raise SystemExit(f"expected one {name} under {build_dir}, found {len(hits)}")
    return hits[0]


def map_inputs(map_path: str) -> list[tuple[str, int, str]]:
    """(input section, bytes, origin file) of every input section the link
    kept, from a GNU ld map (the Arm legs) or an lld one (Hexagon)."""
    lines = pathlib.Path(map_path).read_text().splitlines()
    inputs = []
    if lines and lines[0].split()[:4] == ["VMA", "LMA", "Size", "Align"]:
        for line in lines[1:]:
            m = re.match(r"^\s*[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+(\S.*):\((\.[^)]*)\)$", line)
            if m:
                inputs.append((m.group(3), int(m.group(1), 16), m.group(2)))
        return inputs
    try:
        start = lines.index("Linker script and memory map")  # before it: the discarded inputs
    except ValueError:
        raise SystemExit(f"{map_path}: neither a GNU ld nor an lld map")
    pending = None
    for line in lines[start:]:
        m = re.match(r"^ (\.\S+)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*))?$", line)
        if m and m.group(2) is None:
            pending = m.group(1)  # a long name: address, size and file on the next line
            continue
        if m:
            inputs.append((m.group(1), int(m.group(3), 16), m.group(4)))
        else:
            cont = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
            if cont and pending:
                inputs.append((pending, int(cont.group(2), 16), cont.group(3)))
        pending = None
    return inputs


def size_rows(tool: str, binary: str) -> dict[str, int]:
    out = subprocess.run([tool, "-A", binary], check=True, capture_output=True, text=True).stdout
    rows = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].startswith(".") and parts[1].isdigit():
            rows[parts[0]] = int(parts[1])
    return rows


def measure_size(target: str, probe: str, binary: str) -> dict[str, int]:
    tool = "llvm-size" if target == "hexagon" else "arm-none-eabi-size"
    if shutil.which(tool) is None:
        raise SystemExit(f"{tool} not found on PATH")
    rows = size_rows(tool, binary)
    # The Arm linker scripts place .rodata in the .text output section, so
    # the image is the two rows together on every leg.
    fig = {"image": rows[".text"] + rows.get(".rodata", 0), "engine_text": 0, "engine_rodata": 0}
    own = f"{probe}.dir/probe_main.cpp."
    for name, size, origin in map_inputs(binary + ".map"):
        if own not in origin:
            continue
        if name == ".text" or name.startswith(".text."):
            fig["engine_text"] += size
        elif name == ".rodata" or name.startswith(".rodata."):
            fig["engine_rodata"] += size
    if fig["engine_text"] == 0:
        raise SystemExit(f"{binary}.map: no input from {own}*; the map format moved?")
    return fig


def parse_callgraph(path: str):
    text = pathlib.Path(path).read_text()
    frames, externals = {}, set()
    for title, label in re.findall(r'node: \{ title: "([^"]*)" label: "((?:[^"\\]|\\.)*)"', text):
        m = re.search(r"(\d+) bytes \(([^)]*)\)", label)
        if m is None:
            externals.add(title)
        elif m.group(2) != "static":
            raise SystemExit(f"{path}: {title} has a {m.group(2)} frame; the bound needs static ones")
        else:
            frames[title] = int(m.group(1))
    edges = {}
    for src, dst in re.findall(r'edge: \{ sourcename: "([^"]*)" targetname: "([^"]*)"', text):
        edges.setdefault(src, set()).add(dst)
    return frames, externals, edges


def count_regs(reglist: str) -> tuple[int, int]:
    """(core, vfp double) registers named in an objdump register list."""
    core = vfp = 0
    for item in reglist.split(","):
        item = item.strip()
        lo, _, hi = item.partition("-")
        n = int(hi[1:]) - int(lo[1:]) + 1 if hi else 1
        if lo.startswith("d"):
            vfp += n
        elif lo.startswith("s"):
            vfp += n / 2
        else:
            core += n
    return core, int(math.ceil(vfp))


def runtime_depths(binary: str):
    """Worst-path stack bytes of each function in a linked Arm image, from its
    disassembly: every push, vpush, pre-decrementing store and sub from sp is
    summed (a function that pushes on two branches is charged both, so the
    figure only errs high), plus the deepest callee by bl or tail branch.
    Used for the precompiled runtime routines alone; GCC's own frames cover
    the compiled code."""
    dis = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", binary], check=True,
                         capture_output=True, text=True).stdout
    funcs, cur = {}, None
    for line in dis.splitlines():
        head = re.match(r"^([0-9a-f]+) <([^>]+)>:$", line)
        if head:
            cur = int(head.group(1), 16)
            funcs[cur] = {"name": head.group(2), "frame": 0, "calls": set(), "unbounded": []}
            continue
        ins = re.match(r"^\s*[0-9a-f]+:\s+(\S+)\s*(.*)$", line)
        if cur is None or not ins:
            continue
        mnem, ops = ins.group(1), ins.group(2).split(";")[0].strip()
        f = funcs[cur]
        regs = re.search(r"\{([^}]*)\}", ops)
        if mnem.startswith(("push", "vpush")) or (mnem.startswith(("stmdb", "vstmdb")) and ops.startswith("sp!")):
            core, vfp = count_regs(regs.group(1))
            f["frame"] += 4 * core + 8 * vfp
        elif m := re.search(r"\[sp, #-(\d+)\]!", ops):
            f["frame"] += int(m.group(1))
        elif re.match(r"^sub[sw]?(\.w)?$", mnem) and re.match(r"^sp,", ops):
            m = re.search(r"#(\d+)$", ops)
            if m is None:
                f["unbounded"].append(f"{mnem} {ops}")
            else:
                f["frame"] += int(m.group(1))
        elif mnem.startswith("blx") and not re.search(r"<", ops):
            f["unbounded"].append(f"{mnem} {ops}")
        elif re.match(r"^b[a-z]*(\.[wn])?$", mnem) and re.match(r"^[0-9a-f]+ <[^+>]+>$", ops):
            # bl, or a branch to another function's start (a tail call); a
            # branch within the function names it as <name+offset>
            target = int(ops.split()[0], 16)
            if target != cur:
                f["calls"].add(target)
    memo, active = {}, set()

    def depth(addr):
        if addr in memo:
            return memo[addr]
        f = funcs.get(addr)
        if f is None:
            raise SystemExit(f"{binary}: a call to {addr:#x}, which is no function's start")
        if f["unbounded"]:
            raise SystemExit(f"{binary}: {f['name']} has no static bound: {f['unbounded']}")
        if addr in active:
            raise SystemExit(f"{binary}: recursion through {f['name']}")
        active.add(addr)
        memo[addr] = f["frame"] + max((depth(c) for c in f["calls"]), default=0)
        active.discard(addr)
        return memo[addr]

    symbols = {}
    for line in subprocess.run(["arm-none-eabi-nm", binary], check=True, capture_output=True,
                               text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "TtWw":
            symbols[parts[2]] = int(parts[0], 16) & ~1
    return lambda name: depth(symbols[name]) if name in symbols else None


def worst_path(frames, externals, edges, root, runtime):
    """(bytes, external routines reached) of the deepest path from root: GCC's
    frames for the compiled code, the disassembly's for the runtime."""
    memo, active = {}, set()

    def walk(node):
        if node in memo:
            return memo[node]
        if node in externals or node not in frames:
            # The graph is written before codegen, which can lower a call
            # away (a 64-bit division by a constant): a routine the image
            # does not hold is called by nothing in it, or the link would
            # have failed, so it costs nothing.
            d = runtime(node)
            memo[node] = (0, set()) if d is None else (d, {node})
            return memo[node]
        if node in active:
            raise SystemExit(f"recursion through {node}: no static bound")
        active.add(node)
        deepest, reached = 0, set()
        for callee in edges.get(node, ()):
            d, r = walk(callee)
            deepest = max(deepest, d)
            reached |= r
        active.discard(node)
        memo[node] = (frames[node] + deepest, reached)
        return memo[node]

    return walk(root)


def measure_stack(engine: str, build_dir: str, fmt: str) -> dict[str, dict[str, int]]:
    binary = find_binary(build_dir, f"tap_sr_{engine}_stack_{fmt}")
    proc = subprocess.run(["qemu-system-arm", "-M", "mps2-an505", "-nographic", "-semihosting",
                           "-kernel", binary], capture_output=True, text=True, timeout=900)
    out = proc.stdout + proc.stderr
    if "TAP_SR_PROBE_DONE ok=1" not in out:
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: the probe did not complete cleanly")
    painted = {m.group(1): int(m.group(2)) for m in re.finditer(r"TAP_SR_STACK (\w+) (\d+)", out)}
    graphs = glob.glob(os.path.join(build_dir, "**", f"tap_sr_{engine}_stack_{fmt}.dir", "**", "*.ci"),
                       recursive=True)
    if len(graphs) != 1:
        raise SystemExit(f"expected one call graph for tap_sr_{engine}_stack_{fmt}, found {len(graphs)}"
                         " (built with GCC's -fcallgraph-info=su?)")
    frames, externals, edges = parse_callgraph(graphs[0])
    runtime = runtime_depths(binary)
    result = {}
    for call in CALLS[engine]:
        roots = [t for t in frames if re.search(rf"\dprobe_{call}E", t)]
        if not roots or call not in painted:
            raise SystemExit(f"{engine} {fmt}: no root or no painted figure for {call}")
        bound, reached = max(worst_path(frames, externals, edges, r, runtime) for r in roots)
        if painted[call] > bound:
            # The bound is every frame on every path, the runtime's
            # included: a deeper painted figure means it missed one.
            raise SystemExit(f"{engine} {fmt} {call}: painted {painted[call]} B exceeds the static bound "
                             f"{bound} B; the analysis missed a frame")
        result[call] = {"static": bound, "measured": painted[call]}
        print(f"{engine} {fmt} {call}: static {bound} B, measured {painted[call]} B; "
              f"runtime routines on the paths: {', '.join(sorted(reached)) or 'none'}")
    return result


def gate(label: str, value: int, recorded: dict | None, failures: list) -> None:
    if recorded is None:
        print(f"{label}: {value} B (NO CEILING — record it with --update)")
        failures.append(label)
        return
    verdict = "ok" if value <= recorded["ceiling"] else "ABOVE CEILING"
    if value > recorded["ceiling"]:
        failures.append(label)
    print(f"{label}: {value} B vs ceiling {recorded['ceiling']} (recorded {recorded['recorded']}) {verdict}")


def cmd_measure(args) -> int:
    path = pathlib.Path(args.ceilings or f"{args.engine}/bench/ceilings.json")
    doc = json.loads(path.read_text()) if path.exists() else {}
    if not path.exists() and not args.update:
        raise SystemExit(f"ceilings file {path} not found")
    base = doc.get(args.target, {})
    measured = {"size": {}}
    for fmt in FORMATS:
        probe = f"tap_sr_{args.engine}_probe_{fmt}"
        measured["size"][fmt] = measure_size(args.target, probe, find_binary(args.build_dir, probe))
    if args.target == "m33":
        measured["stack"] = {fmt: measure_stack(args.engine, args.build_dir, fmt) for fmt in FORMATS}

    if args.update:
        rec = {"size": {f: {k: {"recorded": v, "ceiling": ceiling(v, SIZE_GRAIN)} for k, v in s.items()}
                        for f, s in measured["size"].items()}}
        if "stack" in measured:
            rec["stack"] = {f: {c: {k: {"recorded": v, "ceiling": ceiling(v, STACK_GRAIN)} for k, v in fig.items()}
                                for c, fig in calls.items()} for f, calls in measured["stack"].items()}
        doc[args.target] = rec
        path.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
        print(f"updated {path} ({args.target})")
        return 0

    failures: list[str] = []
    for fmt, figs in measured["size"].items():
        for key, value in figs.items():
            gate(f"{args.target} {fmt} {key}", value, base.get("size", {}).get(fmt, {}).get(key), failures)
    for fmt, calls in measured.get("stack", {}).items():
        for call, figs in calls.items():
            for key, value in figs.items():
                rec = base.get("stack", {}).get(fmt, {}).get(call, {}).get(key)
                gate(f"{args.target} {fmt} {call} stack {key}", value, rec, failures)
    # Every recorded ceiling must have been measured: a probe that vanished
    # must not simply stop being gated.
    for fmt in base.get("size", {}):
        if fmt not in measured["size"]:
            failures.append(f"{args.target} {fmt} size recorded but not measured")
    if base.get("stack") and "stack" not in measured:
        failures.append(f"{args.target} stack recorded but not measured")
    for f in failures:
        print(f"FAILED: {f}")
    return 1 if failures else 0


def render(engine: str, doc: dict) -> str:
    rows = ["| target | format | engine `.text` (bytes) | ceiling | engine `.rodata` (bytes) | ceiling "
            "| image (bytes) | ceiling |", "|---|---|--:|--:|--:|--:|--:|--:|"]
    for target in ("m33", "m55", "hexagon"):
        for fmt, figs in doc.get(target, {}).get("size", {}).items():
            t, r, i = figs["engine_text"], figs["engine_rodata"], figs["image"]
            rows.append(f"| {target} | {fmt} | {t['recorded']:,} | {t['ceiling']:,} "
                        f"| {r['recorded']:,} | {r['ceiling']:,} | {i['recorded']:,} | {i['ceiling']:,} |")
    stack = doc.get("m33", {}).get("stack", {})
    if stack:
        rows += ["", "| M33 call | format | static worst path (bytes) | ceiling | measured high-water (bytes) "
                 "| ceiling |", "|---|---|--:|--:|--:|--:|"]
        for call in CALLS[engine]:
            for fmt, calls in stack.items():
                s, m = calls[call]["static"], calls[call]["measured"]
                rows.append(f"| `{call}` | {fmt} | {s['recorded']} | {s['ceiling']} "
                            f"| {m['recorded']} | {m['ceiling']} |")
    return "\n".join(rows)


def cmd_docs(args) -> int:
    doc = json.loads(pathlib.Path(f"{args.engine}/bench/ceilings.json").read_text())
    readme = pathlib.Path(f"{args.engine}/README.md")
    text = readme.read_text()
    if BEGIN not in text or END not in text:
        raise SystemExit(f"{readme}: no ceilings markers")
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    new = f"{head}{BEGIN}\n{render(args.engine, doc)}\n{END}{tail}"
    if args.check:
        if new != text:
            print(f"{readme}: the ceilings table is stale; run scripts/ceilings.py docs --engine {args.engine}")
            return 1
        return 0
    readme.write_text(new)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("measure")
    m.add_argument("--engine", required=True, choices=sorted(CALLS))
    m.add_argument("--target", required=True, choices=["m33", "m55", "hexagon"])
    m.add_argument("--build-dir", required=True)
    m.add_argument("--ceilings", help="default: <engine>/bench/ceilings.json")
    m.add_argument("--update", action="store_true")
    d = sub.add_parser("docs")
    d.add_argument("--engine", required=True, choices=sorted(CALLS))
    d.add_argument("--check", action="store_true")
    args = ap.parse_args()
    return cmd_measure(args) if args.cmd == "measure" else cmd_docs(args)


if __name__ == "__main__":
    sys.exit(main())
