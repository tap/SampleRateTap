# rational vs. other sample rate converters

The async comparison ([`async/docs/COMPARISON.md`](../../async/docs/COMPARISON.md))
asks what clock recovery costs; the bridge comparison
([`bridge/docs/COMPARISON.md`](../../bridge/docs/COMPARISON.md)) asks the
synchronous question at one ratio pair. This document asks it at four ratios
of `rational`'s vocabulary: **↑2 (48 → 96 kHz), ↓2 (96 → 48), 3/2 (32 → 48)
and 2/3 (48 → 32)** — the by-2 pair every rate family has and the mixed
pair one Nyquist stage serves. Every library converts exactly these ratios,
on one clock, so this is engine against engine on the same number, as the
bridge comparison is.

## The matching rule

`rational` ships the family's two tiers that matter here: `economy` (70 dB
stopband, passband 3/8 of the lower rate; the speed-first default) and
`transparent` (120 dB, 5/12). The competitors are measured at **matched
spec** and at their **frontier**:

- A library's *matched* setting for a tier is its cheapest whose measured
  stopband is at least the tier's and whose passband droops no more than
  0.1 dB at the tier's edge, at **every one of the four ratios**. The sweep
  in [notebooks/rational_comparison.ipynb](../notebooks/rational_comparison.ipynb)
  measures every candidate: the droop at both edges, and the worst spurious
  product at the exact frequencies the arithmetic puts them (every
  |a·f_in + b·f_out ± f| below the output Nyquist), from tones in the
  passband and, for the decimations, from tones in the stopband above
  (lower rate − passband edge), whose aliases land inside the passband. A
  Nyquist design's transition band is symmetric about the lower Nyquist, so
  tones there alias above the passband edge by contract; the probes sit
  above it. The picks are asserted against the constants the benches
  compile in.
- Its *frontier* is its best setting.

| Tier | libsamplerate | soxr | r8brain-free-src | SpeexDSP |
|---|---|---|---|---|
| `economy` (70 dB, 3/8) | `SINC_MEDIUM` (its `FASTEST` droops 0.5 dB at the edge) | 16-bit precision at a 0.75 passband (its lowest precision) | 70 dB at a 16 % band | quality 2 (float), quality 3 (fixed-point) |
| `transparent` (120 dB, 5/12) | `SINC_BEST` | 20-bit at a 0.80 passband | 120 dB at a 10 % band | quality 9 (float; the fixed-point build cannot reach 120 dB) |
| frontier | `SINC_BEST` | `VHQ` | `CDSPResampler24` (180.15 dB) | quality 10 |

The same picture as at 44.1 ↔ 48: r8brain is the one competitor whose
economy-matched setting is a 70 dB design (85–92 dB worst product here,
since a 16 % band leaves margin at the edge); libsamplerate's cheapest
flat-to-the-edge converter is `MEDIUM` at ~117 dB, soxr's lowest precision is
16-bit (~110 dB), SpeexDSP's quality 2 is 70–76 dB — the one competitor
setting that lands on `economy`'s number, at a quarter of its own quality
10's cost.

## Measured, identical conditions

