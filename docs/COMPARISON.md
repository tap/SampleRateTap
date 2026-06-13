# SampleRateTap vs. other sample rate converters

Two different kinds of product get called an "SRC": **full ASRCs** that
recover the clock ratio themselves (hardware chips, OS audio engines,
SampleRateTap), and **resampler libraries** that must be handed the ratio by
an external servo (libsamplerate, soxr, zita-resampler). The second group
solves only half of the drift problem.

## Measured, identical conditions (software subjects)

From [notebooks/asrc_comparison.ipynb](../notebooks/asrc_comparison.ipynb)
(2026-06-11): one AES17-style measurement implementation applied to every
subject — 997 Hz at −1 dBFS across a +200 ppm clock crossing
(48 009.6 → 48 000 Hz), fundamental removed by exact fit + ±20 Hz notch,
residual integrated 20 Hz–20 kHz; DR per AES17 (−60 dBFS, A-weighted). The
**24-bit interface** columns quantize each subject's output to 24 bits —
the condition under which software numbers are directly comparable to
hardware datasheets, since that is the interface silicon presents. The
measurement instrument is calibrated in-notebook against known synthetic
signals before use.

| Subject | Clock knowledge | THD+N (24-bit IO) | THD+N (float IO) | DR A-wtd (24-bit IO) |
|---|---|---:|---:|---:|
| **SampleRateTap** (balanced, float) | **recovered by servo** | **−132.1 dB** | −132.3 dB | **149.1 dB** |
| libsamplerate `sinc_best` | given exact ratio (oracle) | −143.5 dB | −149.4 dB | 149.1 dB |
| soxr `VHQ` | given exact ratio (oracle) | −143.8 dB | −150.8 dB | 149.1 dB |
| naive FIFO (drop on full) | n/a | −34.7 dB | −34.7 dB | 94.7 dB |

Reading guide:

- The oracle-fed libraries measure at the *format ceilings* (float32 I/O
  ≈ −150 dB; 24-bit ≈ −143.5 dB; A-weighted 24-bit DR ceiling = 149.1 dB).
  Near-unity is their easy regime — libsamplerate's published "97 dB worst
  case" applies to aggressive ratios, not this one.
- SampleRateTap's −132 dB includes the entire problem: the servo discovered
  the ratio from FIFO occupancy and the conversion ran causally at 1.5 ms
  latency. The ~11 dB to the oracle libraries is the measured price of
  clock recovery + real-time operation — the part of the problem the
  libraries do not solve.
- The naive FIFO row is the cost of doing nothing.

## Computational cost, identical conditions (software subjects)

Same engines, same task: convert a float 997 Hz stereo stream at the fixed,
known near-unity ratio 1 + 200 ppm, streaming in 128-frame blocks
(`bench/compare/`, `-DSRT_BUILD_COMPARE_BENCH=ON`). SampleRateTap runs its
datapath with a constant rate deviation (the servo is quiescent at a fixed
ratio); the libraries take the ratio as an input. Quality tiers are paired
by vendor-stated stopband: balanced ≈ `MEDIUM` ≈ `HQ` (~120 dB),
transparent ≈ `BEST` ≈ `VHQ` (~140 dB+). Latency figures are measured:
SampleRateTap's is the filter group delay, libsamplerate's the input
buffered before its first streaming output, soxr's via `soxr_delay()`.

### Host wall-clock (x86, GCC 13.3 -O2, shared Xeon @ 2.10 GHz, 2026-06-12)

Million output frames/s — relative ratios are the meaningful figures on a
shared machine; all subjects ran in the same session.

| Engine (~120 dB tier) | mono | stereo | 8-ch | algorithmic latency |
|---|---:|---:|---:|---:|
| **SampleRateTap** balanced | 15.6 | 10.5 | 3.0 | **24 frames (0.50 ms)** |
| libsamplerate `MEDIUM` (0.2.2) | 4.4 | 3.7 | 1.4 | 46 frames (0.96 ms) |
| soxr `HQ` (0.1.3) | 72.9 | 32.4 | 8.4 | 556–607 frames (11.6–12.6 ms) |

| Engine (~140 dB tier) | stereo | algorithmic latency |
|---|---:|---:|
| **SampleRateTap** transparent | 5.8 | 40 frames (0.83 ms) |
| libsamplerate `BEST` | 0.9 | 143 frames (3.0 ms) |
| soxr `VHQ` | 22.2 | 777 frames (16.2 ms) |

| No competitor analog | stereo | |
|---|---:|---|
| **SampleRateTap** Q15 balanced | 17.5 | the row FPU-less embedded targets actually run |

Reading guide:

- **soxr wins raw host throughput, and the latency column is why.** It
  processes in large internal batches with SIMD throughout (soxr latency
  measured via `soxr_delay()`). At ~12–16 ms it is a fine batch/offline
  resampler and unusable inside a 1–2 ms live monitoring budget — the
  regime SampleRateTap is built for. There is no setting that buys soxr's
  throughput at SampleRateTap's latency.
