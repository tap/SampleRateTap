#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""The migration gates, as the migration-gates workflow runs them.

Every check compares the gated tree ("new") against the two step-0 tips
(old-async = SampleRateTap@S0, old-ratio = RatioTap@R0) built in the SAME
job with the same toolchain (A/B gates), or against docs/migration/snapshot/
(snapshot gates). Which renames to expect comes from docs/migration/step.txt
through rename.py's map, so one script serves every gated step.

  gates.py host  --work DIR     G1 G4(C ABI) G5 G6 G7 G9 G10 G12 G14
  gates.py cross --work DIR --target m33|m55|hexagon
                                G3 G4(icount) G5(checksums) G7
  gates.py notebooks --work DIR G11

Trees default to ./new, ./old-async and ./old-ratio (the workflow's
checkout paths); override with --new/--old-async/--old-ratio. Each gate
prints PASS or FAIL lines; the command exits nonzero if any gate failed.
"""
import argparse
import bisect
import collections
import difflib
import fnmatch
import glob
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import collect  # noqa: E402
import rename  # noqa: E402

FAILS = []


def report(gate: str, ok: bool, detail: str = ""):
    print(f"{'PASS' if ok else 'FAIL'} {gate}{': ' + detail if detail else ''}", flush=True)
    if not ok:
        FAILS.append(gate)


def run(cmd, **kw):
    print("+ " + " ".join(map(str, cmd)), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def out(cmd, **kw) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def step() -> str:
    return (HERE / "step.txt").read_text().split()[0]


def at_least(s: str) -> bool:
    return rename.at_least(step(), s)


# -- configuration per tree ------------------------------------------------

def opts_new(kind: str) -> list[str]:
    """CMake options for the gated tree; the option names follow the step."""
    unified = at_least("3.4")
    if kind == "host":
        return (["-DTAP_SR_BUILD_CAPI=ON"] if unified
                else ["-DSRT_BUILD_CAPI=ON", "-DTAP_RATIO_BUILD_CAPI=ON"])
    if unified:
        return ["-DTAP_SR_BUILD_TESTS=OFF", "-DTAP_SR_BUILD_EXAMPLES=OFF",
                "-DTAP_SR_BUILD_ICOUNT_BENCH=ON"]
    return ["-DSRT_BUILD_TESTS=OFF", "-DSRT_BUILD_EXAMPLES=OFF",
            "-DTAP_RATIO_BUILD_TESTS=OFF", "-DTAP_RATIO_BUILD_EXAMPLES=OFF",
            "-DSRT_BUILD_ICOUNT_BENCH=ON", "-DTAP_RATIO_BUILD_ICOUNT_BENCH=ON"]


OPTS_OLD = {
    "host": {"async": ["-DSRT_BUILD_CAPI=ON"], "ratio": ["-DTAP_RATIO_BUILD_CAPI=ON"]},
    "cross": {"async": ["-DSRT_BUILD_TESTS=OFF", "-DSRT_BUILD_EXAMPLES=OFF", "-DSRT_BUILD_ICOUNT_BENCH=ON"],
              "ratio": ["-DTAP_RATIO_BUILD_TESTS=OFF", "-DTAP_RATIO_BUILD_EXAMPLES=OFF",
                        "-DTAP_RATIO_BUILD_ICOUNT_BENCH=ON"]},
}
TOOLCHAIN = {"m33": "cmake/arm-cortex-m33-mps2.cmake", "m55": "cmake/arm-cortex-m55-mps3.cmake",
             "hexagon": "cmake/hexagon-linux-musl.cmake"}


def configure_build(src, bld, opts, target=None, build_type="Release"):
    cmd = ["cmake", "-S", str(src), "-B", str(bld), f"-DCMAKE_BUILD_TYPE={build_type}",
           "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + opts
    if target:
        cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={pathlib.Path(src).resolve() / TOOLCHAIN[target]}")
    run(cmd, stdout=subprocess.DEVNULL)
    run(["cmake", "--build", str(bld), "-j", str(os.cpu_count() or 4)], stdout=subprocess.DEVNULL)


# -- name and path maps (from rename.py, at the current step) --------------

def map_old_path(repo: str, rel: str, is_file: bool = False) -> str | None:
    """Where a step-0 path lives in the gated tree. A file path maps as
    itself (so asrc.h -> converter.h applies); anything else may be a
    directory (an include dir in a flag), which arrives without the
    trailing slash the map's prefixes carry."""
    if repo == "ratio":
        # RatioTap's test-only copy of SampleRateTap is the async engine here.
        if rel.startswith("submodules/sampleratetap/"):
            return map_old_path("async", rel[len("submodules/sampleratetap/"):], is_file)
        if rel.startswith("submodules/dsptap"):
            return rel
        if is_file:
            return rename.map_r0_path(rel, step())
        mapped = rename.map_r0_path(rel + "/", step())
        return mapped.rstrip("/") if mapped is not None else None
    if rel.startswith("submodules/"):
        return rel
    if is_file:
        return rename.map_s0_path(rel, step())
    mapped = rename.map_s0_path(rel + "/", step())
    if mapped is not None and mapped.rstrip("/") != rel:
        return mapped.rstrip("/")
    return rename.map_s0_path(rel, step())


