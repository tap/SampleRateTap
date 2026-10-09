# SampleRateTap vs. other sample rate converters

Two different kinds of product get called an "SRC": **full ASRCs** that
recover the clock ratio themselves (hardware chips, OS audio engines,
SampleRateTap), and **resampler libraries** that must be handed the ratio by
an external servo (libsamplerate, soxr, r8brain-free-src, zita-resampler).
The second group solves only half of the drift problem. One of them,
SpeexDSP's resampler, is also the only one with a fixed-point build, so it
is the one competitor the Q15 rows below can be measured against on their
own terms.

## Measured, identical conditions (software subjects)

From [notebooks/asrc_comparison.ipynb](../notebooks/asrc_comparison.ipynb)
(re-executed 2026-10-09): one AES17-style measurement implementation applied
to every subject — 997 Hz at −1 dBFS across a +200 ppm clock crossing
(48 009.6 → 48 000 Hz), fundamental removed by exact fit + ±20 Hz notch,
residual integrated 20 Hz–20 kHz; DR per AES17 (−60 dBFS, A-weighted). The
**24-bit interface** columns quantize each subject's output to 24 bits —
the condition under which software numbers are directly comparable to
hardware datasheets, since that is the interface silicon presents. The
measurement instrument is calibrated in-notebook against known synthetic
signals before use.

| Subject | Clock knowledge | THD+N (24-bit IO) | THD+N (float IO) | DR A-wtd (24-bit IO) |
|---|---|---:|---:|---:|
| **SampleRateTap** (balanced, float) | **recovered by servo** | **−133.9 dB** | −134.3 dB | **149.1 dB** |
| libsamplerate `sinc_best` | given exact ratio (oracle) | −143.5 dB | −149.4 dB | 149.1 dB |
| soxr `VHQ` | given exact ratio (oracle) | −143.8 dB | −150.8 dB | 149.1 dB |
| r8brain-free-src `CDSPResampler24` | given exact ratio (oracle) | −143.9 dB | −150.8 dB | 149.1 dB |
| SpeexDSP quality 10, float build | given exact ratio (oracle) | −133.7 dB | −134.1 dB | 149.1 dB |
| SpeexDSP quality 10, fixed-point build (Q15 IO) | given exact ratio (oracle) | −80.2 dB | −80.2 dB | 97.4 dB |
| **SampleRateTap** (balanced, Q15) | **recovered by servo** | −76.6 dB | −76.6 dB | 97.3 dB |
| naive FIFO (drop on full) | n/a | −34.7 dB | −34.7 dB | 94.7 dB |

SampleRateTap's float row moved from −132.1 dB (2026-06-11) when the
notebook was re-executed for the r8brain row on 2026-09-25: the balanced
profile gained its compensated prototype (transmission zeros at k·fs) on
2026-07-04, and that was the first re-measurement since. The 2026-10-09
re-execution (SpeexDSP and the Q15 row added) reproduces every earlier row
to the displayed precision.

Reading guide:

- The oracle-fed libraries measure at the *format ceilings* (float32 I/O
  ≈ −150 dB; 24-bit ≈ −143.5 dB; A-weighted 24-bit DR ceiling = 149.1 dB).
  Near-unity is their easy regime — libsamplerate's published "97 dB worst
  case" applies to aggressive ratios, not this one. All three are at the
  ceilings, so this measurement cannot rank them against each other.
- r8brain is measured through its offline `oneshot()` path (filter delay
  removed, tail flushed) via a two-function C shim over the pinned headers
  (`tools/compare_shim/`, `cmake/r8brain.cmake`), since it has no maintained
  Python binding. Its preset for 24-bit/float work, `CDSPResampler24`
  (180.15 dB stopband, 2 % transition band), is the subject.
