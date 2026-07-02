#!/usr/bin/env python3
"""Regenerates the book's figures (book/src/img/*.svg).

Every figure is produced from the same sources the text cites:

- the filter figures re-run the exact design math of
  include/srt/detail/kaiser.hpp (formula-for-formula port below);
- the servo and feasibility figures are MEASURED: this script compiles
  scripts/book_figures_trace.cpp against the current include/ tree and runs
  it in deterministic virtual time. The feasibility "before" panel compiles
  the same tool against the include/ tree of commit 045de5d — the last
  commit before the PR #25 feasibility fix — extracted with `git archive`,
  so both panels of that figure are measurements, not models;
- the phase-wraparound figure runs the resampler's actual uint64 slip
  arithmetic (mod 2^64) in Python integers;
- the architecture figure is drawn, not computed.

Usage:  python3 scripts/book_figures.py          (from the repo root)
Needs:  numpy, matplotlib, g++, git.

The SVGs are committed. CI does not regenerate them — matplotlib's SVG
output is not byte-stable across matplotlib versions, so a regenerate-and-
diff gate would ratchet toolchain noise, not truth — but the book CI job
does verify that every image the chapters reference exists.
"""

import os
import subprocess
import sys
import tempfile

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "book", "src", "img")
PREFIX_COMMIT = "045de5d"  # last commit before the feasibility fix (PR #25)

# Palette (validated categorical slots + chrome ink; light surface).
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
SECONDARY = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
BASELINE = "#c3c2b7"
BLUE = "#2a78d6"    # slot 1
AQUA = "#1baf7a"    # slot 2 (sub-3:1 contrast: always direct-labeled)
YELLOW = "#eda100"  # slot 3 (sub-3:1 contrast: always direct-labeled)
RED = "#e34948"     # slot 6, used only for the pre-fix (failing) trace

plt.rcParams.update({
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans"],
    "font.size": 9,
    "text.color": INK,
    "axes.edgecolor": BASELINE,
    "axes.labelcolor": SECONDARY,
    "axes.titlecolor": INK,
    "axes.titlesize": 10,
    "axes.linewidth": 0.75,
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.75,
    "grid.linestyle": "-",
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "xtick.labelcolor": MUTED,
    "ytick.labelcolor": MUTED,
    "lines.linewidth": 1.5,
    "lines.solid_joinstyle": "round",
    "lines.solid_capstyle": "round",
    "legend.frameon": False,
    "svg.hashsalt": "sampleratetap-book",
})


def save(fig, name):
    fig.savefig(os.path.join(OUT, name + ".svg"))
    png_dir = os.environ.get("PNG_OUT")  # optional raster copies for review
    if png_dir:
        fig.savefig(os.path.join(png_dir, name + ".png"), dpi=110)


def despine(ax):
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


# --- the filter design math, ported formula-for-formula from kaiser.hpp ---

def bessel_i0(x):
    x = np.asarray(x, dtype=float)
    half = 0.5 * x
    term = np.ones_like(x)
    total = np.ones_like(x)
    for k in range(1, 1000):
        r = half / k
        term = term * r * r
        total = total + term
        if np.all(term < 1e-21 * total):
            break
    return total


def kaiser_beta(atten_db):
    if atten_db > 50.0:
        return 0.1102 * (atten_db - 8.7)
    if atten_db > 21.0:
        return 0.5842 * (atten_db - 21.0) ** 0.4 + 0.07886 * (atten_db - 21.0)
    return 0.0


def design_prototype(num_phases, taps_per_phase, cutoff_norm, beta):
    n = num_phases * taps_per_phase
    i = np.arange(n, dtype=float)
    center = 0.5 * (n - 1)
    t = (i - center) / num_phases
    u = (i - center) / center
    w = bessel_i0(beta * np.sqrt(np.maximum(0.0, 1.0 - u * u))) / bessel_i0(beta)
    h = cutoff_norm * np.sinc(cutoff_norm * t) * w  # np.sinc is sin(pi x)/(pi x)
    return h * (num_phases / h.sum())


# FilterSpec presets, verbatim from polyphase_filter.hpp.
PRESETS = [
    ("fast", 128, 32, 18000.0, 30000.0, 96.0, BLUE),
    ("balanced", 256, 48, 20000.0, 28000.0, 120.0, AQUA),
    ("transparent", 512, 80, 20000.0, 26000.0, 140.0, YELLOW),
]
FS = 48000.0


