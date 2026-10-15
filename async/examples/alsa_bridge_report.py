#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Report on an alsa_bridge --csv run (docs/HARDWARE_TESTING.md, Setup 1).

  alsa_bridge_report.py RUN.csv [--skip SECONDS] [--plot OUT.png]
  alsa_bridge_report.py --self-test

The bridge logs, once per second, the converter's status() beside each
device's cumulative frame count and CLOCK_MONOTONIC_RAW (and, where the
driver gives them, each device's last hardware pointer update with its own
timestamp). This script answers the bench's questions from that record:

- What are the two crystals' rates? A least-squares line through each
  device's frames against time gives its rate in frames per second, from the
  threads' counts and, where present, from the hardware timestamps, which
  carry no scheduling jitter. Their ratio is the true clock offset,
  (rate_in / rate_out - 1) * 1e6 ppm, the quantity the servo estimates.
- Does the servo agree? The mean of its ppm over the locked rows, against
  that measured offset.
- How quiet is it? The ppm "wobble" (standard deviation and peak to peak
  of the per-second estimate while locked) and the FIFO's "breathing" (the
  same of its smoothed fill).
- Did anything break? The final underrun, overrun, resync and ALSA xrun
  counts, which a soak must hold at zero.

Standard library only; --plot needs matplotlib. --self-test checks the
arithmetic against a synthetic run with known rates and noise.
"""
import argparse
import csv
import math
import random
import statistics
import sys
import tempfile


def fit_rate(t_s: list[float], frames: list[float]) -> tuple[float, float]:
    """Least-squares slope (frames per second) and the residuals' RMS."""
    n = len(t_s)
    if n < 2:
        return math.nan, math.nan
    mt = sum(t_s) / n
    mf = sum(frames) / n
    sxx = sum((t - mt) ** 2 for t in t_s)
    if sxx == 0.0:
        return math.nan, math.nan
    slope = sum((t - mt) * (f - mf) for t, f in zip(t_s, frames)) / sxx
    resid = [f - (mf + slope * (t - mt)) for t, f in zip(t_s, frames)]
    return slope, math.sqrt(sum(r * r for r in resid) / n)


def load(path: str) -> list[dict[str, str]]:
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def spread(values: list[float]) -> tuple[float, float, float]:
    """Mean, standard deviation and peak to peak (NaN when empty)."""
    if not values:
        return math.nan, math.nan, math.nan
    sd = statistics.pstdev(values) if len(values) > 1 else 0.0
    return statistics.fmean(values), sd, max(values) - min(values)


def analyse(rows: list[dict[str, str]], skip_s: float) -> dict:
    rows = [r for r in rows if float(r["time_s"]) > skip_s]
    if len(rows) < 2:
        raise SystemExit("not enough rows after --skip")
    out: dict = {"rows": len(rows), "seconds": float(rows[-1]["time_s"]) - float(rows[0]["time_s"])}

    t = [int(r["mono_raw_ns"]) * 1e-9 for r in rows]
    for dev in ("in", "out"):
        out[f"{dev}_rate"], out[f"{dev}_rms"] = fit_rate(t, [float(r[f"{dev}_frames"]) for r in rows])
        hw = [r for r in rows if r.get(f"{dev}_hw_ns")]
        if len(hw) >= 2:
            out[f"{dev}_hw_rate"], out[f"{dev}_hw_rms"] = fit_rate(
                [int(r[f"{dev}_hw_ns"]) * 1e-9 for r in hw], [float(r[f"{dev}_hw_frames"]) for r in hw])
        else:
            out[f"{dev}_hw_rate"] = out[f"{dev}_hw_rms"] = math.nan
    out["ppm_measured"] = (out["in_rate"] / out["out_rate"] - 1.0) * 1e6
    out["ppm_measured_hw"] = (out["in_hw_rate"] / out["out_hw_rate"] - 1.0) * 1e6

    locked = [r for r in rows if r["state"] == "Locked"]
    out["locked_rows"] = len(locked)
    out["ppm_mean"], out["ppm_sd"], out["ppm_pp"] = spread([float(r["ppm"]) for r in locked])
    out["fill_mean"], out["fill_sd"], out["fill_pp"] = spread([float(r["fill"]) for r in locked])

    last = rows[-1]
    for k in ("underruns", "overruns", "resyncs", "in_xruns", "out_xruns", "in_errors", "out_errors"):
        out[k] = int(last[k])
    return out