- SpeexDSP is measured the same way through its own shim
  (`tap_sr_async_speex_shim.cpp`, `cmake/speexdsp.cmake`: upstream's
  `resample.c` compiled twice from the pinned 1.2.1 source, once per
  arithmetic build). Its float build at quality 10 lands where `balanced`
  does, −134 dB, not at the format ceiling the other three reach: its
  quality rating ("~100 dB" by its own table) is a stopband figure, and at
  near-unity the images it leaves fall near the 24-bit floor. Its
  fixed-point build and SampleRateTap's Q15 datapath are the two 16-bit
  rows: both measure the 16-bit interface's A-weighted DR ceiling (97 dB)
  and sit within a few dB of each other in THD+N (−80 and −77; the README's
  "77 dB, format-limited" for `converter_q15` is this measurement). A Q15
  caller gets the same quality from either; the cost tables below say what
  each charges for it.
- SampleRateTap's −134 dB includes the entire problem: the servo discovered
  the ratio from FIFO occupancy and the conversion ran causally at 1.5 ms
  latency. The ~10 dB to the oracle libraries is the measured price of
  clock recovery + real-time operation — the part of the problem the
  libraries do not solve.
- The naive FIFO row is the cost of doing nothing.

## Computational cost, identical conditions (software subjects)

Same engines, same task: convert a float 997 Hz stereo stream at the fixed,
known near-unity ratio 1 + 200 ppm, streaming in 128-frame blocks
(`bench/compare/`, `-DTAP_SR_BUILD_COMPARE_BENCH=ON`). SampleRateTap runs its
datapath with a constant rate deviation (the servo is quiescent at a fixed
ratio); the libraries take the ratio as an input. Quality tiers are paired
by vendor-stated stopband: balanced ≈ `MEDIUM` ≈ `HQ` ≈ r8brain at
`ReqAtten` 120 dB (~120 dB), transparent ≈ `BEST` ≈ `VHQ` ≈ r8brain's
16-bit preset (~140 dB+). SpeexDSP's best quality (10) is "~100 dB" by its
own table, below both tiers; it is kept for being the one competitor with
a fixed-point build, so its fixed-point rows are ratioed against the Q15
datapath and its float rows against `balanced`. r8brain is mono per instance with double I/O, so
the harness runs one instance per channel and the float↔double
(de)interleave is inside the timed loop — what any float-interleaved caller
pays to use it. Latency figures are measured: SampleRateTap's is the filter
group delay, libsamplerate's the input buffered before its first streaming
output, soxr's via `soxr_delay()` (it varies with the streaming state; the
range over the mono/stereo/8-ch runs is shown), r8brain's via `getInLenBeforeOutPos(0)` — the
input it consumes before its first output.

### r8brain's latency is a transition-band choice