def map_text(text: str, path: str = "x.cpp") -> str:
    return rename.apply_subs(text, path, step())


# -- G7: compile and link flags --------------------------------------------

def load_flags(bld: pathlib.Path, src: pathlib.Path, repo: str | None):
    """{mapped source path: ordered, normalized tokens}. repo None = new tree."""
    src_s, bld_s = str(src.resolve()), str(bld.resolve())
    res = {}
    for e in json.load(open(bld / "compile_commands.json")):
        toks = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
        norm, skip = [], False
        for t in toks[1:]:
            if skip:
                skip = False
                continue
            if t in ("-o", "-c", "-MF", "-MT", "-MQ"):
                skip = True
                continue
            if t == "-MD" or t.startswith("-o"):
                continue
            norm.append(norm_token(t, src_s, bld_s, repo))
        f = norm_token(e["file"], src_s, bld_s, repo)
        if "<DEPS>" in f:
            continue
        # CMake emits a target's definitions sorted by name, so a renamed
        # macro moves in the command line (3.3: SRT_SC_* past
        # TAP_DSP_FFT_CMSIS). Compared as the sorted set CMake makes them;
        # every other flag keeps its order.
        defs = sorted(t for t in norm if t.startswith("-D"))
        res[f] = [t for t in norm if not t.startswith("-D")] + defs
    return res


def norm_token(t: str, src: str, bld: str, repo: str | None) -> str:
    # FetchContent's _deps directory moves with the build layout.
    t = re.sub(re.escape(bld) + r"(/[^ ]*?)?/_deps/", "<DEPS>/", t)
    t = t.replace(bld, "<BLD>")

    def path_map(m):
        rel = m.group(1)
        if repo is not None:
            is_file = pathlib.PurePosixPath(rel).suffix in (".c", ".cc", ".cpp", ".h", ".hpp", ".ld", ".cmake")
            mapped = map_old_path(repo, rel, is_file) if rel else rel
            rel = mapped if mapped is not None else "<DROPPED>/" + rel
        return "<SRC>/" + rel if rel else "<SRC>"
    t = re.sub(re.escape(src) + r"/?([^\s\"']*)", path_map, t)
    if repo is not None:
        t = map_text(t, "x.cmake") if t.startswith("-D") else t
    # Build-tree paths differ by the engine's subdirectory in the root tree.
    t = re.sub(r"<BLD>/(async|bridge)/", "<BLD>/", t)
    return t


def g7(old: dict, new: dict, label: str):
    bad = []
    for f, toks in sorted(old.items()):
        if f not in new:
            bad.append(f"{f}: missing from the gated tree")
        elif new[f] != toks:
            d = [l for l in difflib.unified_diff(toks, new[f], lineterm="", n=0)
                 if l[:1] in "+-" and not l.startswith(("+++", "---"))]
            bad.append(f"{f}: {' '.join(d)[:300]}")
    for b in bad[:20]:
        print("   ", b)
    report(f"G7 {label}", not bad, f"{len(old)} TUs compared, {len(bad)} differ")


# -- G4: per-function disassembly ------------------------------------------

