# bridge vs. other sample rate converters

The async engine's comparison ([`async/docs/COMPARISON.md`](../../async/docs/COMPARISON.md))
asks what clock recovery costs, and its landscape table places the whole
family among hardware ASRCs, OS engines and the resampler libraries. This
document asks the synchronous question at the one ratio pair `bridge` exists
for: **44.1 ↔ 48 kHz, both directions, on one clock.** Every library below
converts exactly that ratio — no servo, no oracle asymmetry — so this is the
cleanest head-to-head the family has: engine against engine on the same
number.

## The matching rule

`bridge` ships two tiers that matter here: `economy` (70 dB stopband, 18 kHz
passband; the speed-first default) and `transparent` (120 dB, 20 kHz). The
competitors are measured at **matched spec** and at their **frontier**:

- A library's *matched* setting for a tier is its cheapest whose measured
  stopband is at least the tier's and whose passband droops no more than
  0.1 dB at the tier's edge, in **both** directions. The sweep in
  [notebooks/bridge_comparison.ipynb](../notebooks/bridge_comparison.ipynb)
  measures every candidate (passband gain at 18 and 20 kHz, worst alias or
  image product at the exact frequency the arithmetic puts it) and asserts
  the picks against the constants the benches compile in.
- Its *frontier* is its best setting.

| Tier | libsamplerate | soxr | r8brain-free-src | SpeexDSP |
|---|---|---|---|---|
| `economy` (70 dB, 18 kHz) | `SINC_MEDIUM` (its `FASTEST` droops 4 dB at 18 kHz) | 16-bit precision at a 0.816 passband (its lowest precision) | 70 dB at a 12 % band | quality 3 (both builds) |
| `transparent` (120 dB, 20 kHz) | `SINC_BEST` | `HQ` (20-bit) | 120 dB at a 6 % band | quality 9 (float; the fixed-point build cannot reach 120 dB) |
| frontier | `SINC_BEST` | `VHQ` | `CDSPResampler24` (180.15 dB) | quality 10 |

One finding falls out of the rule before any cost is measured: **r8brain is
the one competitor that offers `economy`'s design point.** Its 70 dB setting
at a 12 % band measures the same 71–72 dB worst product and −88 dB THD+N as
`economy`. The others cannot decline the attenuation: libsamplerate's
cheapest flat-to-18 kHz converter is `MEDIUM` at 123 dB, soxr's lowest
precision is 16-bit (~110 dB), SpeexDSP's quality 3 is 85 dB. The cost
tables below say what each pays for it.

## Measured, identical conditions

