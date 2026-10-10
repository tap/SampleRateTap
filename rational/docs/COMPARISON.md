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

From the notebook (executed 2026-10-10; one AES17-style measurement
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
| **rational** `economy` | **-146.6 dB** | **-144.1 dB** | **-88.0 dB** | **-143.5 dB** |
| **rational** `balanced` | -146.7 dB | -144.0 dB | -86.5 dB | -143.4 dB |
| **rational** `transparent` | **-146.7 dB** | **-143.9 dB** | **-139.7 dB** | **-143.5 dB** |
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
| **rational** `economy` (Q15) | -96.9 dB | -97.2 dB | -81.2 dB | -94.9 dB |
| **rational** `transparent` (Q15) | -96.9 dB | -95.8 dB | -83.1 dB | -95.3 dB |
| SpeexDSP q3 fixed-point | -96.1 dB | -96.3 dB | -91.9 dB | -95.2 dB |
| SpeexDSP q10 fixed-point | -96.2 dB | -96.4 dB | -89.6 dB | -95.3 dB |

Dynamic range (−60 dBFS, A-weighted, 24-bit interface), the rows that differ:

| Subject | ↑2 (48 → 96) | ↓2 (96 → 48) | 3/2 (32 → 48) | 2/3 (48 → 32) |
|---|---:|---:|---:|---:|
| **rational** `economy` | 152.1 dB | 149.0 dB | 148.2 dB | 147.7 dB |
| **rational** `transparent` | 152.1 dB | 149.0 dB | 149.1 dB | 147.7 dB |
| libsamplerate BEST (transparent-matched) | 152.2 dB | 149.1 dB | 149.1 dB | 147.6 dB |
| **rational** `economy` (Q15) | 99.9 dB | 100.6 dB | 98.3 dB | 98.0 dB |
| SpeexDSP q3 fixed-point | 99.6 dB | 100.6 dB | 98.1 dB | 97.9 dB |

Reading guide:

- **A 997 Hz probe measures the imaging floor only where the arithmetic puts
  an image in band.** At ↑2, ↓2 and 2/3 the fundamental's images and
  aliases land above 20 kHz (↑2: 47 kHz; ↓2: none; 2/3: 31 kHz folded to
  1 kHz below Nyquist's mirror, outside the band), so every float row sits
  at the 24-bit ceiling (−144 to −147 dB) and the table cannot separate them.
  At 3/2 the image of 997 Hz lands at 17 kHz, inside the band, and the table
  becomes the stopband test it is elsewhere: `economy` −88 dB (its 70 dB
  design plus the probe's distance from the edge), r8brain's and
  libsamplerate's economy rows at −122 dB, SpeexDSP's quality 2 at −82 dB,
  `transparent` at −140 dB with the transparent-matched rows at the
  ceiling. The sweep above is the measurement that holds at every ratio;
  this table is its 997 Hz slice.
- **The 16-bit rows are format floors**, as in both sibling comparisons:
  `rational` economy Q15 and SpeexDSP's fixed-point build at quality 3 sit at
  the 16-bit interface's ~98–101 dB A-weighted DR everywhere and within 1 dB
  of each other in THD+N except at 3/2, where `economy`'s in-band image
  shows (−81 against −92 dB).
- **Dynamic range is at the ceiling for every float row** (152 dB at 96 kHz
  out, where the A-weighted band is a smaller share of Nyquist; 148–149 at
  48 and 32 kHz), `economy` at 3/2 one dB under it for the image above.
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
| **rational** `economy` | **0.22 ms** | **0.22 ms** | **0.33 ms** | **0.33 ms** |
| **rational** `transparent` | **0.64 ms** | **0.64 ms** | **0.77 ms** | **0.77 ms** |
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

`rational`'s half-band at `economy` is 21 output frames at 96 kHz going up
and 10.5 at 48 kHz going down — 0.22 ms either way — and 0.33 ms for the
mixed stage; SpeexDSP's quality 2 is the one economy-matched competitor in
the same class (1.5×), libsamplerate `MEDIUM` ~4.5×, r8brain ~7–10×, soxr
6–40× (its delay varies with the ratio's FFT geometry). The frontier
settings are 10× (SpeexDSP) to 160–225× (`CDSPResampler24`).

## Computational cost, identical conditions

Same engines, same task: convert a float 997 Hz stereo stream at exactly
2/1, 1/2, 3/2 or 2/3, streaming in 128-frame input blocks (`bench/compare/`,
`-DTAP_SR_BUILD_COMPARE_BENCH=ON`), at the matched settings above. r8brain
is mono per instance with double I/O, so the harness runs one instance per
channel and the float↔double (de)interleave is inside the timed loop — what
any float-interleaved caller pays to use it.

<!-- HOST:BEGIN -->
(host table pending)
<!-- HOST:END -->

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
<!-- COMPARE:END -->

(reading pending)

³ r8brain guards its process-wide filter cache with `std::mutex` and has no
hook to replace it; the thread-less arm-none-eabi newlib declares none, so
the Cortex-M builds force-include `tools/compare/r8b_single_thread_mutex.h`
(a no-op lock — exact for this single-threaded workload, and outside the
per-sample path). Hexagon's musl build uses the real mutex.

## Caveats, stated plainly

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
