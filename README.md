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
the lock acquisition and rate estimate.

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
in Track, where the block beat is phase-tracked as benign latency breathing
(sub-cent wow at sub-hertz rates). Promotion to Quiet is gated on the
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

The high-frequency residual is dominated by the linear interpolation between
adjacent phase-table rows (≈ −12 dB per doubling of `L`, +12 dB per octave of
signal frequency). Servo lock from a cold start takes ~1 s; a 0 → 300 ppm
drift ramp at 10 ppm/s is tracked without unlocking.

## Sample types

The datapath is templated on the sample type via `srt::SampleTraits`
(`include/srt/sample_traits.hpp`). v0.1 implements the `float` path (float
I/O and coefficients, double accumulation). Q15/Q31 fixed-point support can
be added by specializing `SampleTraits` with integer accumulators and a
saturating `finalize()` — no API changes; the servo and filter design always
run in double (control path / one-time init).

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