def preset_response(L, T, pass_hz, stop_hz, atten_db, nfft=1 << 21):
    cutoff = (pass_hz + stop_hz) / FS
    h = design_prototype(L, T, cutoff, kaiser_beta(atten_db))
    H = np.fft.rfft(h, nfft) / L
    f = np.arange(H.size) * (L * FS) / nfft
    keep = f <= 48000.0
    return f[keep], 20.0 * np.log10(np.maximum(np.abs(H[keep]), 1e-12))


def fig_kaiser_window():
    fig, ax = plt.subplots(figsize=(6.4, 3.2), layout="constrained")
    u = np.linspace(-1.0, 1.0, 801)
    iu = 180  # u = -0.55, where the three curves are well separated
    for name, _, _, _, _, atten, color in PRESETS:
        beta = kaiser_beta(atten)
        w = bessel_i0(beta * np.sqrt(1.0 - u * u)) / bessel_i0(beta)
        ax.plot(u, w, color=color, label=f"{name}: {atten:.0f} dB, β = {beta:.1f}")
        ax.annotate(name, (u[iu], w[iu]), xytext=(-4, 4),
                    textcoords="offset points", color=SECONDARY, fontsize=8.5,
                    ha="right", bbox=dict(fc=SURFACE, ec="none", pad=1.0))
    ax.set_xlabel("window argument u (full aperture −1 … 1)")
    ax.set_ylabel("w(u)")
    ax.set_title("Kaiser window: attenuation buys taper")
    ax.legend(loc="upper right", fontsize=8.5)
    ax.set_xlim(-1.0, 1.0)
    ax.set_ylim(0.0, 1.05)
    despine(ax)
    save(fig, "kaiser-window")
    plt.close(fig)


def fig_kaiser_response():
    fig, (ax, axz) = plt.subplots(
        2, 1, figsize=(7.0, 5.6), layout="constrained", height_ratios=[2.4, 1.0])
    for name, L, T, pass_hz, stop_hz, atten, color in PRESETS:
        f, db = preset_response(L, T, pass_hz, stop_hz, atten)
        ax.plot(f / 1e3, db, color=color, label=name)
        axz.plot(f / 1e3, db, color=color)
        # direct label at each preset's measured stopband floor
        floor = db[f >= stop_hz].max()
        ax.annotate(f"{name}: {floor:.0f} dB past {stop_hz/1e3:.0f} kHz",
                    (47.0, floor), xytext=(0, 7),
                    textcoords="offset points", color=SECONDARY, fontsize=8.5,
                    ha="right", bbox=dict(fc=SURFACE, ec="none", pad=1.0))
    for x, label in ((20.0, "20 kHz passband edge"), (24.0, "input Nyquist")):
        ax.axvline(x, color=BASELINE, lw=0.75, zorder=0)
        ax.annotate(label, (x, -182), rotation=90, xytext=(-3, 0),
                    textcoords="offset points", color=MUTED,
                    fontsize=7.5, ha="right", va="bottom",
                    bbox=dict(fc=SURFACE, ec="none", pad=1.0))
    ax.set_ylim(-185, 8)
    ax.set_xlim(0, 48)
    ax.set_ylabel("magnitude (dB)")
    ax.set_title("Prototype magnitude response, the three presets")
    ax.legend(loc="upper right", fontsize=8.5)
    despine(ax)
    axz.set_xlim(0, 22)
    axz.set_ylim(-0.031, 0.031)
    axz.set_xlabel("frequency at 48 kHz (kHz)")
    axz.set_ylabel("passband detail (dB)")
    axz.annotate("all three presets flat within ±0.01 dB across their passbands",
                 (0.5, 0.021), color=SECONDARY, fontsize=8.5, ha="left")
    despine(axz)
    save(fig, "kaiser-response")
    plt.close(fig)


# --- measured traces via the C++ tool ---

def build_trace_tool(include_dir, exe):
    subprocess.run(
        ["g++", "-O2", "-std=c++20", f"-I{include_dir}",
         os.path.join(ROOT, "scripts", "book_figures_trace.cpp"), "-o", exe],
        check=True)


