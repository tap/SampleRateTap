# rational — synchronous small-factor L/M within a rate family

[![CI](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](../LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**Synchronous sample rate conversion by a small rational factor, inside one
rate family, as fast as possible.**

Ratios L/M with L, M ∈ {2^a · 3^b} and gcd(L, M) = 1 — ↑2, ↓3, 2/3, ↑8,
3/8 … — realised as chains of Nyquist (L-th-band) stages whose structural
zeros cost nothing. Header-only C++20, built on the Tap family's shared
substrate ([DspTap](https://github.com/tap/DspTap): the L-th-band designer
and the stage composition landed there first, with the float/Q15/Q31
sample-format traits, the dot kernels and the row-sum quantization).

> **Status: M3 of [PLAN.md](PLAN.md) section 6.** The single stages ship
> for every ratio of the vocabulary — ↑2, ↓2, ↑3, ↓3, ↑6, ↓6, ↑8, ↓8 and the
> mixed 3/2, 2/3, 4/3, 3/4, 8/3, 3/8 — as `converter<ratio<L, M>>` (float,
> the golden model pinned sample-for-sample against committed scipy
> `upfirdn` vectors), `converter_q15` and `converter_q31`, with the family's
> call shapes: `process`, `pull`, exact `outputs_for` / `frames_needed`,
> `flush`, `reset`, the latency as an exact rational. Each stage is one
> L-th-band filter at the pinned length of the M2 design spike
> ([`notebooks/design_spike.ipynb`](notebooks/design_spike.ipynb)); its
> structural zeros are never multiplied, so the half-band decimator costs
> 23 MACs per output of its 43 taps and an interpolator's centre phase is a
> copy. Chains and the 14 × 14 coverage matrix (M4), the fixed-point floors
> (M5), the C ABI and the ratchet baselines (M6) follow. The plan is
> authoritative: charter, the decisions R1–R16, the generated matrix,
> layout, test strategy, non-goals and risks.

## Quick start

```cpp
#include <tap/sr/rational/rational.h>

using namespace tap::sr::rational;

converter<down_2> down(2);                 // 96 -> 48 kHz (any by-2), stereo, economy
// profiles: economy() (default, 3/8 of the lower rate flat) | balanced() |
// transparent() (120 dB) | super_economy()
std::vector<float> out(down.outputs_for(n_in) * 2);
std::size_t made = down.process(in, n_in, out.data());   // noexcept, alloc-free
std::vector<float> tail(down.flush_output_frames() * 2);
down.flush(tail.data());                                 // end of stream

converter<ratio_2_3> to_32k(1);            // 48 -> 32 kHz: one mixed stage, 33 MACs per output
auto lat = to_32k.latency_output_frames(); // exact: {32, 3} output frames = 32 / 3
```

The ratio is a compile-time type (`ratio<L, M>`, L and M of the form
2^a · 3^b in lowest terms: anything else fails to compile with the charter's
message). `pull(out, n, pop_fn)` is the callback-driven shape and
`frames_needed(n)` is exact arithmetic from the current position.

- `ratio.h` — `ratio<L, M>` (R1) and `ratio_traits`: the band index
  max(L, M) of the stage's one Nyquist filter, the composite factor, the
  lower rate, the direction flags and the 2 / 3 exponents the chain
  factoring reads.
- `design.h` — `profile` (`bridge`'s four names as (A, f_pass / r_min),
  each carrying the pinned taps per branch for the bands 2, 3, 4, 6, 8),
  `design_stage<R>(profile)`: the band's L-th-band design at the pinned
  length (a custom profile without pins is searched on the same
  16384-point grid), centre exactly 1, zeros exactly 0, every branch at DC
  gain 1; `stage_taps_per_phase<R>` for the mixed ratios.
- `stage.h` — `basic_stage<S, R>`: the L-phase schedule machine (bridge's)
  for interpolators and mixed ratios, each phase row trimmed to its nonzero
  span; the M-branch commutator for decimators, the nonzero branches summed
  under one finalize. Zero-primed and causal (scipy's `upfirdn` streaming
  prefix), bit-identical for any chunking, DC gain exactly 1 in every
  format.
- `converter.h` — `basic_converter<S, R>` and the `converter<R>`,
  `converter_q15<R>`, `converter_q31<R>` aliases.
- `rational.h` — the umbrella, `TAP_SR_VERSION_*` (0.4.0; 0.5.0 at M6) and
  the named ratios of the vocabulary (`up_2` … `ratio_3_8`).

## The boundaries are identity, not policy

- **Within a family only.** The 48 kHz family (8, 12, 16, 24, 32, 48, 96,
  192, 384 kHz) and the 44.1 kHz family (11.025, 22.05, 44.1, 88.2,
  176.4 kHz). Crossing them is the family's [`bridge`](../bridge/README.md)
  engine (147/160), reached by a chain the caller writes — never this
  engine. Absorbing a clock is [`async`](../async/README.md), by
  composition.
- **Never routed to by rate.** `ratio<L, M>` names the number and a chain
  names the stages; there is no `(in_hz, out_hz)` lookup anywhere. The
  plan's coverage matrix documents chains and dispatches nothing.
- **Speed-first.** The ratio is a compile-time type; the stage factoring
  is chosen by MACs (↑4 is two half-bands); the default `economy` profile
  takes the speed side of every inaudible trade, with a 120 dB
  `transparent` profile behind the same design path.

## Build

```sh
git clone --recurse-submodules https://github.com/tap/SampleRateTap
cmake -S SampleRateTap -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure -L '^rational$'
```

This engine lives in `rational/` of the SampleRateTap family repository; the
root build configures every engine and the `rational` label selects this
one's tests, including the family's dependency-rule checks for it and the
compile-fail charter test. `cmake -S SampleRateTap/rational` configures it on
its own. The design-spike notebook is committed executed; re-run it with
`jupyter nbconvert --to notebook --execute --inplace notebooks/design_spike.ipynb`
in the root `requirements.lock` environment when the design changes.