def disasm(elf: str, objdump: str, nm: str, mapper) -> dict:
    """Normalized instruction lists keyed by mapped demangled function name
    (the MONOREPO_PLAN G4 normalizer). Stripped: addresses, call/branch
    targets' addresses (their symbol names stay), RIP displacements and
    disassembler comments. A literal-pool word becomes a string literal's
    text if it points at one, the symbol it points INTO (by nm -S extent)
    if any, or else the name of the section it points into; other words
    stay raw constants. The data layout may move when a string's length
    changes (D15's exception prefixes); the code must not."""
    from elftools.elf.elffile import ELFFile
    syms = []
    for line in out([nm, "-C", "-n", "-S", elf]).splitlines():
        m = re.match(r"^([0-9a-f]+)(?: ([0-9a-f]+))? (\S) (.*)$", line)
        if m and m.group(3) not in "aUwNv":
            size = int(m.group(2), 16) if m.group(2) else 0
            syms.append((int(m.group(1), 16), size, mapper(m.group(4))))
    syms.sort()
    addrs = [a for a, _, _ in syms]
    secs, funcs_at = [], []
    with open(elf, "rb") as fh:
        ef = ELFFile(fh)
        symtab = ef.get_section_by_name(".symtab")
        for sy in (symtab.iter_symbols() if symtab else []):
            if sy["st_info"]["type"] == "STT_FUNC" and sy["st_size"] > 0:
                start = sy["st_value"] & ~1  # Thumb bit
                funcs_at.append((start, start + sy["st_size"]))
        for sec in ef.iter_sections():
            if sec["sh_addr"] and sec["sh_type"] in ("SHT_PROGBITS", "SHT_NOBITS", "SHT_INIT_ARRAY", "SHT_FINI_ARRAY"):
                data = sec.data() if sec["sh_type"] != "SHT_NOBITS" else b""
                secs.append((sec["sh_addr"], sec["sh_size"], sec.name, data, bool(sec["sh_flags"] & 1)))  # SHF_WRITE

    def section(v):
        for a, size, name, data, writable in secs:
            if a <= v < a + size:
                return name, a, data, writable
        return None

    def symbolize(v, strict=False):
        """strict: the value may be a plain constant (an absolute ##imm on
        Hexagon, where the static musl link reaches every read-only datum
        PC-relatively), so it is symbolized only into writable data; anything
        else stays raw, and a raw value that moves is a visible diff, never a
        hidden one."""
        sec = section(v)
        if strict and not (sec is not None and sec[3]):
            return f"{v:#x}"
        i = bisect.bisect_right(addrs, v) - 1
        if i >= 0:
            a, size, n = syms[i]
            if v == a or v < a + size:
                return f"<{n}+{v - a:#x}>"
        if sec is None:
            return f"{v:#x}"
        name, a, data, _ = sec
        o = v - a
        e = data.find(b"\0", o) if o < len(data) else -1
        # Only from a string's first byte: a numeric constant that lands
        # inside the string pool must not read as the string's tail.
        at_start = o == 0 or (o < len(data) and data[o - 1] == 0)
        if at_start and 0 <= e - o <= 400 and e > o:
            b = data[o:e]
            if all(32 <= c < 127 or c in (9, 10) for c in b):
                # Mapped as a C string literal, quotes included: some rules
                # (D15's exception prefix) anchor on the opening quote.
                return repr(mapper('"' + b.decode() + '"')[1:-1])
        return f"<data in {name}>"

    # Only bytes inside an STT_FUNC's extent are code. Bare-metal links put
    # read-only data (string pools, typeinfo, vtables) in .text too, and
    # objdump decodes it as instructions; its bytes move whenever a string
    # changes length, which G5's checksums and G3's counts already cover.
    funcs_at.sort()
    starts = [a for a, _ in funcs_at]

    def in_code(addr):
        i = bisect.bisect_right(starts, addr) - 1
        return i >= 0 and addr < funcs_at[i][1]

    listing = out([objdump, "-d", "--no-show-raw-insn", "-C", elf])
    hexagon = "elf32-hexagon" in listing[:400]
    funcs, cur, packet = {}, None, 0
    for line in listing.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.*)>:$", line)
        if m:
            cur = mapper(m.group(1))
            funcs.setdefault(cur, [])
            continue
        m = re.match(r"^\s*([0-9a-f]+):\s+(.*)$", line)
        if not m or cur is None or not in_code(int(m.group(1), 16)):
            continue
        addr, ins = int(m.group(1), 16), mapper(m.group(2))
        # Target addresses first: a PLT name like <new(...)@plt> contains '@'.
        ins = re.sub(r"\b[0-9a-f]+ <([^>]*)>", r"<\1>", ins)
        ins = re.sub(r"\s+#\s.*$", "", ins)
        ins = re.sub(r"\s+[@;]\s.*$", "", ins)
        ins = re.sub(r"-?0x[0-9a-f]+\(%rip\)", "REL(%rip)", ins)
        w = re.match(r"^\.word\s+0x([0-9a-f]+)$", ins)
        if w:
            ins = ".word " + symbolize(int(w.group(1), 16))
        # Hexagon: "pc" in add(pc,##imm) is the packet's address; the packet
        # opens with "{". The immext that carries an immediate's upper bits
        # is redundant with the extended instruction, which objdump prints
        # with the full value, so its own value is dropped. An absolute ##imm
        # is a constant unless it points into writable data (see symbolize).
        if not hexagon:
            funcs[cur].append(ins)
            continue
        if ins.lstrip().startswith("{"):
            packet = addr
        # Branch and call targets are bare addresses (no <symbol>); every
        # immediate carries a '#', so a bare 0x... is an address.
        ins = re.sub(r"(?<![#\w])0x([0-9a-f]+)\b", lambda mm: symbolize(int(mm.group(1), 16)), ins)
        ins = re.sub(r"add\(pc,##(0x[0-9a-f]+)\)",
                     lambda mm: "add(pc," + symbolize((packet + int(mm.group(1), 16)) & 0xffffffff) + ")", ins)
        ins = re.sub(r"immext\(#0x[0-9a-f]+\)", "immext(#EXT)", ins)
        ins = re.sub(r"##(0x[0-9a-f]+)\b", lambda mm: "##" + symbolize(int(mm.group(1), 16), strict=True), ins)
        funcs[cur].append(ins)
    return {k: v for k, v in funcs.items() if v}


def demangled_mapper(s: str) -> str:
    return map_text(s)


def g4_allowances(label: str) -> dict[str, set[str]]:
    """allow-g4.txt rows for one G4 label: pair name -> functions that must
    differ (a row whose function is identical is stale and fails)."""
    allowed = collections.defaultdict(set)
    for line in (HERE / "allow-g4.txt").read_text().splitlines():
        line = line.split("--")[0].strip()
        if not line or line.startswith("#"):
            continue
        lbl, pair, func = line.split(maxsplit=2)
        if lbl == label.replace(" ", "-"):
            allowed[pair].add(func)
    return allowed