def run_trace(exe, *args):
    out = subprocess.run([exe] + [str(a) for a in args],
                         check=True, capture_output=True, text=True).stdout
    rows = [line.split(",") for line in out.strip().splitlines()[1:]]
    a = np.array(rows, dtype=float)
    return {"t": a[:, 0], "fill": a[:, 1], "state": a[:, 2],
            "ppm": a[:, 3], "underruns": a[:, 4]}


def fig_servo_lock(head_exe):
    # 1-frame pushes: the long tests' methodology — block-quantized pushes
    # would hide the 200 ppm surplus in one 32-frame lump every ~3.3 s.
    tr = run_trace(head_exe, 32, 1, 200, 45, 28.0, 0.05)
    fig, (axf, axp) = plt.subplots(
        2, 1, figsize=(7.0, 4.8), sharex=True, layout="constrained")

    state = tr["state"]
    t_lock1 = tr["t"][np.argmax(state == 2)]
    i_stall = int(np.searchsorted(tr["underruns"], 0.5))
    t_stall = tr["t"][i_stall]
    after_stall = (tr["t"] > t_stall) & (state == 2)
    t_lock2 = tr["t"][np.argmax(after_stall)]

    axf.plot(tr["t"], tr["fill"], color=BLUE, lw=1.2)
    axf.axhline(48, color=BASELINE, lw=0.75, zorder=0)
    axf.annotate("setpoint 48", (44.8, 48), xytext=(0, 5),
                 textcoords="offset points", color=MUTED, fontsize=8, ha="right")
    axf.set_ylim(46.6, 50.6)
    axf.annotate(f"cold start: Locked in {t_lock1:.2f} s",
                 (t_lock1, 50.0), xytext=(2.5, 50.0), textcoords="data",
                 color=SECONDARY, fontsize=8,
                 arrowprops=dict(arrowstyle="-", color=SECONDARY, lw=0.75))
    axf.annotate("50 ms producer stall → refill → "
                 f"re-Locked {t_lock2 - t_stall:.2f} s later",
                 (t_stall, 50.0), xytext=(30.5, 50.0), textcoords="data",
                 color=SECONDARY, fontsize=8,
                 arrowprops=dict(arrowstyle="-", color=SECONDARY, lw=0.75))
    axf.set_ylabel("FIFO occupancy (frames)")
    axf.set_title("Acquire, lock, dropout, re-lock  (measured, producer +200 ppm)")

    axp.plot(tr["t"], tr["ppm"], color=BLUE, lw=1.2)
    for y in (1500, -1500):
        axp.axhline(y, color=BASELINE, lw=0.75, zorder=0)
    axp.axhline(200, color=BASELINE, lw=0.75, zorder=0)
    axp.annotate("true offset 200 ppm", (44.8, 200), xytext=(0, 5),
                 textcoords="offset points", color=MUTED, fontsize=8, ha="right")
    axp.annotate("servo clamp ±1500 ppm", (44.8, 1500), xytext=(0, -10),
                 textcoords="offset points", color=MUTED, fontsize=8,
                 ha="right", va="top")
    axp.annotate("Acquiring (10 Hz) rings against the clamp\n"
                 "on the ±1-frame quantized occupancy",
                 (2.7, 900), color=SECONDARY, fontsize=8, ha="left")
    axp.annotate("Locked (1 Hz): settles on the true offset",
                 (14, 480), color=SECONDARY, fontsize=8, ha="left")
    axp.set_ylim(-1750, 1950)
    axp.set_ylabel("estimated ppm")
    axp.set_xlabel("time (s)")

    # hairlines at the recorded stage transitions
    for i in np.flatnonzero(np.diff(state) != 0):
        for ax in (axf, axp):
            ax.axvline(tr["t"][i + 1], color=GRID, lw=0.75, zorder=0)
    for ax in (axf, axp):
        despine(ax)
    save(fig, "servo-lock")
    plt.close(fig)
    return tr


