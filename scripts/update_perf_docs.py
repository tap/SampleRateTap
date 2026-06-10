#!/usr/bin/env python3
"""Run the host benchmarks and refresh the README performance table.

Usage: scripts/update_perf_docs.py path/to/srt_bench [README.md]

Rewrites the block between <!-- PERF:BEGIN --> and <!-- PERF:END --> with a
machine- and date-annotated table. See docs/PERFORMANCE.md.
"""
import datetime
import json
import pathlib
import platform
import re
import subprocess
import sys

BEGIN, END = "<!-- PERF:BEGIN -->", "<!-- PERF:END -->"
FS_HZ = 48000.0


def cpu_name() -> str:
    try:
        for line in pathlib.Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def run_bench(bench: str) -> list[dict]:
    out = subprocess.run(
        [bench, "--benchmark_format=json", "--benchmark_min_time=0.5s"],
        check=True, capture_output=True, text=True).stdout
    return json.load(__import__("io").StringIO(out))["benchmarks"]


def table(rows: list[dict]) -> str:
    lines = [
        f"Indicative numbers from a shared machine ({cpu_name()}, "
        f"{datetime.date.today().isoformat()}); regenerate with "
        "`scripts/update_perf_docs.py`. Items are output samples (kernel) "
        "or frames (pipeline); ×realtime is per 48 kHz stream.",
        "",
        "| Benchmark | ns/item | ×realtime @48k |",
        "|---|---:|---:|",
    ]
    for b in rows:
        if b.get("run_type") != "iteration":
            continue
        items_s = b.get("items_per_second")
        if not items_s:
            continue
        ns_item = 1e9 / items_s
        lines.append(f"| `{b['name']}` | {ns_item:,.1f} | {items_s / FS_HZ:,.0f}× |")
    return "\n".join(lines)


def main() -> int:
    bench = sys.argv[1]
    readme = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "README.md")
    text = readme.read_text()
    if BEGIN not in text or END not in text:
        print(f"markers not found in {readme}", file=sys.stderr)
        return 1
    block = f"{BEGIN}\n{table(run_bench(bench))}\n{END}"
    text = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), block, text, flags=re.S)
    readme.write_text(text)
    print(f"updated {readme}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