def g4(pairs, objdump, nm, label):
    bad, shown = [], 0
    allowed = g4_allowances(label)
    for name, old_elf, new_elf in pairs:
        a = disasm(old_elf, objdump, nm, demangled_mapper)
        b = disasm(new_elf, objdump, nm, lambda s: s)
        diffs = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
        extra = [k for k in diffs if k not in allowed.get(name, set())]
        stale = sorted(allowed.get(name, set()) - set(diffs))
        if extra:
            bad.append(f"{name}: {len(extra)} function(s) differ, e.g. {extra[:3]}")
        if stale:
            bad.append(f"{name}: allow-g4.txt names identical function(s) {stale}")
        for k in diffs:
            if k in allowed.get(name, set()):
                shown += 1
                print(f"    {name}: {k} differs as allowed:")
                for line in difflib.unified_diff(a.get(k, []), b.get(k, []), "step-0", "gated", lineterm="", n=0):
                    print("       ", line)
    for b in bad[:20]:
        print("   ", b)
    report(f"G4 {label}", not bad, f"{len(pairs)} binaries compared"
           + (f", {shown} function(s) at their allow-g4.txt diff" if shown else ""))


# -- host ------------------------------------------------------------------

def hash_lines(bld, regex):
    log = subprocess.run(["ctest", "--test-dir", str(bld), "-R", regex, "-V"],
                         capture_output=True, text=True).stdout
    return sorted(set(re.findall(r"\[ measured \] hash .*", log)))


def host(args):
    w = pathlib.Path(args.work).resolve()
    new, oa, orat = map(lambda p: pathlib.Path(p).resolve(), (args.new, args.old_async, args.old_ratio))
    configure_build(oa, w / "old-async", OPTS_OLD["host"]["async"])
    configure_build(orat, w / "old-ratio", OPTS_OLD["host"]["ratio"])
    configure_build(new, w / "new", opts_new("host"))

    # G7
    new_flags = load_flags(w / "new", new, None)
    for repo, tree, bld in (("async", oa, w / "old-async"), ("ratio", orat, w / "old-ratio")):
        g7(load_flags(bld, tree, repo), new_flags, f"host {repo}")

    # G1: registered tests and labels, against the snapshot (+ allow.txt)
    rows = out(["ctest", "--test-dir", str(w / "new"), "--show-only=json-v1"])
    tests = json.loads(rows)["tests"]
    report("G1 non-empty", bool(tests), f"{len(tests)} tests")
    got = collections.defaultdict(set)
    for t in tests:
        labels = next((p["value"] for p in t.get("properties", []) if p["name"] == "LABELS"), [])
        # A test carrying both engine labels (the family's D13 check, 3.4)
        # runs under either engine's selection, so it belongs to both.
        for lab in labels or [""]:
            got[lab].add(f"{t['name']}\t{','.join(sorted(labels))}")
    allow = [l.split("--")[0].split() for l in (HERE / "allow.txt").read_text().splitlines()
             if l.strip() and not l.startswith("#")]
    for engine, label in (("async", "async"), ("bridge", "bridge" if at_least("3.4") else "ratio")):
        want = set()
        for line in (HERE / "snapshot" / "g1" / f"{engine}-labels.txt").read_text().splitlines():
            name, lab = line.split("\t")
            if at_least("3.4"):
                name = collect.mapped(name, True)
                lab = "bridge" if lab == "ratio" else lab
            want.add(f"{name}\t{lab}")
        have = got.get(label, set())
        added = {a[2] for a in allow
                 if len(a) >= 3 and fnmatch.fnmatch(f"{engine}-labels.txt", a[0]) and a[1] == "+"}
        missing = sorted(want - have)
        extra = sorted(n for n in have - want if n.split("\t")[0] not in added)
        for m in missing[:10]:
            print("    missing:", m)
        for m in extra[:10]:
            print("    unlisted new row:", m)
        report(f"G1 {engine}", not missing and not extra, f"{len(have)} tests")

    # G5: output hashes, A vs B in this job
    async_re = "async.OutputHash"
    bridge_re = ("bridge." if at_least("3.4") else "ratio.") + "OutputHash"
    for label, old_b, old_re, new_re in (("async", w / "old-async", "OutputHash", async_re),
                                         ("bridge", w / "old-ratio", "OutputHash", bridge_re)):
        a, b = hash_lines(old_b, old_re), hash_lines(w / "new", new_re)
        report(f"G5 host {label}", bool(a) and a == b, f"{len(a)} hashes")

    # G6: cross-validation lines against the snapshot
    xlog = subprocess.run(["ctest", "--test-dir", str(w / "new"), "-R", "CrossValidation", "-V"],
                          capture_output=True, text=True).stdout
    got6 = sorted({m.group(0) for m in re.finditer(r"\[ measured \] cross-validation [^\n]*", xlog)})
    want6 = (HERE / "snapshot" / "g6.txt").read_text().splitlines()
    report("G6", got6 == want6, f"{len(got6)} lines")

    # G10 and G4 for the C ABI libraries
    pairs = []
    for engine, old_glob, new_glob in (("async", "old-async/**/libsrt_capi.so", "new/**/lib*async*capi*.so" if at_least("3.5") else "new/**/libsrt_capi.so"),
                                       ("bridge", "old-ratio/**/libratio_capi.so", "new/**/lib*bridge*capi*.so" if at_least("3.5") else "new/**/libratio_capi.so")):
        olds = glob.glob(str(w / old_glob), recursive=True)
        news = glob.glob(str(w / new_glob), recursive=True)
        if len(olds) != 1 or len(news) != 1:
            report(f"G10 {engine}", False, f"libraries found: old {olds} new {news}")
            continue
        want = [map_text(l) for l in (HERE / "snapshot" / "g10" / f"{engine}.txt").read_text().splitlines()]
        have = out(["python3", str(HERE / "collect.py"), "symbols", news[0]]).splitlines()
        if at_least("3.5"):
            want += [l for l in have if re.search(r"_version$", l) and l not in want]  # D13
        report(f"G10 {engine}", sorted(have) == sorted(want), f"{len(have)} symbols")
        pairs.append((engine, olds[0], news[0]))
    g4(pairs, "objdump", "nm", "host C ABI")

    # G14: the rename-only residual
    r = subprocess.run(["python3", str(HERE / "rename.py"), "check", "--through", step(),
                        "--tree", str(new), "--s0-repo", str(oa), "--r0-repo", str(orat)],
                       capture_output=True, text=True)
    print(r.stdout[-6000:], r.stderr[-2000:])
    report("G14", r.returncode == 0, r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "no output")

    # G9: retired identifiers, from step 3.7
    if at_least("3.7"):
        r = subprocess.run(["python3", str(HERE / "collect.py"), "retired", str(new)],
                           capture_output=True, text=True)
        print(r.stdout[-4000:])
        report("G9", r.returncode == 0, r.stdout.strip().splitlines()[-1])
    else:
        print(f"SKIP G9 (applies from step 3.7; this is {step()})")

    # G12: history of a fixed file list (needs a full clone of the gated tree)
    g12(new)


