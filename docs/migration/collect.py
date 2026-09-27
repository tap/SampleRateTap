#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Collectors for the migration's snapshot gates (MONOREPO_PLAN.md section 5).

Each subcommand reads one kind of evidence and prints it in the canonical,
sorted text form that snapshot/ stores, so a gate is `collect.py ... |
diff - snapshot/<file>` after the name map. Nothing here depends on the
toolchain; the A/B gates live in the migration-gates workflow.

  ctest-log FILE           G1: tests ctest ran, from a CI job log or a
                           --output-log file ("Test #N: name" / "Start N: name")
  ctest-json BUILD_DIR     G1: registered tests and their labels
                           (ctest --show-only=json-v1); fails if empty
  gtest-runs FILE          G2: "[ RUN ]" test names in an on-target log,
                           with ctest's "N: " line prefix stripped
  xval FILE                G6: the cross-validation lines, limits included
  symbols LIB              G10: exported C ABI symbols (nm -D --defined-only,
                           unmangled only)
  history REPO REV PATH... G12: per file, the commits `git log --follow`
                           reaches and the lines blame attributes to each
  retired [TREE]           G9: retired identifiers outside snapshot/g9.txt's
                           allowlist (applies from step 3.7)

--map applies rename.py's test-name map (ratio. -> bridge., D16) so a
step-0 snapshot compares directly against a post-3.4 tree.
"""
import argparse
import collections
import fnmatch
import json
import re
import subprocess
import sys

TEST_NAME_MAP = [(re.compile(r"^ratio\."), "bridge.")]
TIMESTAMP = re.compile(r"^\d{4}-\d\d-\d\dT[\d:.]+Z ")
CTEST_PREFIX = re.compile(r"^\d+: ")


def clean(line: str) -> str:
    # GitHub job logs prefix a timestamp; ctest -V prefixes "N: ".
    return CTEST_PREFIX.sub("", TIMESTAMP.sub("", line.rstrip("\n")))


def mapped(name: str, use_map: bool) -> str:
    if use_map:
        for pat, rep in TEST_NAME_MAP:
            name = pat.sub(rep, name)
    return name


def emit_multiset(names, use_map):
    counts = collections.Counter(mapped(n, use_map) for n in names)
    if not counts:
        sys.exit("no entries found: an empty list must never pass as a snapshot")
    for name in sorted(counts):
        print(name if counts[name] == 1 else f"{name}\t×{counts[name]}")


def ctest_log(args):
    names = []
    for line in open(args.file, encoding="utf-8", errors="replace"):
        line = clean(line)
        m = re.match(r"^\s*\d+/\d+ Test +#\d+: (\S+) ", line)
        if m:
            names.append(m.group(1))
    emit_multiset(names, args.map)


def ctest_json(args):
    out = subprocess.run(["ctest", "--test-dir", args.build, "--show-only=json-v1"],
                         check=True, capture_output=True, text=True).stdout
    tests = json.loads(out).get("tests", [])
    if not tests:  # --no-tests=error is ignored under --show-only
        sys.exit("ctest --show-only listed no tests")
    rows = []
    for t in tests:
        labels = []
        for p in t.get("properties", []):
            if p.get("name") == "LABELS":
                labels = p.get("value", [])
        rows.append(f"{mapped(t['name'], args.map)}\t{','.join(sorted(labels))}")
    for row in sorted(rows):
        print(row)


def gtest_runs(args):
    names = []
    for line in open(args.file, encoding="utf-8", errors="replace"):
        m = re.match(r"^\[ RUN      \] (\S+)", clean(line))
        if m:
            names.append(m.group(1))
    emit_multiset(names, False)


def xval(args):
    lines = sorted({m.group(0) for line in open(args.file, encoding="utf-8", errors="replace")
                    if (m := re.search(r"\[ measured \] cross-validation .*", clean(line)))})
    if not lines:
        sys.exit("no cross-validation lines")
    print("\n".join(lines))


def symbols(args):
    out = subprocess.run(["nm", "-D", "--defined-only", args.lib],
                         check=True, capture_output=True, text=True).stdout
    # The C ABI is the unmangled symbols. Weak C++ template and inline
    # instantiations (_Z...) are incidental to it and re-mangle under the
    # namespace rename; G4 covers their code.
    syms = sorted({f"{p[1]} {p[2]}" for p in (l.split() for l in out.splitlines())
                   if len(p) == 3 and not p[2].startswith("_Z")})
    if not syms:
        sys.exit("no exported symbols")
    print("\n".join(syms))


def history(args):
    # Keyed by author date and subject, never by SHA: RatioTap's SHAs are
    # rewritten by filter-repo at 1b, and both survive the rewrite.
    for path in args.paths:
        log = subprocess.run(["git", "-C", args.repo, "log", "--follow", "--format=%aI %s",
                              args.rev, "--", path], check=True, capture_output=True,
                             text=True).stdout.splitlines()
        blame = subprocess.run(["git", "-C", args.repo, "blame", "--line-porcelain", args.rev,
                                "--", path], check=True, capture_output=True,
                               text=True).stdout.splitlines()
        per_commit, meta, cur = collections.Counter(), {}, None
        for line in blame:
            if re.match(r"^[0-9a-f]{40} ", line):
                cur = line.split()[0]
                meta.setdefault(cur, {})
            elif line.startswith("author-time "):
                meta[cur]["t"] = line.split()[1]
            elif line.startswith("summary "):
                meta[cur]["s"] = line[len("summary "):]
            elif line.startswith("\t"):
                per_commit[(meta[cur].get("t"), meta[cur].get("s"))] += 1
        print(f"== {path}  commits={len(log)}  lines={sum(per_commit.values())}")
        for entry in log:
            print(f"log   {entry}")
        for (t, s), n in sorted(per_commit.items(), key=lambda kv: (kv[0][0] or "", kv[0][1] or "")):
            print(f"blame {n:5d}  {t}  {s}")


def retired(args):
    rules = open(args.rules).read().splitlines()
    patterns = [re.compile(l.split(None, 1)[1]) for l in rules if l.startswith("pattern ")]
    allows = []
    for l in rules:
        if l.startswith("allow "):
            glob, rx = l.split("--")[0].split(None, 2)[1:]
            allows.append((glob, re.compile(rx.strip())))
    files = subprocess.run(["git", "-C", args.tree, "ls-files", "-z"], check=True,
                           capture_output=True).stdout.decode().split("\0")
    hits = 0
    for path in filter(None, files):
        if path.startswith("docs/migration/"):
            continue
        try:
            text = open(f"{args.tree}/{path}", encoding="utf-8").read()
        except (UnicodeDecodeError, IsADirectoryError, FileNotFoundError):
            continue
        for n, line in enumerate(text.splitlines(), 1):
            for pat in patterns:
                for m in pat.finditer(line):
                    ok = any(fnmatch.fnmatch(path, g) and rx.search(line)
                             for g, rx in allows)
                    if not ok:
                        hits += 1
                        print(f"{path}:{n}: {m.group(0)!r}: {line.strip()[:120]}")
    print(f"G9: {hits} retired-identifier hit(s)")
    sys.exit(1 if hits else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn, arg in (("ctest-log", ctest_log, "file"), ("ctest-json", ctest_json, "build"),
                          ("gtest-runs", gtest_runs, "file"), ("xval", xval, "file"),
                          ("symbols", symbols, "lib")):
        p = sub.add_parser(name)
        p.add_argument(arg)
        p.add_argument("--map", action="store_true", help="apply the D16 test-name map")
        p.set_defaults(fn=fn)
    p = sub.add_parser("retired")
    p.add_argument("tree", nargs="?", default=".")
    p.add_argument("--rules", default=str(__import__("pathlib").Path(__file__).parent / "snapshot" / "g9.txt"))
    p.set_defaults(fn=retired)
    p = sub.add_parser("history")
    p.add_argument("repo")
    p.add_argument("rev")
    p.add_argument("paths", nargs="+")
    p.set_defaults(fn=history)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
