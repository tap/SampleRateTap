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

> **Status: M2 of [PLAN.md](PLAN.md) section 6.** The tree, the
> compile-time ratio type `ratio<L, M>` whose charter is a `static_assert`,
> `ratio_traits`, the four profiles carrying the **pinned** stage designs
> (the M2 design spike, [`notebooks/design_spike.ipynb`](notebooks/design_spike.ipynb),
> executed: every band ∈ {2, 3, 4, 6, 8} × profile, the minimal L-th-band
> length meeting the stopband with ≥ 1 dB margin on a 16384-point grid —
> economy half-band 43 taps, third-band 65, 8th-band 191; transparent
> 123 / 149 / 399 — with measured worst stopband and ripple, enforced by
> `tests/test_design.cpp`), and `design_stage<R>` over `tap::dsp::nyquist.h`.
> No stage, chain or converter ships yet: M3 lands the stages, M4 the
> chains and the 14 × 14 coverage matrix, M5 the fixed-point profiles, M6
> the C ABI and the ratchet baselines. The plan is authoritative: charter,
> the decisions R1–R16, the generated matrix, layout, test strategy,
> non-goals and risks.

## What exists today

```cpp
#include <tap/sr/rational/rational.h>

using namespace tap::sr::rational;

using t = ratio_traits<ratio_2_3>;         // 48 -> 32 kHz: one mixed stage
static_assert(t::k_band == 3);             // a third-band filter at 2 f_in
static_assert(t::k_lower_rate_num == 2 && t::k_lower_rate_den == 3);

std::vector<double> h = design_stage<up_2>(profile::economy()); // 43 taps (pinned), h[21] == 1.0 exactly
std::size_t t = stage_taps_per_phase<ratio_2_3>(profile::economy());  // 33: the mixed stage's MACs per output
// ratio<5, 1>, ratio<4, 2>, ratio<2, 2>: compile errors carrying the charter's message
```

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
- `rational.h` — the umbrella, `TAP_SR_VERSION_*` (0.4.0; 0.5.0 at M6) and
  the named single-stage ratios of the vocabulary.

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