def log_key(entry: str) -> str:
    """'<ISO author date> <subject>' with the date as epoch seconds: git
    2.55 prints UTC in %aI as 'Z' where 2.43 prints '+00:00'."""
    import datetime
    date, _, subject = entry.partition(" ")
    return f"{int(datetime.datetime.fromisoformat(date).timestamp())} {subject}"


def g12(new: pathlib.Path):
    if out(["git", "-C", str(new), "rev-parse", "--is-shallow-repository"]).strip() == "true":
        report("G12", False, "the gated tree is a shallow clone; G12 needs full history")
        return
    tips = rename.read_tips()
    for engine, repo, tip_key in (("async", "async", "S0"), ("bridge", "ratio", "R0")):
        text = (HERE / "snapshot" / "g12" / f"{engine}.txt").read_text()
        for block in text.split("== ")[1:]:
            lines = block.splitlines()
            old_path = lines[0].split()[0]
            new_path = map_old_path(repo, old_path, is_file=True)
            want_log = [log_key(l[6:]) for l in lines if l.startswith("log   ")]
            want_blame = {}
            for l in lines:
                if l.startswith("blame "):
                    n, rest = l[6:].strip().split("  ", 1)
                    want_blame[rest] = int(n)
            r = subprocess.run(["python3", str(HERE / "collect.py"), "history", str(new), "HEAD", new_path],
                               capture_output=True, text=True)
            if r.returncode:
                report(f"G12 {new_path}", False, r.stderr.strip()[-200:])
                continue
            got_lines = r.stdout.splitlines()
            got_log = [log_key(l[6:]) for l in got_lines if l.startswith("log   ")]
            got_blame = {}
            for l in got_lines:
                if l.startswith("blame "):
                    n, rest = l[6:].strip().split("  ", 1)
                    got_blame[rest] = int(n)
            # --follow must still reach every step-0 commit, in order.
            it = iter(got_log)
            follows = all(any(g == w_ for g in it) for w_ in want_log)
            if not follows:
                unreached = [w_ for w_ in want_log if w_ not in got_log]
                print(f"    not reached ({len(unreached)}): {unreached[:3]!r}")
                print(f"    gated log head: {got_log[:4]!r}")
            total = sum(got_blame.values()) or 1
            new_owned = sum(n for k, n in got_blame.items() if k not in want_blame)
            wholesale = new_owned > total / 2
            report(f"G12 {new_path}", follows and not wholesale,
                   f"{len(want_log)}/{len(got_log)} commits reached; "
                   f"{new_owned}/{total} lines owned by post-step-0 commits")


# -- cross -------------------------------------------------------------------

def build_plugin(src_c: pathlib.Path, dst: pathlib.Path, header_dir: str):
    cflags = out(["pkg-config", "--cflags", "glib-2.0"]).split()
    run(["gcc", "-shared", "-fPIC", *cflags, f"-I{header_dir}", "-o", str(dst), str(src_c)])