From the notebook (executed 2026-10-09; one AES17-style measurement
implementation applied to every subject — 997 Hz at −1 dBFS, fundamental
removed by exact fit + ±20 Hz notch, residual integrated 20 Hz–20 kHz; DR at
−60 dBFS, A-weighted; the **24-bit interface** columns quantize each
subject's output to 24 bits; the instrument is calibrated in-notebook at
both output rates). Every subject through the shipping C++ (the C ABI) or
the real library (libsamplerate's Python binding; soxr, r8brain and SpeexDSP
through the family's C shims, `tools/compare/shim/`).

| Subject | THD+N 48 → 44.1 (24-bit IO) | THD+N 44.1 → 48 (24-bit IO) | DR A-wtd 48 → 44.1 | DR A-wtd 44.1 → 48 |
|---|---:|---:|---:|---:|
| **bridge** `economy` (float) | **−91.4 dB** | **−86.2 dB** | 146.7 dB | 144.0 dB |
| **bridge** `balanced` (float) | −89.3 dB | −91.6 dB | 145.9 dB | 146.9 dB |
| **bridge** `transparent` (float) | **−141.7 dB** | **−142.9 dB** | 148.8 dB | 149.1 dB |
| libsamplerate `FASTEST` | −103.4 dB | −119.0 dB | 148.7 dB | 149.0 dB |
| libsamplerate `MEDIUM` (economy-matched) | −135.1 dB | −140.1 dB | 148.7 dB | 149.1 dB |
| libsamplerate `BEST` (transparent-matched, frontier) | −143.2 dB | −143.5 dB | 148.7 dB | 149.1 dB |
| soxr 16-bit @ 0.816 (economy-matched) | −115.4 dB | −115.1 dB | 148.8 dB | 149.2 dB |
| soxr `HQ` (transparent-matched) | −134.3 dB | −134.4 dB | 148.8 dB | 149.1 dB |
| soxr `VHQ` (frontier) | −143.5 dB | −143.8 dB | 148.7 dB | 149.1 dB |
| r8brain 70 dB @ 12 % (economy-matched) | −88.5 dB | −87.9 dB | 146.1 dB | 146.0 dB |
| r8brain 120 dB @ 6 % (transparent-matched) | −132.7 dB | −131.9 dB | 148.8 dB | 149.1 dB |
| r8brain `CDSPResampler24` (frontier) | −143.5 dB | −143.8 dB | 148.7 dB | 149.1 dB |
| SpeexDSP quality 3, float (economy-matched) | −104.5 dB | −104.4 dB | 148.7 dB | 149.1 dB |
| SpeexDSP quality 9, float (transparent-matched) | −131.2 dB | −133.5 dB | 148.8 dB | 149.1 dB |
| SpeexDSP quality 10, float (frontier) | −137.2 dB | −136.7 dB | 148.8 dB | 149.1 dB |
| **bridge** `economy` (Q15) | −77.4 dB | −78.9 dB | 97.8 dB | 97.9 dB |
| **bridge** `transparent` (Q15) | −73.4 dB | −77.5 dB | 97.8 dB | 97.9 dB |
| SpeexDSP quality 3, fixed-point (economy-matched) | −86.4 dB | −89.9 dB | 97.8 dB | 97.8 dB |
| SpeexDSP quality 10, fixed-point (frontier) | −77.9 dB | −79.3 dB | 97.8 dB | 97.8 dB |

Reading guide:

- **`economy` measures its design.** −91 / −86 dB THD+N is the imaging
  floor a 70 dB prototype leaves under a 997 Hz probe, 2–5 dB of A-weighted
  DR under the 24-bit ceiling for the same reason; r8brain's economy-matched
  setting, the one true 70 dB competitor, lands within 3 dB of it in both
  directions. Everything measured above −100 dB here is a library spending
  attenuation `bridge`'s charter chose not to.
- **`transparent` and the frontier settings all measure at the 24-bit
  ceiling** (−142 to −144 dB; the ceiling is −143.5); the 120 dB-class
  transparent-matched rows sit 8–10 dB above it. `transparent`'s −121 dB
  design figure bounds alias and image products, which a 997 Hz probe does
  not excite inside the integration band — the notebook's sweep is where
  that contract is measured, and it holds (−121 / −128 dB worst product).
- **The 16-bit rows are format floors**, as the async comparison found for
  its Q15 row: `bridge` economy Q15 and SpeexDSP's fixed-point build at
  quality 3 share the 16-bit interface's ~98 dB A-weighted DR and sit within
  10 dB in THD+N. `transparent` in Q15 is no better than `economy` in Q15,
  which is why the README pairs Q15 with the economy-class tiers.
- The notebook pins `bridge`'s rows with assertions, so a regression fails
  the run.

### Latency

Input frames consumed before the first output frame (milliseconds at the
input rate); soxr reports its delay in *output* frames after priming
(`soxr_delay()`), marked *.

| Engine | 48 → 44.1 | 44.1 → 48 |
|---|---:|---:|
| **bridge** `economy` | **29 (0.60 ms)** | **19 (0.43 ms)** |
| **bridge** `transparent` | 92 (1.92 ms) | 48 (1.09 ms) |
| libsamplerate `FASTEST` | 23 (0.48 ms) | 21 (0.48 ms) |
| libsamplerate `MEDIUM` (economy-matched) | 52 (1.08 ms) | 48 (1.09 ms) |
| libsamplerate `BEST` (transparent-matched) | 158 (3.29 ms) | 145 (3.29 ms) |
| soxr 16-bit @ 0.816 (economy-matched)* | 226 (5.1 ms) | 209 (4.4 ms) |
| soxr `HQ` (transparent-matched)* | 818 (18.6 ms) | 892 (18.6 ms) |
| soxr `VHQ`* | 555 (12.6 ms) | 392 (8.2 ms) |
| r8brain 70 dB @ 12 % (economy-matched) | 105 (2.19 ms) | 107 (2.43 ms) |
| r8brain 120 dB @ 6 % (transparent-matched) | 429 (8.94 ms) | 436 (9.89 ms) |
| r8brain `CDSPResampler24` | 1,668 (34.8 ms) | 1,700 (38.6 ms) |
| SpeexDSP quality 3 (economy-matched) | 28 (0.58 ms) | 24 (0.54 ms) |
| SpeexDSP quality 9 (transparent-matched) | 104 (2.17 ms) | 96 (2.18 ms) |
| SpeexDSP quality 10 | 140 (2.92 ms) | 128 (2.90 ms) |

Among the economy-matched competitors SpeexDSP's quality 3 is at `economy`'s
latency, libsamplerate `MEDIUM` ~2×, r8brain ~4×, soxr ~8×; the frontier
settings are 19× (soxr `VHQ`) to 57× (`CDSPResampler24`). r8brain's and
soxr's delays come from FFT block convolution, which is also where their
host throughput comes from (next section).

## Computational cost, identical conditions

Same engines, same task: convert a float 997 Hz stereo stream at exactly
147/160 (or 160/147), streaming in 128-frame input blocks
(`bench/compare/`, `-DTAP_SR_BUILD_COMPARE_BENCH=ON`), at the matched
settings above. r8brain is mono per instance with double I/O, so the harness
runs one instance per channel and the float↔double (de)interleave is inside
the timed loop — what any float-interleaved caller pays to use it.

### Host wall-clock (x86, GCC 13.3 -O3 (CMake Release), shared Xeon @ 2.80 GHz, 2026-10-09)

Million output frames/s, stereo, median of 5 — relative ratios are the
meaningful figures on a shared machine (× = vs. the bridge tier the row is
matched to; all subjects ran in the same session). libsamplerate 0.2.2 and
soxr 0.1.3 are the Ubuntu 24.04 packages; r8brain and SpeexDSP are the
pinned commits (7.6 and 1.2.1), stock configurations.

| Engine | 48 → 44.1 | 44.1 → 48 |
|---|---:|---:|
| **bridge** `economy` | **8.4** | **13.0** |
| **bridge** `transparent` | **2.4** | **4.9** |
| libsamplerate `FASTEST` (below the ladder) | 6.2 (0.7×) | 6.5 (0.5×) |
| libsamplerate `MEDIUM` (economy-matched) | 2.9 (0.3×) | 3.1 (0.2×) |
| soxr 16-bit @ 0.816 (economy-matched) | 38.0 (4.5×) | 42.4 (3.3×) |
| r8brain 70 dB @ 12 % (economy-matched) | 18.6 (2.2×) | 19.6 (1.5×) |
| SpeexDSP quality 3, float (economy-matched) | 2.7 (0.3×) | 2.7 (0.2×) |
| libsamplerate `BEST` (transparent-matched, frontier) | 0.7 (0.3×) | 0.7 (0.1×) |
| soxr `HQ` (transparent-matched) | 39.9 (16.7×) | 41.8 (8.5×) |
| soxr `VHQ` (frontier) | 25.0 (10.5×) | 33.1 (6.7×) |
| r8brain 120 dB @ 6 % (transparent-matched) | 18.3 (7.7×) | 22.2 (4.5×) |
| r8brain `CDSPResampler24` (frontier) | 15.8 (6.6×) | 15.8 (3.2×) |
| SpeexDSP quality 9, float (transparent-matched) | 1.0 (0.4×) | 1.2 (0.2×) |
| SpeexDSP quality 10, float (frontier) | 0.8 (0.4×) | 0.9 (0.2×) |

| Fixed-point rows (Q15 I/O) | 48 → 44.1 | 44.1 → 48 |
|---|---:|---:|
| **bridge** `economy` Q15 | **15.5** | **23.4** |
| SpeexDSP quality 3, fixed-point (economy-matched) | 4.8 (0.3×) | 5.1 (0.2×) |
| SpeexDSP quality 10, fixed-point (frontier) | 1.1 (0.1×) | 1.1 (0.0×) |

Reading guide:

- **The FFT engines win raw host throughput**, as in the async comparison:
  soxr at 3–17× `bridge`'s matched tier, r8brain at 1.5–8×, batch convolution
  amortizing on a desktop core. Their latency column above is the price
  (soxr's economy-matched setting at ~8× `economy`'s delay, its `HQ` at ~30×;
  r8brain's matched settings at 4–15×), and on the embedded targets the
  float ranking narrows to r8brain alone (next section).
- **Against the other polyphase engines `bridge` is the faster one by
  3–5×**: libsamplerate `MEDIUM` at 0.2–0.3× `economy`, `BEST` at 0.1–0.3×
  `transparent`, SpeexDSP's float build at 0.2–0.4× its matched tier. The
  fixed-point rows separate further: `bridge` economy Q15 converts 3–5× what
  SpeexDSP's fixed-point build does at the matched quality and 15–20× its
  frontier.
- Up-conversion is cheaper for `bridge` than down (38 taps against 58 at
  economy, 96 against 184 at transparent); the libraries are closer to
  symmetric.

### Embedded executed instructions per output frame (QEMU TCG plugin)

Same comparison workload cross-compiled per target (`TAP_SR_ICOUNT_COMPARE`,
`.github/workflows/compare.yml`; deterministic counts, methodology as the
ratchet in the README). Stereo, float I/O (Q15 for the Q15 rows and
SpeexDSP's fixed-point build), 32-frame input blocks, both directions.
libsamplerate 0.2.2, r8brain and SpeexDSP at their pinned commits (7.6 and
1.2.1; SpeexDSP from source in both arithmetic builds, `tools/compare/`);
arm-none-eabi-gcc 13.2.1, hexagon-clang 19.1.5, -O3 (CMake Release).

Every engine is built at 2 s and 4 s of input: the difference is the
**steady-state** cost per output frame, the remainder the **one-time
construction** (filter design, tables, FFT setup). The tables below are
generated from `bench/compare_counts.json`, the record `compare.yml` writes
(`scripts/harvest_compare.py --prefix cmp_bridge_icount_`), by
`scripts/update_compare_docs.py`; CI regenerates them and fails on a diff,
so a DspTap pin bump that moves a count is a red check until the comparison
is re-run and re-harvested. The soxr rows are host-only: it is not
cross-compiled here.

<!-- COMPARE:BEGIN -->
<!-- COMPARE:END -->

**The Q15 converter is the cheapest row on every target, and the float
rows are not.** Economy in Q15 costs 209 instructions per stereo output
frame on the M55, 419 on the M33 and 235 on Hexagon going down (164 / 305 /
165 going up): 3× under r8brain's economy-matched setting on the M55 and
13–34× under it on the two soft-double targets, where r8brain's
double-precision datapath pays for every multiply in software. SpeexDSP's
fixed-point build, the one competitor that can run there on the Q15 row's
terms, costs 2.3–7× it. That is the row FPU-less and Bluetooth-class
deployments run, and it is where `bridge`'s charter is cashed.

**In float, r8brain's FFT block convolution undercuts `bridge`'s direct-form
polyphase at both tiers on every target**: 0.8–1.0× economy's cost at its
economy-matched setting, 0.3–0.6× transparent's at its transparent-matched
one. A 58-tap (economy) or 184-tap (transparent) dot product per output
sample is more multiplies than an FFT of the same bandwidth, and on the
soft-double M33 and Hexagon `bridge`'s float path accumulates in double by
contract, so the comparison is soft-double against soft-double and the
multiply count decides it. r8brain pays in latency (105–436 input frames
against 29) and in construction, never in the per-frame count. SpeexDSP's
single-precision float build tells the same story as in the async
comparison: on the two soft-double targets it undercuts the float row by an
order of magnitude and still costs 2.3–7× the Q15 row. The multistage and
FFT levers `PLAN.md` section 7 defers "until a consumer pulls them" are
exactly what would move the float rows; this table is the measurement that
says by how much. libsamplerate's economy-matched `MEDIUM` costs 3–4×
economy float and its `BEST` 3–5× transparent float, consistently across
targets: the same architecture, more taps.

**Construction is the one column `bridge` loses outright**, as the async
engine does: its prototype design runs in double at construction, 98–131 M
instructions on the M33 for economy and 256–437 M for transparent (seconds
on a 150 MHz core without an FP64 unit; the RP2350's DCP shortens it),
against 19–54 M for the libraries. Paid once per converter, never on the
audio path; a device that constructs at boot should know.

³ r8brain guards its process-wide filter cache with `std::mutex` and has no
hook to replace it; the thread-less arm-none-eabi newlib declares none, so
the Cortex-M builds force-include `tools/compare/r8b_single_thread_mutex.h`
(a no-op lock — exact for this single-threaded workload, and outside the
per-sample path). Hexagon's musl build uses the real mutex.

⁴ A small negative construction (libsamplerate `BEST` on the M33) is the
split's resolution, not a measurement: that engine's per-frame cost drifts
by a few parts in a thousand over the run, more than its sub-million
construction, so 2 × (2 s) − (4 s) lands just below zero. Read it as ~0.

## Caveats, stated plainly

- **The matching rule is ours.** 0.1 dB at the tier's edge and "stopband at
  least the tier's" are the two numbers that decide every matched row; the
  notebook prints the whole sweep so a different rule can be read off it.
- **The 70 dB tier has one true competitor.** Against `economy`, three of
  the four libraries are measured at settings that overshoot its spec by
  15–50 dB because they offer nothing closer; their rows are the cheapest
  each library can be while keeping `economy`'s passband, not a like-for-like
  design.
- **soxr is measured on the host only** (no cross-compiled row), and its
  latency is reported in output frames by its own API.
- **Software rows regenerate by re-running the notebook**, which asserts
  `bridge`'s own numbers and the matched picks; the embedded tables
  regenerate from the committed record, under CI's freshness gate.