def fig_feasibility(head_exe, prefix_exe):
    before = run_trace(prefix_exe, 64, 32, 200, 6)
    after = run_trace(head_exe, 64, 32, 200, 6)
    fig, (axb, axa) = plt.subplots(
        2, 1, figsize=(7.0, 4.8), sharex=True, sharey=True, layout="constrained")

    axb.plot(before["t"], before["fill"], color=RED, lw=1.2)
    hits = np.flatnonzero(np.diff(before["underruns"]) > 0)
    axb.plot(before["t"][hits + 1], before["fill"][hits + 1], "o",
             ms=4.5, color=RED, mec=SURFACE, mew=1.0, ls="none")
    axb.annotate(f"{int(before['underruns'][-1])} underruns in 6 s — "
                 "one every ~0.25 s, forever",
                 (1.6, 116), color=SECONDARY, fontsize=8.5, ha="left")
    axb.axhline(48, color=BASELINE, lw=0.75, zorder=0)
    axb.set_title(f"Before  (commit {PREFIX_COMMIT}, measured): "
                  "pull(64) against setpoint 48")
    axb.set_ylabel("FIFO occupancy (frames)")

    axa.plot(after["t"], after["fill"], color=BLUE, lw=1.2)
    axa.axhline(48, color=BASELINE, lw=0.75, zorder=0)
    axa.axhline(96, color=BASELINE, lw=0.75, zorder=0)
    axa.annotate("configured setpoint 48", (5.95, 48), xytext=(0, 5),
                 textcoords="offset points", color=MUTED, fontsize=8, ha="right",
                 bbox=dict(fc=SURFACE, ec="none", pad=1.0))
    axa.annotate("effective setpoint 96 = 64 + 64/2, raised on first pull",
                 (5.95, 96), xytext=(0, 5), textcoords="offset points",
                 color=MUTED, fontsize=8, ha="right",
                 bbox=dict(fc=SURFACE, ec="none", pad=1.0))
    axa.set_title(f"After  (HEAD, measured): {int(after['underruns'][-1])} underruns, "
                  "servo regulates the raised setpoint")
    axa.set_ylabel("FIFO occupancy (frames)")
    axa.set_xlabel("time (s)")
    axa.set_ylim(38, 132)
    for ax in (axb, axa):
        despine(ax)
    save(fig, "feasibility")
    plt.close(fig)
    return before, after


# --- the Q0.64 wraparound, run with the real modular arithmetic ---

def fig_q064():
    M = 1 << 64
    eps_mag = 0.09  # exaggerated so the wrap is visible; real |eps| ~ 2e-4
    fig, axes = plt.subplots(
        1, 2, figsize=(7.0, 3.0), sharey=True, layout="constrained")
    for ax, sign, title, note in (
        (axes[0], +1, "ε > 0: wrap past 1.0 → advance 2",
         "consume one extra input frame"),
        (axes[1], -1, "ε < 0: wrap below 0.0 → advance 0",
         "re-use the current window"),
    ):
        eps_fix = int(sign * eps_mag * M) % M  # two's-complement, like the C++
        phase, mu = 0 if sign > 0 else int(0.5 * M), []
        wraps = []
        for n in range(26):
            m = (phase + eps_fix) % M
            if sign > 0 and m < phase:
                wraps.append(n)
            if sign < 0 and m > phase:
                wraps.append(n)
            phase = m
            mu.append(phase / M)
        n = np.arange(26)
        mu = np.array(mu)
        ax.plot(n, mu, color=BLUE, lw=1.2, marker="o", ms=4.5,
                mec=SURFACE, mew=1.0)
        for w in wraps:
            ax.plot([w], [mu[w]], "o", ms=6, color=BLUE, mec=SURFACE, mew=1.0)
        w0 = wraps[0]  # annotate the first wrap only; the rest just repeat
        ax.annotate(note, (w0, mu[w0]), xytext=(8, 16 * sign),
                    textcoords="offset points", color=SECONDARY, fontsize=8,
                    bbox=dict(fc=SURFACE, ec="none", pad=1.0),
                    arrowprops=dict(arrowstyle="-", color=SECONDARY, lw=0.75))
        ax.set_title(title, fontsize=9)
        ax.set_xlabel("output frame n")
        ax.set_ylim(-0.06, 1.06)
        despine(ax)
    axes[0].set_ylabel("phase μ = phase_ / 2⁶⁴")
    fig.suptitle("The Q0.64 accumulator slips by wrapping  (ε exaggerated to 0.09; real |ε| ≈ 2×10⁻⁴)",
                 fontsize=9.5, color=INK)
    save(fig, "q064-slip")
    plt.close(fig)


# --- the architecture diagram (drawn) ---