def g3_allowances(target: str, engine: str) -> dict[str, tuple[str, int]]:
    """allow-g3.txt rows for one target and engine: workload -> (symbol, delta)."""
    allowed = {}
    for line in (HERE / "allow-g3.txt").read_text().splitlines():
        line = line.split("--")[0].strip()
        if not line or line.startswith("#"):
            continue
        tgt, eng, workload, symbol, delta = line.split()
        if tgt == target and eng == engine:
            if int(delta) == 0 or workload in allowed:
                raise SystemExit(f"allow-g3.txt: bad row for {workload}")
            allowed[workload] = (symbol, int(delta))
    return allowed


def fn_counts(target: str, plugin: pathlib.Path, binary: str, nm: str, work: pathlib.Path) -> list[tuple[str, int]]:
    """(symbol, executed instructions) per text symbol, via fncount.c. Runs
    the binary exactly as icount.py does (fixed path, argv[0], empty
    environment), so the total is the count G3 measured."""
    symfile = work / (pathlib.Path(binary).name + ".syms")
    rows = []
    for line in out([nm, "-n", "-S", "--defined-only", binary]).splitlines():
        m = re.match(r"^([0-9a-f]+) ([0-9a-f]+) [tTwW] (.*)$", line)
        if m:
            rows.append(f"{m.group(1)} {m.group(2)} {m.group(3)}")
    symfile.write_text("\n".join(rows) + "\n")
    if target != "hexagon":
        raise SystemExit("fn_counts: only the Hexagon user-mode leg is supported")
    run_dir = pathlib.Path("/tmp/tap-icount")
    run_dir.mkdir(exist_ok=True)
    fixed = run_dir / "w"
    shutil.copyfile(binary, fixed)
    fixed.chmod(0o755)
    qemu = shutil.which("qemu-hexagon")
    proc = subprocess.run([qemu, "-0", "w", "-d", "plugin", "-plugin", f"{plugin},symfile={symfile}", str(fixed)],
                          cwd=run_dir, env={}, capture_output=True, text=True, timeout=1200)
    counts = [(m.group(2), int(m.group(1))) for m in re.finditer(r"^FN (\d+) (.*)$", proc.stdout + proc.stderr, re.M)]
    if not counts:
        print(proc.stdout[-2000:], proc.stderr[-2000:])
        raise SystemExit(f"{binary}: fncount produced no rows")
    return counts


def g3_prove(target, work, plugin, workload, old_bin, new_bin, symbol, delta, nm) -> bool:
    """The allow-g3.txt row for one workload, re-proved: every symbol but the
    named one executes the same instruction count in both trees (as a
    multiset, so renamed functions need no name map), and the named symbol
    differs by exactly the row's delta."""
    old_c = fn_counts(target, plugin, old_bin, nm, work)
    new_c = fn_counts(target, plugin, new_bin, nm, work)
    old_sym = sum(c for n, c in old_c if n == symbol)
    new_sym = sum(c for n, c in new_c if n == symbol)
    old_rest = collections.Counter(c for n, c in old_c if n != symbol)
    new_rest = collections.Counter(c for n, c in new_c if n != symbol)
    ok = new_sym - old_sym == delta and old_rest == new_rest
    if new_sym - old_sym != delta:
        print(f"    {workload}: {symbol} {new_sym - old_sym:+d} insns, row says {delta:+d}")
    if old_rest != new_rest:
        gone = sorted((c, n) for n, c in old_c if n != symbol and (old_rest - new_rest)[c])[:5]
        came = sorted((c, n) for n, c in new_c if n != symbol and (new_rest - old_rest)[c])[:5]
        print(f"    {workload}: other functions moved, e.g. step 0 {gone} vs gated {came}")
    if ok:
        print(f"    {workload}: {symbol} {delta:+d}; {sum(old_rest.values())} other symbols identical per function")
    return ok