def report(a: dict) -> str:
    def ppm(v: float) -> str:
        return "n/a" if math.isnan(v) else f"{v:+.3f} ppm"

    lines = [
        f"{a['rows']} rows over {a['seconds']:.0f} s ({a['locked_rows']} locked)",
        f"input  {a['in_rate']:.4f} frames/s (thread counts, residual rms {a['in_rms']:.1f} frames)",
        f"output {a['out_rate']:.4f} frames/s (thread counts, residual rms {a['out_rms']:.1f} frames)",
        f"measured offset:          {ppm(a['ppm_measured'])} (thread counts)",
        f"measured offset, hw:      {ppm(a['ppm_measured_hw'])} (hardware timestamps)",
        f"servo, mean while locked: {ppm(a['ppm_mean'])} "
        f"(servo minus measured {ppm(a['ppm_mean'] - a['ppm_measured'])})",
        f"ppm wobble while locked:  sd {a['ppm_sd']:.3f}, peak to peak {a['ppm_pp']:.3f}",
        f"fill breathing:           mean {a['fill_mean']:.2f}, sd {a['fill_sd']:.3f}, "
        f"peak to peak {a['fill_pp']:.3f} frames",
        f"underruns {a['underruns']}, overruns {a['overruns']}, resyncs {a['resyncs']}, "
        f"xruns in {a['in_xruns']} out {a['out_xruns']}, other recoveries in {a['in_errors']} "
        f"out {a['out_errors']}",
    ]
    return "\n".join(lines)


def plot(rows: list[dict[str, str]], a: dict, path: str) -> None:
    import matplotlib  # noqa: PLC0415 (optional dependency)
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt  # noqa: PLC0415

    t = [float(r["time_s"]) for r in rows]
    fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(9, 6))
    ax1.plot(t, [float(r["ppm"]) for r in rows], lw=0.8, label="servo ppm")
    ax1.axhline(a["ppm_measured"], color="k", ls="--", lw=0.8, label="measured (frame counts)")
    ax1.set_ylabel("ppm")
    ax1.legend()
    ax2.plot(t, [float(r["fill"]) for r in rows], lw=0.8)
    ax2.set_ylabel("FIFO fill (frames)")
    ax2.set_xlabel("time (s)")
    fig.tight_layout()
    fig.savefig(path, dpi=120)


def self_test() -> int:
    """A synthetic hour: input 80 ppm fast against a 48 kHz output, frame
    counts sampled with up to a period of jitter, hardware timestamps exact
    to 2 us, a servo estimate that wobbles 0.5 ppm round the truth."""
    rng = random.Random(1)
    true_ppm, fs = 80.0, 48000.0
    rate_in, rate_out = fs * (1 + true_ppm * 1e-6), fs
    fields = ["time_s", "state", "ppm", "fill", "underruns", "overruns", "resyncs", "mono_raw_ns",
              "in_frames", "out_frames", "in_xruns", "out_xruns", "in_errors", "out_errors",
              "in_hw_frames", "in_hw_ns", "out_hw_frames", "out_hw_ns"]
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False, newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        t0 = 1_000_000_000_000
        for s in range(1, 3601):
            ns = t0 + s * 1_000_000_000
            hw_ns_in = ns - rng.randrange(0, 2_000_000)
            hw_ns_out = ns - rng.randrange(0, 2_000_000)
            w.writerow({
                "time_s": s, "state": "Locked" if s > 30 else "Acquiring",
                "ppm": f"{true_ppm + rng.gauss(0, 0.5):.3f}", "fill": f"{96 + rng.gauss(0, 2):.2f}",
                "underruns": 0, "overruns": 0, "resyncs": 0, "mono_raw_ns": ns,
                "in_frames": int(rate_in * (ns - t0) * 1e-9) - rng.randrange(0, 128),
                "out_frames": int(rate_out * (ns - t0) * 1e-9) - rng.randrange(0, 128),
                "in_xruns": 0, "out_xruns": 0, "in_errors": 0, "out_errors": 0,
                "in_hw_frames": round(rate_in * (hw_ns_in - t0) * 1e-9), "in_hw_ns": hw_ns_in,
                "out_hw_frames": round(rate_out * (hw_ns_out - t0) * 1e-9), "out_hw_ns": hw_ns_out,
            })
        path = f.name
    a = analyse(load(path), skip_s=60)
    print(report(a))
    checks = [
        ("thread-count offset within 0.05 ppm", abs(a["ppm_measured"] - true_ppm) < 0.05),
        ("hardware offset within 0.001 ppm", abs(a["ppm_measured_hw"] - true_ppm) < 0.001),
        ("servo mean within 0.05 ppm", abs(a["ppm_mean"] - true_ppm) < 0.05),
        ("wobble sd near 0.5", abs(a["ppm_sd"] - 0.5) < 0.05),
        ("breathing sd near 2", abs(a["fill_sd"] - 2.0) < 0.2),
        ("only locked rows count", a["locked_rows"] == 3600 - 60),
    ]
    failed = [name for name, ok in checks if not ok]
    for name, ok in checks:
        print(f"{'ok  ' if ok else 'FAIL'} {name}")
    return 1 if failed else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("csv", nargs="?", help="the bridge's --csv output")
    ap.add_argument("--skip", type=float, default=60.0,
                    help="seconds to leave out at the start (acquisition; default 60)")
    ap.add_argument("--plot", help="write the ppm and fill traces to this image (needs matplotlib)")
    ap.add_argument("--self-test", action="store_true", help="check the arithmetic on a synthetic run")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.csv:
        ap.error("a CSV path is required (or --self-test)")
    rows = load(args.csv)
    a = analyse(rows, args.skip)
    print(report(a))
    if args.plot:
        plot([r for r in rows if float(r["time_s"]) > args.skip], a, args.plot)
    return 0


if __name__ == "__main__":
    sys.exit(main())