- **libsamplerate is the closest architectural analog** (streaming
  time-domain polyphase, block-by-block) and SampleRateTap is 2.9–3.6×
  (mono/stereo; 2.1× at 8 channels, where both engines amortize)
  faster at the matched ~120 dB tier, 6.2× at ~140 dB, while also carrying
  ~2–3.6× less latency. That is the near-unity specialization dividend:
  a 48-tap window with a creeping phase instead of general-ratio
  machinery.
- Even at 8 channels, one stream costs SampleRateTap ~1.6 % of a single
  Xeon core (3.0 M frames/s ≈ 62× realtime).

### Embedded executed instructions per output frame (QEMU TCG plugin)

Same comparison workload cross-compiled per target (`SRT_ICOUNT_COMPARE`,
`.github/workflows/compare.yml`; deterministic counts, methodology as the
ratchet in [PERFORMANCE.md](PERFORMANCE.md)). Stereo float, 2 s of audio.
libsamplerate 0.2.2; arm-none-eabi-gcc 13.2.1, hexagon-clang 19.1.5, -O2.

| Target | **SampleRateTap** balanced | lsr `MEDIUM` | lsr `BEST` |
|---|---:|---:|---:|
| Cortex-M55 | **899** | 2,218 (2.5×) | 6,400 (7.1×) |
| Cortex-M33 (Pico 2 class) | 18,842¹ | 49,424 (2.6×) | 149,426 (7.9×) |
| Hexagon | **3,275** | 9,102 (2.8×) | 26,959 (8.2×) |

¹ The float datapath is soft-double-bound on the FP64-less M33 — the
README directs Pico-class parts to Q15, where the **full converter**
(servo and FIFO included) costs ~5,043 instructions/frame (post-C4):
libsamplerate has no fixed-point path, so its cheapest option on such parts costs
**~9.8×** what SampleRateTap's intended configuration does.

## The landscape

| | Type | Clock recovery | Ratio range | Quality | Latency | Footprint / targets | License & form |
|---|---|---|---|---|---|---|---|
| **SampleRateTap** | software ASRC | built-in (PI servo on FIFO occupancy) | near-unity (±~1000 ppm) | −132 dB THD+N / 149 dB DR measured above; Q15/Q31 paths for FPU-less DSPs | **1.5 ms default** (0.5 ms filter); sub-ms with `fast()` | 308× RT/core x86; ~515 insn/sample Q15 kernel-only on Hexagon (full converter ~1,245/frame stereo), CI-gated | MIT, header-only C++20 |
| [AD1896][ad1896] (ADI) | hardware ASRC | built-in | 1:8 up / 7.75:1 down | THD+N −117 dB min / −133 dB best; 142 dB DNR (datasheet) | sub-ms–ms, mode dependent | dedicated chip, one stereo pair | proprietary |
| [SRC4392][src4392] (TI) | hardware ASRC | built-in (automatic) | 1:16–16:1 | THD+N −140 dB typ; 144 dB DR (datasheet) | selectable filter delay | dedicated chip + DIR/DIT | proprietary |
| [libsamplerate][lsr] | resampler library | **no** — caller supplies ratio | 1/256–256 | measured above (near-unity); 97 dB worst-case across ratios (own docs) | filter-dependent, offline-friendly | portable C, float | BSD-2 |
| [soxr][soxr] | resampler library | no (fixed ratio + bounded VR mode) | wide | measured above (near-unity) | quality-dependent | portable C, SIMD | LGPL |
| zita-resampler + zita-ajbridge | resampler + DLL servo | ajbridge adds a delay-locked loop | near-unity (bridge) | designed for 24-bit transparency; no published CI-verified figures | several ms (period-driven) | Linux/JACK, float | GPL |
| OS engines (CoreAudio, WASAPI shared, PipeWire) | system ASRC | built-in, opaque | device-dependent | unpublished; generally well below the above | typically 5–20 ms | bundled | n/a |

## Caveats, stated plainly

- **Hardware rows are datasheet values, not our measurement.** Silicon is
  characterized through an analog test loop with its own converters and a
  wider notch than the ±20 Hz used here; both differences flatter a number.
  A pristine-digital software measurement and a bench measurement of a chip
  are comparable in definition, not in environment.
- **The structural trade is ratio range.** The chips convert 44.1↔48 and
  beyond; SampleRateTap deliberately handles only clock *drift* around a
  common nominal rate — that restriction is what buys the 48-tap datapath,
  0.5 ms filter delay, and embedded-class compute. For genuine rate
  *conversion*, put soxr/libsamplerate in the chain.
- **Coarse-block operation is a different regime** (cent-scale low-rate FM
  over a 53–61 dB floor — measured in
  [the block-size study](../notebooks/asrc_block_size_study.ipynb)); the
  numbers above are for fine-grained transfer.
- Software-row figures regenerate by re-running the comparison notebook;
  its assertions pin SampleRateTap's results so regressions fail the run.

[ad1896]: https://www.analog.com/media/en/technical-documentation/data-sheets/ad1896.pdf
[src4392]: https://www.ti.com/product/SRC4392
[lsr]: https://libsndfile.github.io/libsamplerate/quality.html
[soxr]: https://github.com/chirlu/soxr