def fig_architecture():
    fig, ax = plt.subplots(figsize=(8.2, 3.6), layout="constrained")
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 44)
    ax.axis("off")

    # clock-domain washes
    for x0, x1, color, label in ((0, 33, BLUE, "input clock domain (producer)"),
                                 (52, 100, AQUA, "output clock domain (consumer)")):
        ax.add_patch(FancyBboxPatch((x0 + 0.5, 1), x1 - x0 - 1, 42,
                                    boxstyle="round,pad=0,rounding_size=1.5",
                                    fc=color, ec="none", alpha=0.08))
        ax.text((x0 + x1) / 2, 2.6, label, ha="center", color=SECONDARY,
                fontsize=8)

    def box(x, y, w, h, title, sub=None, weight="bold"):
        ax.add_patch(FancyBboxPatch((x, y), w, h,
                                    boxstyle="round,pad=0,rounding_size=1.2",
                                    fc=SURFACE, ec=BASELINE, lw=1.0))
        cy = y + h / 2 + (1.6 if sub else 0)
        ax.text(x + w / 2, cy, title, ha="center", va="center",
                color=INK, fontsize=8.6, fontweight=weight)
        if sub:
            ax.text(x + w / 2, cy - 3.8, sub, ha="center", va="center",
                    color=SECONDARY, fontsize=7.6)

    def arrow(p, q, label=None, dy=1.4, style="-|>"):
        ax.add_patch(FancyArrowPatch(p, q, arrowstyle=style, color=SECONDARY,
                                     lw=1.1, mutation_scale=9,
                                     shrinkA=1, shrinkB=1))
        if label:
            ax.text((p[0] + q[0]) / 2, (p[1] + q[1]) / 2 + dy, label,
                    ha="center", color=SECONDARY, fontsize=7.6)

    box(3, 24, 16, 10, "producer", "audio callback / core 0")
    box(36, 24, 15, 10, "SpscRing", "interleaved frames")
    box(60, 7, 16, 10, "PiServo", "occupancy → ε̂")
    box(57, 24, 22, 10, "FractionalResampler", "polyphase bank + Q0.64 phase")
    box(84, 24, 13, 10, "consumer", "core 1 / thread")

    arrow((19, 29), (36, 29), "push()")
    arrow((51, 29), (57, 29), "pop")
    arrow((79, 29), (84, 29), "pull()")
    # occupancy: ring bottom, down and across to the servo
    arrow((43.5, 24), (43.5, 12), None, style="-")
    arrow((43.5, 12), (60, 12), None)
    ax.text(51.5, 13.4, "occupancy", ha="center", color=SECONDARY, fontsize=7.6)
    # rate estimate: servo top, up into the resampler
    arrow((70, 17), (70, 24), None)
    ax.text(71.5, 20.2, "ε̂ (rate estimate)", ha="left", color=SECONDARY,
            fontsize=7.6)
    ax.text(50, 41.5, "one passive object, two callers — the converter owns no threads",
            ha="center", color=SECONDARY, fontsize=8.4)
    save(fig, "architecture")
    plt.close(fig)


def main():
    os.makedirs(OUT, exist_ok=True)
    fig_kaiser_window()
    fig_kaiser_response()
    fig_q064()
    fig_architecture()

    with tempfile.TemporaryDirectory() as tmp:
        head_exe = os.path.join(tmp, "trace_head")
        build_trace_tool(os.path.join(ROOT, "include"), head_exe)
        prefix_tree = os.path.join(tmp, "prefix")
        os.makedirs(prefix_tree)
        archive = subprocess.run(["git", "-C", ROOT, "archive", PREFIX_COMMIT, "include"],
                                 check=True, capture_output=True).stdout
        subprocess.run(["tar", "-x", "-C", prefix_tree], input=archive, check=True)
        prefix_exe = os.path.join(tmp, "trace_prefix")
        build_trace_tool(os.path.join(prefix_tree, "include"), prefix_exe)

        tr = fig_servo_lock(head_exe)
        before, after = fig_feasibility(head_exe, prefix_exe)

    print(f"servo: locked at t={tr['t'][np.argmax(tr['state'] == 2)]:.1f}s, "
          f"final ppm {tr['ppm'][-1]:.1f}, underruns {int(tr['underruns'][-1])}")
    print(f"feasibility: before {int(before['underruns'][-1])} underruns/6s, "
          f"after {int(after['underruns'][-1])}")
    print(f"wrote 6 SVGs to {OUT}")


if __name__ == "__main__":
    main()