def cross(args):
    t = args.target
    w = pathlib.Path(args.work).resolve() / t
    w.mkdir(parents=True, exist_ok=True)
    new, oa, orat = map(lambda p: pathlib.Path(p).resolve(), (args.new, args.old_async, args.old_ratio))
    configure_build(oa, w / "old-async", OPTS_OLD["cross"]["async"], t)
    configure_build(orat, w / "old-ratio", OPTS_OLD["cross"]["ratio"], t)
    configure_build(new, w / "new", opts_new("cross"), t)

    new_flags = load_flags(w / "new", new, None)
    for repo, tree, bld in (("async", oa, w / "old-async"), ("ratio", orat, w / "old-ratio")):
        g7(load_flags(bld, tree, repo), new_flags, f"{t} {repo}")

    # G3 (+ G5 checksums): each tree measured with its own harness, then
    # the gated tree compared EXACTLY against the step-0 measurements.
    plugins = {}
    for key, c in (("old-async", oa / "tools/qemu_insn_plugin/insn_count.c"),
                   ("old-ratio", orat / "tools/qemu_insn_plugin/insn_count.c"),
                   ("new-async", new / "tools/qemu_insn_plugin/insn_count.c"),
                   ("new-bridge", new / "bridge/tools/qemu_insn_plugin/insn_count.c")):
        if not c.exists():
            c = new / "tools/qemu_insn_plugin/insn_count.c"
        plugins[key] = w / f"lib{key}.so"
        build_plugin(c, plugins[key], args.plugin_header_dir)

    def icount(tree_script, build, plugin, extra):
        cmd = ["python3", str(tree_script), "--target", t, "--build-dir", str(build),
               "--plugin", str(plugin)] + extra
        print("+ " + " ".join(cmd), flush=True)
        return subprocess.run(cmd, capture_output=True, text=True)

    if t == "hexagon":
        bindir = pathlib.Path(shutil.which("hexagon-unknown-linux-musl-clang++")).parent
        objdump = str(bindir / "llvm-objdump") if (bindir / "llvm-objdump").exists() else "llvm-objdump"
        nm = str(bindir / "llvm-nm") if (bindir / "llvm-nm").exists() else "llvm-nm"
    else:
        objdump, nm = "arm-none-eabi-objdump", "arm-none-eabi-nm"
    plugins["fncount"] = w / "libfncount.so"
    build_plugin(HERE / "fncount.c", plugins["fncount"], args.plugin_header_dir)

    prefixes = {"async": ("srt_icount_", "tap_sr_async_icount_" if at_least("3.6") else "srt_icount_"),
                "bridge": ("ratio_icount_", "tap_sr_bridge_icount_" if at_least("3.6") else "ratio_icount_")}
    for label, old_tree, old_bld, old_plugin, new_script, new_extra, new_plugin in (
            ("async", oa, w / "old-async", plugins["old-async"],
             new / "scripts/icount.py", ["--engine", "async"] if not (new / "bridge/scripts/icount.py").exists() and at_least("2") else [],
             plugins["new-async"]),
            ("bridge", orat, w / "old-ratio", plugins["old-ratio"],
             (new / "bridge/scripts/icount.py") if (new / "bridge/scripts/icount.py").exists() else (new / "scripts/icount.py"),
             [] if (new / "bridge/scripts/icount.py").exists() else ["--engine", "bridge"],
             plugins["new-bridge"])):
        old_prefix, new_prefix = prefixes[label]
        ref = w / f"{label}-old.json"
        ref.unlink(missing_ok=True)
        r = icount(old_tree / "scripts/icount.py", old_bld, old_plugin,
                   ["--baselines", str(old_tree / "bench/baselines.json"), "--tolerance", "1e9",
                    "--json-out", str(ref)])
        print(r.stdout[-3000:], r.stderr[-2000:])
        if not ref.exists():
            report(f"G3 {t} {label}", False, "step-0 measurement failed")
            continue
        got = w / f"{label}-new.json"
        got.unlink(missing_ok=True)
        r = icount(new_script, w / "new", new_plugin,
                   new_extra + ["--baselines", str(new / label / "bench/baselines.json"),
                                "--tolerance", "1e9", "--json-out", str(got)])
        print(r.stdout[-3000:], r.stderr[-2000:])
        if not got.exists():
            report(f"G3+G5 {t} {label}", False, "gated-tree measurement failed")
            continue
        want_m = json.loads(ref.read_text()).get(t, {})
        got_m = json.loads(got.read_text()).get(t, {})
        allowed = g3_allowances(t, label)
        ok = bool(want_m) and set(want_m) == set(got_m)
        if not ok:
            print(f"    workload sets differ: -{sorted(set(want_m) - set(got_m))} +{sorted(set(got_m) - set(want_m))}")
        for k in sorted(set(allowed) - set(want_m)):
            print(f"    {k}: allow-g3.txt row names no measured workload")
            ok = False
        proved = 0
        for k in sorted(set(want_m) & set(got_m)):
            delta = got_m[k]["insns"] - want_m[k]["insns"]
            symbol, want_delta = allowed.get(k, (None, 0))
            if got_m[k]["checksum"] != want_m[k]["checksum"]:
                print(f"    {k}: checksum {got_m[k]['checksum']} vs {want_m[k]['checksum']} MISMATCH")
                ok = False
            if delta != want_delta:
                print(f"    {k}: {delta:+d} insns vs step 0, allowed {want_delta:+d} MISMATCH")
                ok = False
            elif symbol is not None:
                old_bin = glob.glob(str(old_bld / "**" / (old_prefix + k)), recursive=True)
                new_bin = glob.glob(str(w / "new" / "**" / (new_prefix + k)), recursive=True)
                if len(old_bin) == 1 and len(new_bin) == 1 and \
                        g3_prove(t, w, plugins["fncount"], k, old_bin[0], new_bin[0], symbol, want_delta, nm):
                    proved += 1
                else:
                    ok = False
        report(f"G3+G5 {t} {label}", ok,
               f"{len(want_m)} workloads, checksums exact, counts exact"
               + (f" ({proved} at their allow-g3.txt delta, proved per function)" if allowed else ""))

    # G4: icount binaries, paired by workload name.
    pairs = []
    for old_bld, (prefix, new_prefix) in ((w / "old-async", prefixes["async"]),
                                          (w / "old-ratio", prefixes["bridge"])):
        for f in sorted(glob.glob(str(old_bld / "**" / (prefix + "*")), recursive=True)):
            if not (os.path.isfile(f) and os.access(f, os.X_OK)):
                continue
            wl = os.path.basename(f)[len(prefix):]
            cand = glob.glob(str(w / "new" / "**" / (new_prefix + wl)), recursive=True)
            if len(cand) != 1:
                report(f"G4 {t} {wl}", False, f"gated-tree binary not found ({cand})")
                continue
            pairs.append((wl, f, cand[0]))
    g4(pairs, objdump, nm, f"{t} icount")


