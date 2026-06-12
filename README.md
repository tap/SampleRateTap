# SampleRateTap

[![CI](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

Header-only C++20 **asynchronous sample rate converter** (ASRC) for the
*near-unity* case: two audio clock domains at nominally the same rate (e.g.
48 kHz ↔ 48 kHz) sourced from independent oscillators, each within a few
hundred ppm and drifting slowly. One thread pushes input samples at the input
clock, another pulls output samples at the output clock; the converter
absorbs the rate mismatch transparently — including the whole-sample
slips that occur roughly once every `1/ppm` samples.

- Header-only, no dependencies, `cmake` ≥ 3.24, GCC 11+/Clang 14+/MSVC 19.30+
- Real-time safe audio path: `push()`/`pull()` are `noexcept`, lock-free and
  allocation-free; all allocation and filter design happen in the constructor
- Measured quality (default *balanced* preset, +200 ppm offset, THD+N-style
  residual): **133 dB** SNR at 997 Hz, **111 dB** at 12 kHz, **105 dB** at
  19.5 kHz
- ~**1.5 ms** designed latency with the default configuration at 48 kHz
  (24-frame filter group delay + 48-frame FIFO setpoint)

## Quick start

```cmake
add_subdirectory(SampleRateTap)            # or FetchContent
target_link_libraries(app PRIVATE SampleRateTap::SampleRateTap)
```

```cpp
#include <srt/srt.hpp>

srt::Config cfg;
cfg.sampleRateHz = 48000.0;
cfg.channels = 2;
srt::AsyncSampleRateConverter asrc(cfg);   // allocates + designs filter; may throw

// Input-device thread (input clock):
asrc.push(inputInterleaved, frames);       // noexcept, lock-free

// Output-device thread (output clock):
asrc.pull(outputInterleaved, frames);      // noexcept, lock-free; silence
                                           // until filled/locked

srt::Status st = asrc.status();            // any thread: state, ppm, fill,
                                           // underruns/overruns/resyncs
```

`examples/drifting_clocks.cpp` runs two real threads 500 ppm apart and shows
the lock acquisition and rate estimate. For a visual tour — lock, measured
transparency vs. a naive FIFO, spectrograms, latency, drift tracking,
dropout recovery — see
[notebooks/asrc_demo.ipynb](notebooks/asrc_demo.ipynb), which drives the
library through its C ABI (`-DSRT_BUILD_CAPI=ON`, `tools/capi/`) via ctypes
(Python needs `numpy` and `matplotlib`; the first cell builds the shared
library if missing). A second notebook,
[notebooks/asrc_block_size_study.ipynb](notebooks/asrc_block_size_study.ipynb),
measures how processing block size (32 / 64 / 240 frames) trades latency
against servo observability — including per-impulse latency-breathing
measurements and a calibrated FM/wideband quality decomposition.

## How it works

The design follows the classic commercial-ASRC architecture (AD1896-style
polyphase FIR + clock servo), specialized for the near-unity regime where the
conversion degenerates into a *creeping fractional delay*.

**Datapath.** A Kaiser-windowed sinc prototype is designed at construction
and decomposed into `L` polyphase branches of `T` taps (default 256 × 48,
120 dB stopband, flat to 20 kHz). Each output sample evaluates one branch
pair: coefficients are linearly interpolated between the two phases adjacent
to the fractional position μ, with the dot product accumulated in double.
The table stores an extra row (phase 0 advanced one tap) so the μ wrap
1.0 → 0.0 with a one-sample window shift — the whole-sample slip — is
exactly continuous and branch-free.

**Phase accumulator.** μ ∈ [0, 1) is a double that accumulates only the rate
*deviation* ε per output sample; the unity part of the ratio is the integer
window advance. Resolution is 2⁻⁵² samples, far below the ~8 ps jitter
budget for 120 dB transparency at 20 kHz.

**Clock servo.** A lock-free SPSC FIFO sits between the domains and its
occupancy is the phase detector of a type-2 (PI) loop whose output ε̂ drives
the phase increment — a PLL in which the FIFO comparison is the phase
detector and the μ accumulator is the NCO. Gains derive from the standard
2nd-order matching: `Kp = 2ζωₙ/fs`, `Ki = ωₙ²/fs`. The loop runs in three
stages with the integrator (the ppm estimate) handed across transitions:

| Stage | Bandwidth | Error smoothing | Role |
|---|---|---|---|
| Acquire | 10 Hz | 1-pole, 50 Hz | fast pull-in (locks in ~1 s) |
| Track | 1 Hz | 1-pole, 5 Hz | robust lock; terminal stage for coarse block transfer |
| Quiet | 0.05 Hz | 3-pole cascade, 0.5 Hz | steady state for fine-grained transfer |

**Why three stages.** The FIFO count is quantized to whole frames (or whole
push blocks), so the occupancy observable carries a deterministic sawtooth at
the *beat frequency* `ppm × pushRate`. Whatever the loop passes into ε̂
frequency-modulates the audio. The Quiet stage rejects a one-frame sawtooth
to roughly −120 dBc equivalent at 20 kHz while still tracking a 1 ppm/s
oscillator drift ramp with under half a frame of standing error. With coarse
blocks (e.g. ≥32-frame callbacks) that level of quiet is information-
theoretically unavailable from counts alone, so the servo deliberately stays
in Track, where the block beat is phase-tracked mostly as benign latency
breathing, the remainder as cent-scale low-rate FM (measured in
[notebooks/asrc_block_size_study.ipynb](notebooks/asrc_block_size_study.ipynb):
~0.9 cents rms / 61 dB wideband at 32-frame blocks, ~1.3 cents rms / 53 dB
at 5 ms blocks). Promotion to Quiet is gated on the
cascade-smoothed error, which is exactly the discriminator between the two
regimes.

**Under/overrun policy.** `pull()` always fills its buffer: silence while
filling or after an underrun (the converter then refills and re-locks,
keeping the ppm estimate). If the consumer stalls until the high watermark,
the converter discards down to the setpoint and counts a resync. `push()`
into a full FIFO drops the newest frames and counts an overrun.

## Latency

```
latency = targetLatencyFrames + (L·T − 1)/(2L)      [input frames]
        = 48 + ~24 ≈ 72 frames ≈ 1.5 ms at 48 kHz   (defaults)
```

`designedLatencySeconds()` reports the figure; the FIFO term breathes by a
fraction of the block size as the servo tracks drift. The filter is linear
phase. For lower latency use `FilterSpec::fast()` (~16-frame group delay)
and a smaller `targetLatencyFrames`; the FIFO setpoint must stay above the
peak occupancy excursion of your push/pull block jitter.

## Measured performance

From the test suite (deterministic two-clock simulation, +200 ppm offset,
sample-granular transfer, 0.5 FS sine, 1 s analysis window after settling):

| Preset | 997 Hz | 6 kHz | 12 kHz | 19.5 kHz | group delay |
|---|---|---|---|---|---|
| `balanced()` (L=256, T=48) | 133 dB | 118 dB | 111 dB | 105 dB | 0.50 ms |
| `transparent()` (L=512, T=80) | 133 dB | — | — | 108 dB | 0.83 ms |

AES17-style THD+N measured under identical conditions against
libsamplerate, soxr and hardware datasheet figures:
[docs/COMPARISON.md](docs/COMPARISON.md) (−132 dB THD+N / 149 dB DR at the
24-bit interface, servo in the loop;
[notebooks/asrc_comparison.ipynb](notebooks/asrc_comparison.ipynb)).

The high-frequency residual is dominated by the linear interpolation between
adjacent phase-table rows (≈ −12 dB per doubling of `L`, +12 dB per octave of
signal frequency). Servo lock from a cold start takes ~1 s; a 0 → 300 ppm
drift ramp at 10 ppm/s is tracked without unlocking.

## Platform support

CI builds and tests every push on:

- **Linux** (GCC, Clang) and **macOS** (AppleClang) with warnings as errors,
  **Windows** (MSVC /W4)
- **AddressSanitizer + UBSan** and **ThreadSanitizer** over the full suite
- **Qualcomm Hexagon** (hexagon-unknown-linux-musl, the open-source clang
  toolchain from [quic/toolchain_for_hexagon]) cross-compiled and executed
  under `qemu-hexagon` user-mode emulation — validating the library on a
  32-bit audio DSP target: `size_t` width, atomics lowering, musl libc and
  soft-float doubles. Emulation proves correctness, not performance; Hexagon
  and HiFi-class DSPs have no double-precision FPU, so the float datapath's
  double accumulation runs soft-float there (the Q15/Q31 fixed-point traits
  are the performance-appropriate path for such targets). Cycle accuracy
  requires the vendor simulator.
- **Performance gating on both DSP targets**: fixed workloads run under
  QEMU with an instruction-counting plugin and are compared against
  committed baselines (`bench/baselines.json`) at ±3% — a hot-path
  regression on Hexagon or Cortex-M55 fails CI. See
  [docs/PERFORMANCE.md](docs/PERFORMANCE.md).
- **Arm Cortex-M55**, bare metal (newlib + semihosting, no OS/threads),
  executed on QEMU's MPS3 AN547 board model via `qemu-system-arm`. The
  platform layer lives in `platform/mps3_an547/` (linker script + minimal
  startup: vector table, FPU/MVE enable, 64-bit atomic helpers) with the
  toolchain file `cmake/arm-cortex-m55-mps3.cmake`; the on-target run covers
  the polyphase kernel, the fixed-point datapaths and the end-to-end
  converter (see `tests/bare_metal_main.cpp` for the emulation-sized
  filter).

A separate workflow (`.github/workflows/ci-arm64.yml`, manual + weekly)
runs the suite natively on GitHub's `ubuntu-24.04-arm` runners, including
the ring-buffer stress under ThreadSanitizer on genuinely weakly-ordered
hardware — coverage x86 TSan and QEMU cannot provide. It is kept out of
per-push CI because arm64 hosted runners are not available on every plan
for private repositories.

For **Tensilica HiFi4/HiFi5** the audio ISA, xt-clang compiler and xt-run
instruction-set simulator are proprietary Cadence tools, so they cannot run
in public CI; `.github/workflows/ci.yml` contains a commented self-hosted
runner job template (`hifi-iss`) that drops in once a runner with a Cadence
license is available. `cmake/hexagon-linux-musl.cmake` shows the general
cross + emulator pattern (`CMAKE_CROSSCOMPILING_EMULATOR` makes `ctest` run
every test through the emulator transparently).

[quic/toolchain_for_hexagon]: https://github.com/quic/toolchain_for_hexagon

## Performance

Methodology, optimization roadmap and regression gating live in
[docs/PERFORMANCE.md](docs/PERFORMANCE.md). Build the benchmarks with
`-DSRT_BUILD_BENCHMARKS=ON` (host only).

<!-- ICOUNT:BEGIN -->
Executed instructions per fixed workload (`bench/icount/`), measured under QEMU with a counting plugin — deterministic, and gated in CI at ±3% against `bench/baselines.json`:

| Workload | Cortex-M55 | Hexagon |
|---|---:|---:|
| `kernel_float` | 99,468,474 | 339,027,222 |
| `kernel_q15` | 181,994,196 | 102,819,852 |
| `kernel_q31` | 210,789,622 | 110,455,141 |
| `pipeline_float` | 92,724,190 | 344,819,729 |
| `pipeline_q15` | 134,581,279 | 133,644,650 |
| `pipeline_q31` | 170,542,353 | 142,846,068 |
<!-- ICOUNT:END -->

<!-- PERF:BEGIN -->
Indicative numbers from a shared machine (Intel(R) Xeon(R) Processor @ 2.10GHz, 2026-06-11); regenerate with `scripts/update_perf_docs.py`. Items are output samples (kernel) or frames (pipeline); ×realtime is per 48 kHz stream.

| Benchmark | ns/item | ×realtime @48k |
|---|---:|---:|
| `BM_Kernel_Float_Fast` | 36.2 | 576× |
| `BM_Kernel_Float_Balanced` | 54.0 | 386× |
| `BM_Kernel_Float_Transparent` | 84.1 | 248× |
| `BM_Kernel_Q15_Balanced` | 38.3 | 544× |
| `BM_Kernel_Q31_Balanced` | 53.6 | 389× |
| `BM_Pipeline_Float_Balanced_1ch` | 58.1 | 359× |
| `BM_Pipeline_Float_Balanced_2ch` | 77.1 | 270× |
| `BM_Pipeline_Float_Balanced_8ch` | 248.3 | 84× |
| `BM_Pipeline_Q15_Balanced_2ch` | 53.9 | 386× |
| `BM_Pipeline_Q31_Balanced_2ch` | 112.1 | 186× |
| `BM_Pipeline_Float_Transparent_2ch` | 111.2 | 187× |
<!-- PERF:END -->

## Sample types

The datapath is templated on the sample type via `srt::SampleTraits`
(`include/srt/sample_traits.hpp`). Three formats are provided:

| Type | Alias | Format | Measured SNR (997 Hz / 19.5 kHz, half scale, +200 ppm) |
|---|---|---|---|
| `float` | `AsyncSampleRateConverter` | float I/O, double accumulation | 133 dB / 105 dB |
| `std::int32_t` | `AsyncSampleRateConverterQ31` | Q31 I/O, Q1.30 coeffs, int64 accumulation, saturating | 133 dB / 105 dB |
| `std::int16_t` | `AsyncSampleRateConverterQ15` | Q15 I/O, Q1.14 coeffs, int64 accumulation, saturating | 77 dB (format-limited) |

The fixed-point datapaths have integer-only inner loops (the μ blend factor
is converted once per output sample), making them the appropriate choice for
DSPs and MCUs without double-precision FPUs. Q31 is bit-for-bit as quiet as
the float path; Q15's floor is the 16-bit format itself. The servo and the
filter design always run in double (control path / one-time init, a handful
of operations per block).

## Limitations

- Near-unity ratios only (±`maxDeviationPpm`, default 1000 ppm). No
  44.1 ↔ 48 kHz conversion.
- The rate estimate is derived from FIFO counts only. With block-quantized
  transfer its instantaneous value wobbles at the block-beat frequency
  (see `Status.ppm` vs. a few seconds of averaging), and ultra-quiet servo
  operation requires fine-grained transfer. A future version may accept
  per-block timestamps for sub-sample phase observation.
- One producer thread and one consumer thread; construction/destruction must
  not overlap either.

## Provenance and license

MIT (see `LICENSE`). All code implements long-published methods: Kaiser
window design (Kaiser 1974), band-limited interpolation (J. O. Smith,
CCRMA), polyphase decomposition and the harris length estimate, and textbook
2nd-order PLL servo design. No third-party source was copied. GoogleTest
(BSD-3) is fetched for tests only and is not part of the shipped headers.
