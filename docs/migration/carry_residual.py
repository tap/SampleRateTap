#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Carry a G14 residual allowlist forward to the next step.

  carry_residual.py PREV NEXT CHECK_OUTPUT [--reason PATH=TEXT ...]

CHECK_OUTPUT is `rename.py check --through NEXT --allow PREV` output. Each
still-unlisted hunk is added to NEXT: with a --reason given for its path,
or else with the reviewed reason PREV already holds for that path (a
hunk whose text a later rename class touched changes its hash, never its
review). A hunk in a path PREV never listed and no --reason covers is an
error: it is new residual and needs its own review. Every carried hunk is
printed, so the commit that adds NEXT shows what was re-reviewed.
"""
import argparse
import re
import sys

ap = argparse.ArgumentParser()
ap.add_argument("prev")
ap.add_argument("next")
ap.add_argument("check_output")
ap.add_argument("--reason", action="append", default=[])
ap.add_argument("--header", required=True, help="first comment line(s), \\n-separated")
args = ap.parse_args()

prev_lines = open(args.prev).read().splitlines()
reasons = {}
for line in prev_lines:
    if line.startswith("hunk ") and "  -- " in line:
        head, why = line.split("  -- ", 1)
        reasons.setdefault(head.split()[1], why)
given = dict(r.split("=", 1) for r in args.reason)

body = [l for l in prev_lines if l.startswith(("file ", "hunk ")) or not l.strip()]
while body and not body[0].strip():
    body.pop(0)
added, bad = [], []
text = open(args.check_output).read()
for m in re.finditer(r"^RESIDUAL hunk (\S+) ([0-9a-f]{12})\n((?:    .*\n)*)", text, re.M):
    path, h, diff = m.groups()
    why = given.get(path) or reasons.get(path)
    if why is None:
        bad.append(f"{path} {h}")
        continue
    added.append(f"hunk {path} {h}  -- {why}")
    print(f"carried: {path} {h}  -- {why}\n{diff}")
if bad:
    sys.exit("new residual with no reviewed reason: " + ", ".join(bad))
header = ["# " + l for l in args.header.split("\\n")] + [""]
open(args.next, "w").write("\n".join(header + body + added) + "\n")
print(f"{args.next}: {len(added)} hunk(s) carried with their reviewed reasons")