# -- notebooks (G11) ---------------------------------------------------------

# Timing cells are tagged "nondeterministic" in the notebooks themselves
# (step P.3); these catch IPython's own timing magics.
VOLATILE = [re.compile(p) for p in (r"^CPU times:", r"^Wall time:")]


def notebook_text(path: pathlib.Path, mapper) -> list[str]:
    nb = json.loads(path.read_text())
    lines = []
    for i, cell in enumerate(nb.get("cells", [])):
        if cell.get("cell_type") != "code":
            continue
        if "nondeterministic" in cell.get("metadata", {}).get("tags", []):
            lines.append(f"[cell {i}: nondeterministic, skipped]")
            continue
        # The kernel may split one print across stream chunks (a bare "\n"
        # chunk after its line was seen once); consecutive chunks of one
        # stream are one text, as the notebook renders them.
        merged = []
        for o in cell.get("outputs", []):
            if o.get("output_type") == "stream" and merged and merged[-1].get("output_type") == "stream" \
                    and merged[-1].get("name") == o.get("name"):
                merged[-1] = {"output_type": "stream", "name": o.get("name"),
                              "text": "".join(merged[-1].get("text", "")) + "".join(o.get("text", ""))}
            else:
                merged.append(o)
        for o in merged:
            if o.get("output_type") == "stream":
                text = "".join(o.get("text", ""))
            elif "data" in o and "text/plain" in o["data"]:
                text = "".join(o["data"]["text/plain"])
            elif o.get("output_type") == "error":
                text = f"ERROR {o.get('ename')}: {o.get('evalue')}"
            else:
                continue
            for line in text.splitlines():
                if re.match(r"^<Figure size", line) or any(v.search(line) for v in VOLATILE):
                    continue
                lines.append(f"[cell {i}] {mapper(line)}")
    return lines


def notebooks(args):
    w = pathlib.Path(args.work).resolve() / "notebooks"
    w.mkdir(parents=True, exist_ok=True)
    new, oa, orat = map(lambda p: pathlib.Path(p).resolve(), (args.new, args.old_async, args.old_ratio))
    pairs = []
    for old_tree, repo in ((oa, "async"), (orat, "ratio")):
        for nb in sorted((old_tree / "notebooks").glob("*.ipynb")):
            new_rel = map_old_path(repo, str(nb.relative_to(old_tree)), is_file=True)
            pairs.append((repo, nb, new / new_rel))
    for repo, old_nb, new_nb in pairs:
        results = []
        for side, nb in (("old", old_nb), ("new", new_nb)):
            dst = w / f"{side}-{repo}-{nb.name}"
            r = subprocess.run(["jupyter", "nbconvert", "--to", "notebook", "--execute",
                                "--ExecutePreprocessor.timeout=1800", "--output", str(dst), str(nb)],
                               cwd=nb.parent, capture_output=True, text=True)
            if r.returncode:
                print(r.stderr[-3000:])
                results.append(None)
            else:
                results.append(dst)
        if None in results:
            report(f"G11 {new_nb.name}", False, "execution failed")
            continue
        roots = [(str(oa), "<ROOT>"), (str(orat), "<ROOT>"), (str(new / "async"), "<ROOT>"),
                 (str(new / "bridge"), "<ROOT>"), (str(new), "<ROOT>")]

        def unroot(line):
            for a_, b_ in roots:
                line = line.replace(a_, b_)
            return line
        a = notebook_text(results[0], lambda s: map_text(unroot(s)))
        b = notebook_text(results[1], unroot)
        d = list(difflib.unified_diff(a, b, "step-0", "gated", lineterm="", n=1))
        for line in d[:40]:
            print("   ", line)
        report(f"G11 {new_nb.name}", not d, f"{len(a)} output lines")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("host", host), ("cross", cross), ("notebooks", notebooks)):
        p = sub.add_parser(name)
        p.add_argument("--work", required=True)
        p.add_argument("--new", default="new")
        p.add_argument("--old-async", default="old-async")
        p.add_argument("--old-ratio", default="old-ratio")
        if name == "cross":
            p.add_argument("--target", required=True, choices=["m33", "m55", "hexagon"])
            p.add_argument("--plugin-header-dir", default="/tmp")
        p.set_defaults(fn=fn)
    args = ap.parse_args()
    print(f"migration gates, step {step()}")
    args.fn(args)
    print(f"\n{len(FAILS)} gate(s) failed: {', '.join(FAILS)}" if FAILS else "\nall gates passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