r8brain's delay is set by its transition-band parameter (percent of the
band below Nyquist), and a wider band rolls the passband off. Measured on
the pinned engine at 120 dB and this ratio (notebook, "Latency vs.
passband"): the default 2 % band withholds **789 input frames (16.4 ms)**,
flat far past 20 kHz; the lowest-latency setting still flat to 20 kHz like
`balanced` is **8 %: 200 frames (4.2 ms)** linear-phase, 143 frames
(3.0 ms) minimum-phase (latency is not monotonic in the knob: 10 % costs
212, and 12 % already droops 0.11 dB at 20 kHz). Its ~1 ms settings exist
only at a 45 % band, which is −35 dB at 20 kHz. The tables below carry both
the default and the passband-matched 8 % configuration.

### SpeexDSP's quality knob is a latency knob too

SpeexDSP's quality (0–10) sets filter length, oversampling and cutoff
together, and its filters are short (the sinc spans eight input samples at
quality 10), so no setting is flat to 20 kHz: measured on the pinned engine
at this ratio (notebook, "SpeexDSP: the same two questions"), the 20 kHz
gain settles at **−0.42 dB from quality 4 up**, both builds. Quality 10
withholds **128 input frames (2.7 ms)**; quality 4, the lowest at that
passband knee, **32 frames (0.67 ms)** at a quarter of the cost. The tables
carry both: quality 10 as its ceiling, quality 4 as the row that matches
`balanced`'s latency class.

### Host wall-clock (x86, GCC 13.3 -O3 (CMake Release), shared Xeon @ 2.80 GHz, 2026-10-09)

Million output frames/s, median of 5 — relative ratios are the meaningful
figures on a shared machine; all subjects ran in the same session (an
earlier session on a 2.10 GHz host, 2026-09-25, gave the same ratios
within ~15 %).
libsamplerate 0.2.2 and soxr 0.1.3 are the Ubuntu 24.04 packages; r8brain
and SpeexDSP are the pinned commits (7.6 and 1.2.1), stock configurations
(r8brain with its Ooura FFT).

| Engine (~120 dB tier) | mono | stereo | 8-ch | algorithmic latency |
|---|---:|---:|---:|---:|
| **SampleRateTap** balanced | 13.9 | 9.5 | 1.8 | **24 frames (0.50 ms)** |
| libsamplerate `MEDIUM` (0.2.2) | 3.6 | 3.1 | 1.4 | 46 frames (0.96 ms)¹ |
| soxr `HQ` (0.1.3) | 70.9 | 32.3 | 8.3 | 703–880 frames (14.7–18.3 ms) |
| r8brain 120 dB, default 2 % band | 21.4 | 10.6 | 2.8 | 789 frames (16.4 ms) |
| r8brain 120 dB, 8 % band (flat to 20 kHz) | — | 11.9 | — | 200 frames (4.2 ms) |

| Engine (~140 dB tier) | stereo | algorithmic latency |
|---|---:|---:|
| **SampleRateTap** transparent | 6.0 | 40 frames (0.83 ms) |
| libsamplerate `BEST` | 0.9 | 143 frames (3.0 ms)¹ |
| soxr `VHQ` | 22.6 | 597 frames (12.4 ms) |
| r8brain `CDSPResampler16` (136.45 dB) | 11.8 | 1,782 frames (37.1 ms) |
| r8brain `CDSPResampler24` (180.15 dB) | 9.6 | 1,700 frames (35.4 ms) |

| Below both tiers (SpeexDSP, "~100 dB" at best) | mono | stereo | 8-ch | algorithmic latency |
|---|---:|---:|---:|---:|
| SpeexDSP float build, quality 10 | 1.9 | 0.9 | 0.2 | 128 frames (2.67 ms) |
| SpeexDSP float build, quality 4 (its passband knee) | — | 2.4 | — | 32 frames (0.67 ms) |

| The fixed-point rows (Q15 I/O) | stereo | algorithmic latency |
|---|---:|---:|
| **SampleRateTap** Q15 balanced | 17.1 | **24 frames (0.50 ms)**; the row FPU-less embedded targets actually run |
| SpeexDSP fixed-point build, quality 10 | 1.2 | 128 frames (2.67 ms) |
| SpeexDSP fixed-point build, quality 4 | 4.5 | 32 frames (0.67 ms) |

¹ Measured 2026-06-12 on the same library version; the harness reports
latency counters for soxr, r8brain and SpeexDSP only.

Reading guide:

- **soxr wins raw host throughput, and the latency column is why.** It
  processes in large internal batches with SIMD throughout. At ~12–18 ms it
  is a fine batch/offline resampler and unusable inside a 1–2 ms live
  monitoring budget — the regime SampleRateTap is built for. There is no
  setting that buys soxr's throughput at SampleRateTap's latency.
- **r8brain out-runs SampleRateTap on x86 at the ~120 dB tier** — 1.5×
  mono, 1.1× stereo, 1.6× at 8 channels (1.3× stereo passband-matched) —
  and at the ~140 dB tier (2.0× at its 136 dB setting). Its FFT block
  convolution amortizes well on a desktop core. The price is the same as
  soxr's: 8× SampleRateTap's filter delay at the matched passband and 33×
  at its default, 45× at the 140 dB tier. On the embedded targets the
  ranking reverses, and it has no fixed-point option (next section).
- **libsamplerate is the closest architectural analog** (streaming
  time-domain polyphase, block-by-block) and SampleRateTap is 3.1–3.9×
  (stereo/mono; 1.3× at 8 channels, where both engines amortize)
  faster at the matched ~120 dB tier, 6.9× at ~140 dB, while also carrying
  ~2–3.6× less latency. That is the near-unity specialization dividend:
  a 48-tap window with a creeping phase instead of general-ratio
  machinery.
- **SpeexDSP is the slowest subject on the host at every setting**, in
  both builds: `balanced` converts 4× its float build's quality 4 and 11×
  its quality 10 (stereo), and the Q15 datapath 3.8× and 14× its
  fixed-point build's. Its design centre is the embedded fixed-point core,
  which is where the next section measures it; on x86 its scalar
  sinc-table loop has none of the batch or SIMD leverage soxr and r8brain
  bring.
- Even at 8 channels, one stream costs SampleRateTap ~2.7 % of a single
  Xeon core (1.8 M frames/s ≈ 37× realtime).

### Embedded executed instructions per output frame (QEMU TCG plugin)

Same comparison workload cross-compiled per target (`TAP_SR_ICOUNT_COMPARE`,
`.github/workflows/compare.yml`; deterministic counts, methodology as the
ratchet in [PERFORMANCE.md](PERFORMANCE.md)). Stereo, float I/O (Q15 for
the Q15 row and SpeexDSP's fixed-point build), 32-frame blocks.
libsamplerate 0.2.2, r8brain and SpeexDSP at their pinned commits (7.6 and
1.2.1; SpeexDSP from source in both arithmetic builds, `cmake/speexdsp.cmake`);
arm-none-eabi-gcc 13.2.1, hexagon-clang 19.1.5, -O3 (CMake Release;
earlier revisions said -O2, but the build type was the same).

Every engine is built at 2 s and 4 s of audio: the difference is the
**steady-state** cost per output frame, the remainder the **one-time
construction** (filter design, tables, FFT setup). The tables below are
generated from `bench/compare_counts.json`, the record `compare.yml` writes
(`scripts/harvest_compare.py`), by `scripts/update_compare_docs.py`; CI
regenerates them and fails on a diff, so a DspTap pin bump that moves a count
is a red check until the comparison is re-run and re-harvested. Earlier
revisions of this table divided the 2 s total by the frame count, which folds
construction into the per-frame figure; the libsamplerate totals under that
old metric reproduce the previous table exactly (2,218 / 6,400 on M55, 49,424
/ 149,426 on M33, 9,102 / 26,959 on Hexagon).

<!-- COMPARE:BEGIN -->
Measured in `compare.yml` run
[37920542901](https://github.com/tap/SampleRateTap/actions/runs/37920542901)
on `fc2949e` (DspTap `ef6fdc4`), 2026-10-09; arm-none-eabi-gcc 13.2.1 and
qemu-system-arm 8.2.2 from the ubuntu-24.04 image, hexagon-clang 19.1.5 and
qemu-hexagon 8.2.2 at the pinned digests.

Steady state, instructions per stereo output frame (× = vs. SampleRateTap
balanced float, or vs. balanced Q15 where the row says so; the cheaper
SampleRateTap row per target in bold):

| Engine | Cortex-M55 | Cortex-M33 (Pico 2 class) | Hexagon |
|---|---:|---:|---:|
| **SampleRateTap** balanced, float | **821** | 15,302 | 2,754 |
| **SampleRateTap** balanced, Q15 | 861 | **879** | **457** |
| r8brain 120 dB, default 2 % band³ | 1,002 (1.2×) | 30,004 (2.0×) | 6,060 (2.2×) |
| r8brain 120 dB, 8 % band (flat to 20 kHz)³ | 934 (1.1×) | 26,619 (1.7×) | 5,420 (2.0×) |
| libsamplerate `MEDIUM` | 2,203 (2.7×) | 49,206 (3.2×) | 9,025 (3.3×) |
| libsamplerate `BEST` | 6,392 (7.8×) | 149,420 (9.8×) | 26,916 (9.8×) |

One-time construction, millions of instructions:

| Engine | Cortex-M55 | Cortex-M33 | Hexagon |
|---|---:|---:|---:|
| **SampleRateTap** balanced, float | 17.3 | 870.0 | 133.3 |
| **SampleRateTap** balanced, Q15 | 18.4 | 883.5 | 135.8 |
| r8brain 120 dB, default 2 % band | 1.6 | 38.1 | 11.3 |
| r8brain 120 dB, 8 % band (flat to 20 kHz) | 1.8 | 45.5 | 12.6 |
| libsamplerate `MEDIUM` | 1.4 | 20.9 | 7.4 |
| libsamplerate `BEST` | 0.8 | 0.7 | 4.0 |

Key figures: on the M33 the Q15 datapath costs **879 instructions/frame** in
steady state, libsamplerate `MEDIUM` **~56×** that and r8brain at 8 %
**~30×**; on the M55 the float datapath (821) is cheaper than Q15 (861) by 5%.
<!-- COMPARE:END -->

² The float datapath is soft-double-bound on the FP64-less M33 and
Hexagon (its accumulation is double by contract) — the README directs
Pico-class parts and double-less DSPs to Q15. The key figures above give the
Q15 datapath's steady-state cost there and the multiples libsamplerate
(no fixed-point path), r8brain (double precision throughout, no
fixed-point path either) and SpeexDSP's fixed-point build — the one
competitor that can run there on the Q15 row's terms — pay against it. On the M55, whose FPU serves
float well, float and Q15 are within a few percent of each other since
DspTap's Helium Q15 dot (the key figures say which is cheaper, and by how
much); before it, Q15 cost 42 % more than float there.

**SpeexDSP's rows read per target.** On the M55, the one target where
SampleRateTap's float datapath is its cheaper row, SpeexDSP's cheapest
setting (quality 4, the row that matches `balanced`'s latency class at
−0.42 dB by 20 kHz) costs ~1.9× `balanced` in either build, and its quality
10 ~7× (fixed-point) to ~12× (float). On the M33 and Hexagon the Q15 row is
the one that runs (footnote ²: the float datapath accumulates in double,
soft-float on both FP64-less cores), and Speex's float build is
single-precision throughout, so its float rows undercut that float row by
an order of magnitude; against the Q15 datapath its quality 4 costs 1.3×
(Hexagon) to 2.0× (M33) fixed-point and 1.4× to 2.5× float, its quality 10
4× to 7× fixed-point and 70× to 200× float. The one row a Q15 caller
would weigh is its fixed-point build at quality 4: within 2× of the Q15
datapath on every target, at a third more latency (32 frames against 24)
and a −0.42 dB shelf at 20 kHz. Its construction is cheap, as the other
libraries' is (under 100 M instructions on the M33, under 27 M elsewhere).

³ r8brain guards its process-wide filter cache with `std::mutex` and has no
hook to replace it; the thread-less arm-none-eabi newlib declares none, so
the Cortex-M builds force-include `bench/icount/r8b_single_thread_mutex.h`
(a no-op lock — exact for this single-threaded workload, and outside the
per-sample path). Hexagon's musl build uses the real mutex.

**Construction is the one column SampleRateTap loses.** Its filter design (the
compensated prototype, run in double at construction) costs ~0.9 G
instructions on the M33 as QEMU emulates it, every double operation a software
libcall (~1.3 G until DspTap shared the Kaiser window's Bessel series across
the design's kernel builds — a change that moved the construction rows and not
one steady-state instruction), against tens of millions for r8brain and
libsamplerate. On a generic FP64-less 150 MHz core that is seconds of
start-up; the RP2350 routes double arithmetic through its DCP coprocessor, so
a Pico 2 should pay less than the count suggests (`pico2_cyccnt` can measure
it). It is paid once per converter, never on the audio path, but it is a real
cost for devices that construct at boot.

## The landscape

| | Type | Clock recovery | Ratio range | Quality | Latency | Footprint / targets | License & form |
|---|---|---|---|---|---|---|---|
| **SampleRateTap** | software ASRC | built-in (PI servo on FIFO occupancy) | near-unity (±~1000 ppm) | −134 dB THD+N / 149 dB DR measured above; Q15/Q31 paths for FPU-less DSPs | **1.5 ms default** (0.5 ms filter); sub-ms with `fast()` | 308× RT/core x86; Q15 datapath 457 insn/frame stereo steady-state on Hexagon, 879 on M33 (above), CI-gated | MIT, header-only C++20 |
| [AD1896][ad1896] (ADI) | hardware ASRC | built-in | 1:8 up / 7.75:1 down | THD+N −117 dB min / −133 dB best; 142 dB DNR (datasheet) | sub-ms–ms, mode dependent | dedicated chip, one stereo pair | proprietary |
| [SRC4392][src4392] (TI) | hardware ASRC | built-in (automatic) | 1:16–16:1 | THD+N −140 dB typ; 144 dB DR (datasheet) | selectable filter delay | dedicated chip + DIR/DIT | proprietary |
| [libsamplerate][lsr] | resampler library | **no** — caller supplies ratio | 1/256–256 | measured above (near-unity); 97 dB worst-case across ratios (own docs) | filter-dependent, offline-friendly | portable C, float | BSD-2 |
| [soxr][soxr] | resampler library | no (fixed ratio + bounded VR mode) | wide | measured above (near-unity) | quality-dependent | portable C, SIMD | LGPL |
| [r8brain-free-src][r8b] | resampler library | no — caller supplies ratio | arbitrary | measured above (near-unity, 24-bit preset at the format ceilings); stopband user-set 49–218 dB | transition-band dependent: 16 ms default at 120 dB here, 4.2 ms flat to 20 kHz, ~1 ms only with a 13 kHz-class roll-off | double precision, no fixed-point path; SSE2/AVX/NEON; ~1.1–2.2× SampleRateTap's float steady state on the embedded targets above | MIT, header-only C++ |
| [SpeexDSP resampler][speex] | resampler library | no — caller supplies ratio (`set_rate_frac`) | arbitrary | measured above: float build at `balanced`'s level, fixed-point build at the 16-bit floor beside the Q15 row; "~100 dB" at best by its own table, and never flat to 20 kHz (−0.42 dB from quality 4 up) | quality-set: 32 frames (0.67 ms) at quality 4, 128 (2.7 ms) at 10 | portable C; **the one competitor with a fixed-point build** (Q15 I/O); its cost against the Q15 datapath on the embedded targets is in the tables above | BSD-3, C |
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
  *conversion*, put a synchronous resampler in the chain —
  soxr/libsamplerate/r8brain, or for exactly 44.1↔48 the family's own
  [`bridge`](https://github.com/tap/SampleRateTap/tree/main/bridge) engine, which cross-validates its
  output against this library's engine.
- **Coarse-block operation is a different regime** (cent-scale low-rate FM
  over a 54–62 dB floor — measured in
  [the block-size study](../notebooks/asrc_block_size_study.ipynb)); the
  numbers above are for fine-grained transfer.
- Software-row figures regenerate by re-running the comparison notebook;
  its assertions pin SampleRateTap's results so regressions fail the run.

[ad1896]: https://www.analog.com/media/en/technical-documentation/data-sheets/ad1896.pdf
[src4392]: https://www.ti.com/product/SRC4392
[lsr]: https://libsndfile.github.io/libsamplerate/quality.html
[soxr]: https://github.com/chirlu/soxr
[r8b]: https://github.com/avaneev/r8brain-free-src
[speex]: https://github.com/xiph/speexdsp