From the notebook (executed 2026-10-10, re-executed 2026-11-20 after X17
re-tuned `rational`'s designer: the `rational` rows below are the new
designs', every other row reproduced unchanged; one AES17-style measurement
implementation applied to every subject — 997 Hz at −1 dBFS, fundamental
removed by exact fit + ±20 Hz notch, residual integrated 20 Hz–20 kHz, or to
1 kHz below the output Nyquist where that is lower (32 kHz out: 20 Hz–15 kHz);
the **24-bit interface** columns quantize each subject's output to 24 bits;
the instrument is calibrated in-notebook at every output rate). Every
subject through the shipping C++ (the C ABI) or the real library
(libsamplerate's Python binding; soxr, r8brain and SpeexDSP through the
family's C shims, `tools/compare/shim/`).

THD+N at the 24-bit interface:

| Subject | ↑2 (48 → 96) | ↓2 (96 → 48) | 3/2 (32 → 48) | 2/3 (48 → 32) |
|---|---:|---:|---:|---:|
| **rational** `economy` | **-146.7 dB** | **-144.1 dB** | **-92.8 dB** | **-143.5 dB** |
| **rational** `balanced` | -146.7 dB | -143.9 dB | -93.5 dB | -143.5 dB |
| **rational** `transparent` | **-146.5 dB** | **-144.0 dB** | **-141.1 dB** | **-143.5 dB** |
| libsamplerate FASTEST | -146.4 dB | -144.1 dB | -121.4 dB | -143.5 dB |
| libsamplerate MEDIUM (economy-matched) | -146.5 dB | -144.4 dB | -139.8 dB | -143.4 dB |
| libsamplerate BEST (transparent-matched) | -146.6 dB | -144.4 dB | -143.9 dB | -143.4 dB |
| soxr 16-bit @ 0.75 (economy-matched) | -137.7 dB | -137.3 dB | -136.3 dB | -136.0 dB |
| soxr 20-bit @ 0.80 (transparent-matched) | -137.1 dB | -136.8 dB | -136.8 dB | -136.5 dB |
| soxr VHQ | -146.6 dB | -144.4 dB | -143.9 dB | -143.7 dB |
| r8brain 70 dB @ 16 % (economy-matched) | -146.4 dB | -121.8 dB | -122.5 dB | -143.6 dB |
| r8brain 120 dB @ 10 % (transparent-matched) | -146.6 dB | -144.4 dB | -143.6 dB | -143.3 dB |
| r8brain CDSPResampler24 | -146.6 dB | -144.4 dB | -143.9 dB | -143.7 dB |
| SpeexDSP q2 float (economy-matched) | -142.3 dB | -137.4 dB | -82.1 dB | -137.9 dB |
| SpeexDSP q9 float (transparent-matched) | -145.8 dB | -143.8 dB | -137.4 dB | -143.3 dB |
| SpeexDSP q10 float | -146.0 dB | -143.9 dB | -141.8 dB | -143.2 dB |
| **rational** `economy` (Q15) | -96.9 dB | -95.6 dB | -80.4 dB | -95.5 dB |
| **rational** `transparent` (Q15) | -96.9 dB | -96.5 dB | -86.6 dB | -95.3 dB |
| SpeexDSP q3 fixed-point | -96.1 dB | -96.3 dB | -91.9 dB | -95.2 dB |
| SpeexDSP q10 fixed-point | -96.2 dB | -96.4 dB | -89.6 dB | -95.3 dB |

Dynamic range (−60 dBFS, A-weighted, 24-bit interface), the rows that differ:

| Subject | ↑2 (48 → 96) | ↓2 (96 → 48) | 3/2 (32 → 48) | 2/3 (48 → 32) |
|---|---:|---:|---:|---:|
| **rational** `economy` | 152.0 dB | 149.0 dB | 148.7 dB | 147.6 dB |
| **rational** `transparent` | 152.1 dB | 149.0 dB | 149.1 dB | 147.6 dB |
| libsamplerate BEST (transparent-matched) | 152.2 dB | 149.1 dB | 149.1 dB | 147.6 dB |
| **rational** `economy` (Q15) | 99.9 dB | 100.6 dB | 98.3 dB | 97.9 dB |
| SpeexDSP q3 fixed-point | 99.6 dB | 100.6 dB | 98.1 dB | 97.9 dB |

Reading guide:

- **A 997 Hz probe measures the imaging floor only where the arithmetic puts
  an image in band.** At ↑2, ↓2 and 2/3 the fundamental's images and
  aliases land above 20 kHz (↑2: 47 kHz; ↓2: none; 2/3: 31 kHz folded to
  1 kHz below Nyquist's mirror, outside the band), so every float row sits
  at the 24-bit ceiling (−144 to −147 dB) and the table cannot separate them.
  At 3/2 the image of 997 Hz lands at 17 kHz, inside the band, and the table
  becomes the stopband test it is elsewhere: `economy` −93 dB (its 70 dB
  design plus the probe's distance from the edge; −88 dB before X17's
  re-tuned β), r8brain's and libsamplerate's economy rows at −122 dB,
  SpeexDSP's quality 2 at −82 dB, `transparent` at −141 dB with the
  transparent-matched rows at the ceiling. The sweep above is the measurement that holds at every ratio;
  this table is its 997 Hz slice.
- **The 16-bit rows are format floors**, as in both sibling comparisons:
  `rational` economy Q15 and SpeexDSP's fixed-point build at quality 3 sit at
  the 16-bit interface's ~98–101 dB A-weighted DR everywhere and within 1 dB
  of each other in THD+N except at 3/2, where `economy`'s in-band image
  shows (−80 against −92 dB).
- **Dynamic range is at the ceiling for every float row** (152 dB at 96 kHz
  out, where the A-weighted band is a smaller share of Nyquist; 148–149 at
  48 and 32 kHz), `economy` at 3/2 half a dB under it for the image above.
- The notebook pins `rational`'s rows with assertions, so a regression fails
  the run.

### Latency

In milliseconds, from each engine's own accounting (`rational`: its exact
group delay in output frames; libsamplerate: input frames counted through its
streaming API before the first output; r8brain: `getInLenBeforeOutPos(0)`;
SpeexDSP: `get_input_latency()`; soxr: `soxr_delay()` after priming, in
output frames), each divided by its own rate:

| Engine | ↑2 (48 → 96) | ↓2 (96 → 48) | 3/2 (32 → 48) | 2/3 (48 → 32) |
|---|---:|---:|---:|---:|
| **rational** `economy` | **0.20 ms** | **0.20 ms** | **0.30 ms** | **0.30 ms** |
| **rational** `transparent` | **0.51 ms** | **0.51 ms** | **0.77 ms** | **0.77 ms** |
| libsamplerate FASTEST | 0.44 ms | 0.42 ms | 0.66 ms | 0.65 ms |
| libsamplerate MEDIUM (economy-matched) | 1.00 ms | 0.97 ms | 1.50 ms | 1.48 ms |
| libsamplerate BEST (transparent-matched) | 3.02 ms | 3.00 ms | 4.53 ms | 4.50 ms |
| soxr 16-bit @ 0.75 (economy-matched) | 9.00 ms | 4.50 ms | 2.00 ms | 4.33 ms |
| soxr 20-bit @ 0.80 (transparent-matched) | 5.08 ms | 7.00 ms | 7.38 ms | 4.90 ms |
| soxr VHQ | 6.38 ms | 11.08 ms | 24.88 ms | 16.58 ms |
| r8brain 70 dB @ 16 % (economy-matched) | 2.29 ms | 2.30 ms | 2.12 ms | 2.12 ms |
| r8brain 120 dB @ 10 % (transparent-matched) | 4.33 ms | 4.34 ms | 9.16 ms | 9.17 ms |
| r8brain CDSPResampler24 | 35.29 ms | 35.29 ms | 74.25 ms | 74.25 ms |
| SpeexDSP q2 (economy-matched) | 0.33 ms | 0.33 ms | 0.50 ms | 0.50 ms |
| SpeexDSP q9 (transparent-matched) | 2.00 ms | 2.00 ms | 3.00 ms | 3.00 ms |
| SpeexDSP q10 | 2.67 ms | 2.67 ms | 4.00 ms | 4.00 ms |

`rational`'s half-band at `economy` is 19 output frames at 96 kHz going up
and 9.5 at 48 kHz going down — 0.20 ms either way — and 0.30 ms for the
mixed stage; SpeexDSP's quality 2 is the one economy-matched competitor in
the same class (1.7×), libsamplerate `MEDIUM` ~5×, r8brain ~7–12×, soxr
7–45× (its delay varies with the ratio's FFT geometry). The frontier
settings are 13× (SpeexDSP) to 180–245× (`CDSPResampler24`).

## Computational cost, identical conditions

The embedded instruction counts below were measured after X17 re-tuned
`rational`'s designer (2026-11-20), in `compare.yml` run 45540985569 on
`f1816cc` (2026-11-25). The host wall-clock table was measured on `667934a`
(2026-10-10), before X17: the designs shipped since are shorter — the by-2
stage at `economy` 21 MACs per output against 23, at `transparent` 51
against 63 — so its `rational` rows are the longer designs' until the host
comparison bench is re-run.

Same engines, same task: convert a float 997 Hz stereo stream at exactly
2/1, 1/2, 3/2 or 2/3, streaming in 128-frame input blocks (`bench/compare/`,
`-DTAP_SR_BUILD_COMPARE_BENCH=ON`), at the matched settings above. r8brain
is mono per instance with double I/O, so the harness runs one instance per
channel and the float↔double (de)interleave is inside the timed loop — what
any float-interleaved caller pays to use it.

### Host wall-clock (x86, GCC 13.3 -O3 (CMake Release), shared Xeon @ 2.80 GHz, 2026-10-10)

Million output frames/s, stereo, median of 5 — relative ratios are the
meaningful figures on a shared machine (× = vs. the rational tier the row is
matched to; all subjects ran in the same session). libsamplerate 0.2.2 and
soxr 0.1.3 are the Ubuntu 24.04 packages; r8brain and SpeexDSP are the
pinned commits (7.6 and 1.2.1), stock configurations.

| Engine | ↑2 (48 → 96) | ↓2 (96 → 48) | 3/2 (32 → 48) | 2/3 (48 → 32) |
|---|---:|---:|---:|---:|
| **rational** `economy` | **51.0** | **26.2** | **42.7** | **21.2** |
| **rational** `transparent` | **23.1** | **11.0** | **22.7** | **9.8** |
| libsamplerate `FASTEST` (below the ladder) | 8.8 (0.2×) | 5.4 (0.2×) | 9.5 (0.2×) | 7.0 (0.3×) |
| libsamplerate `MEDIUM` (economy-matched) | 4.4 (0.1×) | 2.5 (0.1×) | 4.7 (0.1×) | 3.1 (0.1×) |
| soxr 16-bit @ 0.75 (economy-matched) | 167.8 (3.3×) | 85.3 (3.3×) | 101.2 (2.4×) | 55.9 (2.6×) |
| r8brain 70 dB @ 16 % (economy-matched) | 88.2 (1.7×) | 47.8 (1.8×) | 40.5 (0.9×) | 24.3 (1.1×) |
| SpeexDSP quality 2, float (economy-matched) | 43.5 (0.9×) | 21.7 (0.8×) | 48.7 (1.1×) | 29.8 (1.4×) |
| libsamplerate `BEST` (transparent-matched, frontier) | 1.4 (0.1×) | 0.8 (0.1×) | 1.5 (0.1×) | 1.1 (0.1×) |
| soxr 20-bit @ 0.80 (transparent-matched) | 149.1 (6.5×) | 82.2 (7.5×) | 91.3 (4.0×) | 52.9 (5.4×) |
| soxr `VHQ` (frontier) | 116.4 (5.0×) | 55.3 (5.0×) | 49.6 (2.2×) | 33.2 (3.4×) |
| r8brain 120 dB @ 10 % (transparent-matched) | 69.9 (3.0×) | 39.5 (3.6×) | 39.8 (1.8×) | 25.6 (2.6×) |
| r8brain `CDSPResampler24` (frontier) | 57.9 (2.5×) | 29.7 (2.7×) | 29.9 (1.3×) | 18.6 (1.9×) |
| SpeexDSP quality 9, float (transparent-matched) | 9.8 (0.4×) | 5.0 (0.5×) | 10.4 (0.5×) | 6.9 (0.7×) |
| SpeexDSP quality 10, float (frontier) | 7.6 (0.3×) | 3.9 (0.4×) | 7.8 (0.3×) | 5.0 (0.5×) |

| Fixed-point rows (Q15 I/O) | ↑2 (48 → 96) | ↓2 (96 → 48) | 3/2 (32 → 48) | 2/3 (48 → 32) |
|---|---:|---:|---:|---:|
| **rational** `economy` Q15 | **69.2** | **30.7** | **57.7** | **42.0** |
| SpeexDSP quality 3, fixed-point (economy-matched) | 107.4 (1.6×) | 62.0 (2.0×) | 105.2 (1.8×) | 70.5 (1.7×) |
| SpeexDSP quality 10, fixed-point (frontier) | 35.0 (0.5×) | 15.8 (0.5×) | 33.0 (0.6×) | 21.6 (0.5×) |

Reading guide:

- **The FFT engines win raw host throughput**, as in both sibling
  comparisons, but by less: soxr at 2.4–3.3× `economy` and r8brain at
  0.9–1.8× (r8brain's two-stage structure for the mixed pair costs it the
  lead there), against 2–17× over `bridge`'s longer polyphase. The latency
  column above is their price (6–40× for soxr's matched settings, 7–10× for
  r8brain's).
- **Against the other polyphase engines `rational` is the faster float
  engine by 7–12×** over libsamplerate's matched rows and about even with
  SpeexDSP's quality 2 (0.8–1.4×), the one competitor setting also on
  `economy`'s 70 dB.
- **On x86 SpeexDSP's fixed-point build out-runs `rational`'s Q15 stage by
  1.6–2×.** Its 16-bit build multiplies in 32-bit integer arithmetic that
  x86 vectorizes freely; `rational`'s Q15 dot accumulates in 64 bits by
  contract (DspTap's traits), which the host compiler does not pack. The
  embedded section is where that row's terms are decided, and there the Q15
  stage is the cheapest row at every ratio on every target.
- `economy` converts 51 M frames/s going up by 2 and 26 M going down; the
  up stages' centre phase is a copy, so they cost roughly half.

### Embedded executed instructions per output frame (QEMU TCG plugin)

Same comparison workload cross-compiled per target (`TAP_SR_ICOUNT_COMPARE`,
`.github/workflows/compare.yml`; deterministic counts, methodology as the
ratchet in the README). Stereo, float I/O (Q15 for the Q15 rows and
SpeexDSP's fixed-point build), 32-frame input blocks, four ratios.
libsamplerate 0.2.2, r8brain and SpeexDSP at their pinned commits (7.6 and
1.2.1; SpeexDSP from source in both arithmetic builds, `tools/compare/`);
arm-none-eabi-gcc 13.2.1, hexagon-clang 19.1.5, -O3 (CMake Release).

Every engine is built at 2 s and 4 s of input: the difference is the
**steady-state** cost per output frame, the remainder the **one-time
construction** (filter design, tables, FFT setup). The tables below are
generated from `bench/compare_counts.json`, the record `compare.yml` writes
(`scripts/harvest_compare.py --prefix cmp_rational_icount_`), by
`scripts/update_compare_docs.py`; CI regenerates them and fails on a diff,
so a DspTap pin bump that moves a count is a red check until the comparison
is re-run and re-harvested. The soxr rows are host-only: it is not
cross-compiled here.

<!-- COMPARE:BEGIN -->
Measured in `compare.yml` run
[45540985569](https://github.com/tap/SampleRateTap/actions/runs/45540985569)
on `f1816cc` (DspTap `4b30a02`), 2026-11-25; arm-none-eabi-gcc 15:13.2.rel1-2,
qemu-system-arm 1:8.2.2+ds-0ubuntu1.18 (ubuntu24 20261116.376.1),
hexagon-clang 19.1.5 and qemu-hexagon 8.2.2 at the pinned digests.

**↑2 (48 → 96 kHz).** Steady state, instructions per stereo output frame (× = vs. the
rational row the label names; the cheapest rational row per target in bold):

| Engine | Cortex-M55 | Cortex-M33 (Pico 2 class) | Hexagon |
|---|---:|---:|---:|
| **rational** economy, float | 192 | 3,263 | 604 |
| **rational** economy, Q15 | **134** | **179** | **87** |
| **rational** transparent, float | 373 | 7,902 | 1,394 |
| libsamplerate `MEDIUM` (economy-matched) | 2,177 (11.4×) | 42,566 (13.0×) | 9,380 (15.5×) |
| r8brain 70 dB, 16 % band (economy-matched)³ | 203 (1.1×) | 4,479 (1.4×) | 932 (1.5×) |
| SpeexDSP float, quality 2 (economy-matched) | 303 (1.6×) | 380 (0.1×) | 130 (0.2×) |
| SpeexDSP fixed-point, quality 3 (× vs. Q15) | 488 (3.6×) | 571 (3.2×) | 165 (1.9×) |
| libsamplerate `BEST` (transparent-matched; × vs. transparent) | 6,355 (17.0×) | 130,259 (16.5×) | 28,006 (20.1×) |
| r8brain 120 dB, 10 % band (transparent-matched; × vs. transparent)³ | 241 (0.6×) | 5,875 (0.7×) | 1,205 (0.9×) |
| SpeexDSP float, quality 9 (transparent-matched; × vs. transparent) | 2,293 (6.1×) | 33,056 (4.2×) | 5,647 (4.1×) |

One-time construction, millions of instructions:

| Engine | Cortex-M55 | Cortex-M33 | Hexagon |
|---|---:|---:|---:|
| **rational** economy, float | 1.4 | 23.8 | 7.9 |
| **rational** economy, Q15 | 1.5 | 26.4 | 8.3 |
| **rational** transparent, float | 1.4 | 24.6 | 8.0 |
| libsamplerate `MEDIUM` (economy-matched) | 1.3 | 19.2 | 7.0 |
| r8brain 70 dB, 16 % band (economy-matched) | 1.4 | 24.2 | 8.1 |
| SpeexDSP float, quality 2 (economy-matched) | 1.4 | 23.6 | 7.9 |
| SpeexDSP fixed-point, quality 3 | 1.5 | 26.4 | 8.3 |
| libsamplerate `BEST` (transparent-matched) | -0.1 | -16.5 | -0.0 |
| r8brain 120 dB, 10 % band (transparent-matched) | 1.5 | 25.5 | 8.3 |
| SpeexDSP float, quality 9 (transparent-matched) | 1.5 | 22.7 | 8.3 |

**↓2 (96 → 48 kHz).** Steady state, instructions per stereo output frame (× = vs. the
rational row the label names; the cheapest rational row per target in bold):

| Engine | Cortex-M55 | Cortex-M33 (Pico 2 class) | Hexagon |
|---|---:|---:|---:|
| **rational** economy, float | 457 | 6,651 | 1,221 |
| **rational** economy, Q15 | **319** | **392** | **180** |
| **rational** transparent, float | 822 | 15,932 | 2,807 |
| libsamplerate `MEDIUM` (economy-matched) | 4,174 (9.1×) | 83,674 (12.6×) | 18,223 (14.9×) |
| r8brain 70 dB, 16 % band (economy-matched)³ | 399 (0.9×) | 8,897 (1.3×) | 1,857 (1.5×) |
| SpeexDSP float, quality 2 (economy-matched) | 568 (1.2×) | 764 (0.1×) | 384 (0.3×) |
| SpeexDSP fixed-point, quality 3 (× vs. Q15) | 915 (2.9×) | 1,111 (2.8×) | 397 (2.2×) |
| libsamplerate `BEST` (transparent-matched; × vs. transparent) | 12,522 (15.2×) | 258,524 (16.2×) | 55,473 (19.8×) |
| r8brain 120 dB, 10 % band (transparent-matched; × vs. transparent)³ | 474 (0.6×) | 11,709 (0.7×) | 2,412 (0.9×) |
| SpeexDSP float, quality 9 (transparent-matched; × vs. transparent) | 4,567 (5.6×) | 65,150 (4.1×) | 11,927 (4.2×) |

One-time construction, millions of instructions:

| Engine | Cortex-M55 | Cortex-M33 | Hexagon |
|---|---:|---:|---:|
| **rational** economy, float | 2.8 | 47.2 | 15.7 |
| **rational** economy, Q15 | 3.0 | 52.4 | 16.4 |
| **rational** transparent, float | 2.8 | 48.0 | 15.8 |
| libsamplerate `MEDIUM` (economy-matched) | 2.7 | 42.8 | 14.8 |
| r8brain 70 dB, 16 % band (economy-matched) | 2.8 | 47.7 | 15.9 |
| SpeexDSP float, quality 2 (economy-matched) | 2.8 | 47.1 | 15.7 |
| SpeexDSP fixed-point, quality 3 | 3.0 | 52.4 | 16.5 |
| libsamplerate `BEST` (transparent-matched) | 1.3 | 7.5 | 7.9 |
| r8brain 120 dB, 10 % band (transparent-matched) | 2.9 | 48.9 | 16.1 |
| SpeexDSP float, quality 9 (transparent-matched) | 2.9 | 46.2 | 16.1 |

**3/2 (32 → 48 kHz).** Steady state, instructions per stereo output frame (× = vs. the
rational row the label names; the cheapest rational row per target in bold):

| Engine | Cortex-M55 | Cortex-M33 (Pico 2 class) | Hexagon |
|---|---:|---:|---:|
| **rational** economy, float | 236 | 4,247 | 776 |
| **rational** economy, Q15 | **147** | **206** | **103** |
| **rational** transparent, float | 477 | 10,440 | 1,829 |
| libsamplerate `MEDIUM` (economy-matched) | 2,195 (9.3×) | 46,071 (10.8×) | 9,277 (12.0×) |
| r8brain 70 dB, 16 % band (economy-matched)³ | 438 (1.9×) | 10,247 (2.4×) | 2,176 (2.8×) |
| SpeexDSP float, quality 2 (economy-matched) | 308 (1.3×) | 386 (0.1×) | 135 (0.2×) |
| SpeexDSP fixed-point, quality 3 (× vs. Q15) | 493 (3.4×) | 577 (2.8×) | 172 (1.7×) |
| libsamplerate `BEST` (transparent-matched; × vs. transparent) | 6,375 (13.4×) | 139,465 (13.4×) | 27,645 (15.1×) |
| r8brain 120 dB, 10 % band (transparent-matched; × vs. transparent)³ | 408 (0.9×) | 10,944 (1.0×) | 2,242 (1.2×) |
| SpeexDSP float, quality 9 (transparent-matched; × vs. transparent) | 2,299 (4.8×) | 33,403 (3.2×) | 5,689 (3.1×) |

One-time construction, millions of instructions:

| Engine | Cortex-M55 | Cortex-M33 | Hexagon |
|---|---:|---:|---:|
| **rational** economy, float | 1.0 | 16.2 | 5.3 |
| **rational** economy, Q15 | 1.0 | 18.0 | 5.6 |
| **rational** transparent, float | 1.0 | 17.5 | 5.5 |
| libsamplerate `MEDIUM` (economy-matched) | 0.9 | 12.3 | 4.6 |
| r8brain 70 dB, 16 % band (economy-matched) | 1.0 | 16.7 | 5.5 |
| SpeexDSP float, quality 2 (economy-matched) | 1.0 | 16.0 | 5.3 |
| SpeexDSP fixed-point, quality 3 | 1.0 | 18.0 | 5.6 |
| libsamplerate `BEST` (transparent-matched) | -0.2 | -16.2 | -0.5 |
| r8brain 120 dB, 10 % band (transparent-matched) | 1.0 | 19.1 | 6.1 |
| SpeexDSP float, quality 9 (transparent-matched) | 1.1 | 16.4 | 5.9 |

**2/3 (48 → 32 kHz).** Steady state, instructions per stereo output frame (× = vs. the
rational row the label names; the cheapest rational row per target in bold):

| Engine | Cortex-M55 | Cortex-M33 (Pico 2 class) | Hexagon |
|---|---:|---:|---:|
| **rational** economy, float | 450 | 8,035 | 1,684 |
| **rational** economy, Q15 | **199** | **332** | **170** |
| **rational** transparent, float | 996 | 20,072 | 4,189 |
| libsamplerate `MEDIUM` (economy-matched) | 3,194 (7.1×) | 73,202 (9.1×) | 13,293 (7.9×) |
| r8brain 70 dB, 16 % band (economy-matched)³ | 684 (1.5×) | 16,307 (2.0×) | 3,297 (2.0×) |
| SpeexDSP float, quality 2 (economy-matched) | 443 (1.0×) | 579 (0.1×) | 265 (0.2×) |
| SpeexDSP fixed-point, quality 3 (× vs. Q15) | 709 (3.6×) | 849 (2.6×) | 284 (1.7×) |
| libsamplerate `BEST` (transparent-matched; × vs. transparent) | 9,457 (9.5×) | 223,356 (11.1×) | 40,069 (9.6×) |
| r8brain 120 dB, 10 % band (transparent-matched; × vs. transparent)³ | 640 (0.6×) | 17,174 (0.9×) | 3,412 (0.8×) |
| SpeexDSP float, quality 9 (transparent-matched; × vs. transparent) | 3,437 (3.5×) | 50,584 (2.5×) | 8,790 (2.1×) |

One-time construction, millions of instructions:

| Engine | Cortex-M55 | Cortex-M33 | Hexagon |
|---|---:|---:|---:|
| **rational** economy, float | 1.4 | 24.1 | 7.9 |
| **rational** economy, Q15 | 1.5 | 26.7 | 8.3 |
| **rational** transparent, float | 1.5 | 25.4 | 8.1 |
| libsamplerate `MEDIUM` (economy-matched) | 1.3 | 19.9 | 7.2 |
| r8brain 70 dB, 16 % band (economy-matched) | 1.4 | 24.5 | 8.1 |
| SpeexDSP float, quality 2 (economy-matched) | 1.4 | 23.8 | 7.9 |
| SpeexDSP fixed-point, quality 3 | 1.5 | 26.7 | 8.4 |
| libsamplerate `BEST` (transparent-matched) | 0.3 | -10.3 | 2.3 |
| r8brain 120 dB, 10 % band (transparent-matched) | 1.5 | 27.1 | 8.7 |
| SpeexDSP float, quality 9 (transparent-matched) | 1.5 | 24.2 | 8.5 |

Key figures (↓2, 96 → 48): on the M33 the economy Q15 half-band costs **392
instructions/frame** in steady state, SpeexDSP's fixed-point build at quality
3 **2.8×** that, libsamplerate `MEDIUM` **~213×** and r8brain at 70 dB
**~23×**; on the M55 the economy float half-band (457) is dearer than Q15
(319) by 43%.
<!-- COMPARE:END -->

**The Q15 stage is the cheapest row at every ratio on every target.**
Economy in Q15 costs 134–319 instructions per stereo output frame on the
M55, 179–392 on the M33 and 87–180 on Hexagon: the half-band's structural
zeros are never multiplied (21 MACs of its 39 taps going down) and the
Helium dot takes the rest. Against it, SpeexDSP's fixed-point build at
quality 3 — the one competitor that runs on the Q15 row's terms — costs
1.7–3.6×, and the float libraries are a different order: r8brain's
economy-matched setting 23–50× on the M33, libsamplerate `MEDIUM` 210–240×.

**In float the Nyquist stage holds its own against r8brain's FFT, which
`bridge`'s 58-tap polyphase did not.** r8brain's economy-matched setting
costs 0.9–1.1× `economy` on the M55 at the by-2 pair, where its block
convolution amortizes best, and 1.3–2.8× everywhere else: 1.3–1.5× on the
soft-double M33 and Hexagon at by 2, 1.5–2.8× at the mixed pair on every
target, where r8brain's two-stage structure for 3/2 and 2/3 costs it. At
`transparent` r8brain is 0.6–0.9× at by 2 and 0.6–1.2× at the mixed pair;
the short L-th-band designs (99 taps by 2 and 149 at the mixed pair at
120 dB) leave FFT convolution less to win. libsamplerate `MEDIUM` costs
7–16× `economy` and its `BEST` 10–20× `transparent`, consistently across
targets. SpeexDSP's single-precision float build at quality 2 costs
1.0–1.6× `economy` on the M55 and, as in both sibling comparisons, undercuts
the double-accumulating float row by an order of magnitude on the
soft-double targets while still costing 1.3–2.1× the Q15 row; it is the one
competitor row near `economy` in cost and in stopband (70–76 dB), at 1.5×
the latency.

**Construction is a wash here, and mostly fixture.** Every row's one-time
figure is within a few hundred thousand instructions of the others at the
same ratio (1.0–3.0 M on the M55, 16–53 M on the M33), because the workload
synthesizes its 0.25 s input fixture with libm `sin()` at the input rate
and that is most of the 2 × (2 s) − (4 s) difference; `rational`'s own
design — one Kaiser prototype of 39 (by 2) to 59 (the mixed pair) taps —
is the fraction of a million above libsamplerate's row. The sibling engines' construction
columns carry the same fixture and are dominated by their designs instead
(`bridge` 98–437 M on the M33); a chain of stages pays this once per stage.

³ r8brain guards its process-wide filter cache with `std::mutex` and has no
hook to replace it; the thread-less arm-none-eabi newlib declares none, so
the Cortex-M builds force-include `tools/compare/r8b_single_thread_mutex.h`
(a no-op lock — exact for this single-threaded workload, and outside the
per-sample path). Hexagon's musl build uses the real mutex.

⁴ libsamplerate `BEST`'s construction cells are below the split's
resolution and land negative (down to −16 M on the M33): that engine's
per-frame cost drifts over the run by more than its construction, so
2 × (2 s) − (4 s) measures the drift, not a design. Read them as ~0.

## Caveats, stated plainly

- **The competitors are frozen at their 2026-10-09 pins.** libsamplerate
  0.2.2, r8brain-free-src 7.6 and SpeexDSP 1.2.1 as `tools/compare/` pins
  them; a newer release of any of them is not tracked. Moving a pin re-runs
  `compare.yml` and the notebook and re-dates every table here
  ([the family comparison](../../docs/COMPARISON.md#the-shared-tooling)).
- **The matching rule is ours**, the same two numbers as the bridge
  comparison's (0.1 dB at the edge, stopband at least the tier's), applied
  across four ratios at once: a setting that passes three and fails one is
  not matched. The notebook prints the whole sweep so a per-ratio reading
  can be taken from it.
- **The 997 Hz table is a slice.** Where no image of the probe lands in
  band, it measures the 24-bit ceiling and nothing about the filter; the
  sweep's stopband and droop numbers are the contract measurement.
- **soxr is measured on the host only** (no cross-compiled row), and its
  latency is reported in output frames by its own API.
- **Software rows regenerate by re-running the notebook**, which asserts
  `rational`'s own numbers and the matched picks; the embedded tables
  regenerate from the committed record, under CI's freshness gate.
