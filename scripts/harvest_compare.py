#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Record a compare.yml run's instruction counts with their provenance.

Usage: scripts/harvest_compare.py COUNTS_FILE --sha SHA --dsptap SHA
           --run-id ID --run-url URL --date YYYY-MM-DD [--toolchains TEXT]
           [--out async/bench/compare_counts.json]

COUNTS_FILE holds the "CMP_COUNT <target> <binary> <count>" lines the
workflow prints (the whole job log works too; other lines are ignored).
The output is the committed record docs/COMPARISON.md's embedded tables
derive from (scripts/update_compare_docs.py): the counts, and what produced
them — the tree, the DspTap pin, the run and the toolchains — so a stale
table is a CI failure, not a date line nobody re-reads.
"""
import argparse
import json
import pathlib
import re
import sys

LINE = re.compile(r"CMP_COUNT (\w+) (cmp_icount_\w+) (\d+)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("counts_file")
    ap.add_argument("--sha", required=True, help="SampleRateTap commit measured")
    ap.add_argument("--dsptap", required=True, help="DspTap commit the tree pinned")
    ap.add_argument("--run-id", required=True)
    ap.add_argument("--run-url", required=True)
    ap.add_argument("--date", required=True, help="YYYY-MM-DD")
    ap.add_argument("--toolchains", default="",
                    help="free text: compiler, QEMU and image versions")
    ap.add_argument("--out", default="async/bench/compare_counts.json")
    args = ap.parse_args()

    counts: dict[str, dict[str, int]] = {}
    for m in LINE.finditer(pathlib.Path(args.counts_file).read_text()):
        counts.setdefault(m.group(1), {})[m.group(2)] = int(m.group(3))
    if not counts:
        print(f"no CMP_COUNT lines in {args.counts_file}", file=sys.stderr)
        return 1
    doc = {
        "provenance": {
            "sha": args.sha,
            "dsptap": args.dsptap,
            "run_id": str(args.run_id),
            "run_url": args.run_url,
            "date": args.date,
            "toolchains": args.toolchains,
        },
        "counts": {t: dict(sorted(v.items())) for t, v in sorted(counts.items())},
    }
    out = pathlib.Path(args.out)
    out.write_text(json.dumps(doc, indent=2) + "\n")
    n = sum(len(v) for v in counts.values())
    print(f"wrote {out}: {n} counts over {', '.join(sorted(counts))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
