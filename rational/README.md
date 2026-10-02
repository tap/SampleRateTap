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

> **Status: M6 of [PLAN.md](PLAN.md) section 6, the plan complete.** The single stages ship
> for every ratio of the vocabulary — ↑2, ↓2, ↑3, ↓3, ↑6, ↓6, ↑8, ↓8 and the
> mixed 3/2, 2/3, 4/3, 3/4, 8/3, 3/8 — as `converter<ratio<L, M>>` (float,
> the golden model pinned sample-for-sample against committed scipy
> `upfirdn` vectors), `converter_q15` and `converter_q31`, with the family's
> call shapes: `process`, `pull`, exact `outputs_for` / `frames_needed`,
> `flush`, `reset`, the latency as an exact rational. Each stage is one
> L-th-band filter at the pinned length of the M2 design spike
> ([`notebooks/design_spike.ipynb`](notebooks/design_spike.ipynb)); its
> structural zeros are never multiplied by an interpolator or a decimator,
> so the half-band decimator costs 23 MACs per output of its 43 taps and an
> interpolator's centre phase is a copy. The chains ship too (`chain.h`):
> `basic_chain<S, R...>` runs stages in sequence with each stage designed at
> its own rate, and the 20 named multi-stage chains of the 14 × 14 coverage
> matrix (PLAN.md section 3), pinned row by row by `tests/test_matrix.cpp`.
> The fixed-point profiles are measured per stage (`tests/test_fixed_point.cpp`,
> PLAN.md section 6): exact-unity rows and full-scale DC in Q15 and Q31,
> bit-pinned tables, saturation without wrap, Q31 within 3.4e−9 of double;
> Q15 is format-limited, and its numbers are stated per stage — at Q15 use
> `economy`, and note that a Q15 decimator by 6 or 8 attains −65 / −63 dB
> of stopband, not 70. The C ABI, the executed coverage-matrix notebook and
> the instruction-count ratchet landed at M6 (below). The
> plan is authoritative: charter, the decisions R1–R16, the generated
> matrix, layout, test strategy, non-goals and risks.

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

converter<ratio_2_3> to_32k(1);            // 48 -> 32 kHz: one mixed stage, 32.5 MACs per output
auto lat = to_32k.latency_output_frames(); // exact: {32, 3} output frames = 32 / 3

down_3_down_8_down_2<float> to_8k(1);      // 384 -> 8 kHz: the matrix's 1/48 chain, 373 MACs per output
// each stage designed at its own rate: divisors 16, 2, 1 of the chain's lowest rate (PLAN.md 3.1)
static_assert(to_8k.k_divisors[0] == tap::dsp::exact_ratio{16, 1});
chain<up_2, ratio_4_3> to_32k_from_12k(2); // 12 -> 32 kHz: 8/3 as the matrix factors it, stereo
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
- `chain.h` — `basic_chain<S, R...>`: DspTap's `chain<>` over
  `basic_stage`, constructing each stage at the profile relaxed by the
  stage's design divisor (its lower rate over the chain's lowest, the
  largest 2^a · 3^b at or below it; `profile::relaxed`, the pinned
  relaxation tables of `design.h`); `chain<R...>` / `chain_q15` /
  `chain_q31`; `macs_per_output()` exact; and the 20 named multi-stage
  chains of the coverage matrix (`up_2_up_2<S>` … `down_3_down_8_down_2<S>`).
- `rational.h` — the umbrella and `TAP_SR_VERSION_*` (0.5.0, from M6);
  the named ratios of the vocabulary (`up_2` … `ratio_3_8`) are `ratio.h`'s.

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
its own. The notebooks are committed executed; re-run them with
`jupyter nbconvert --to notebook --execute --inplace notebooks/<name>.ipynb`
in the root `requirements.lock` environment when what they measure changes:
`design_spike.ipynb` (the M2 pins, an independent numpy leg) and
`matrix.ipynb` (the coverage matrix through the C ABIs, below).

### C ABI

`capi/tap_sr_rational_capi.h` (`-DTAP_SR_BUILD_CAPI=ON`, or
`cmake -S rational/capi -B build_capi` on its own) exposes the float chains
to FFI consumers: `tap_sr_rational_create(chain, profile, channels)` takes
one of the 28 named within-family chains of the coverage matrix as a
constant (`TAP_SR_RATIONAL_UP_2` … `TAP_SR_RATIONAL_DOWN_3_DOWN_8_DOWN_2`),
never a rate; `tap_sr_rational_create_stage(L, M, profile, divisor_num,
divisor_den, channels)` is one stage of the vocabulary at a stated design
divisor, what a chain through `bridge` composes. Each converter reports its
exact accounting, its latency and its MACs per output as exact rationals,
per-stage design lengths, and the bit-packed family version
(`tap_sr_rational_version()`). `notebooks/tap_sr_rational_py.py` is the
ctypes binding the notebook measures the shipping C++ through.

### Embedded targets and the instruction-count ratchet

Every push runs this engine's emulation-sized battery on **Cortex-M33**
(QEMU mps2-an505), **Cortex-M55** (mps3-an547) and **Hexagon**
(qemu-hexagon, static musl), and gates twelve fixed workloads — the by-2
and by-3 stages both ways in float and Q15 at `economy`, the Q15 by-4
chain, the by-2 pair in float at `transparent`, and construction alone —
against committed per-target instruction counts (`bench/baselines.json`,
two-sided ±3 %), measured by the family's shared harness from the
repository root:

```sh
cmake -B build-m55 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
      -DTAP_SR_BUILD_TESTS=OFF -DTAP_SR_BUILD_EXAMPLES=OFF \
      -DTAP_SR_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55 -j
python3 scripts/icount.py --engine rational --target m55 --build-dir build-m55 \
      --plugin libinsncount.so
```

<!-- ICOUNT:BEGIN -->
Executed instructions per fixed workload (`rational/bench/icount/`), measured under QEMU with a counting plugin — deterministic, and gated in CI at ±3% against `rational/bench/baselines.json`:

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `construct_q15_eco` | 774,741 | 33,682 | 183,854 |
| `down2_down2_q15_eco` | 62,508,360 | 54,506,280 | 22,251,668 |
| `down2_float_eco` | 366,459,700 | 24,577,869 | 72,734,422 |
| `down2_float_tr` | 955,278,690 | 47,997,517 | 174,664,319 |
| `down2_q15_eco` | 47,494,712 | 25,070,113 | 18,554,089 |
| `down3_float_eco` | 476,573,580 | 31,908,454 | 90,345,997 |
| `down3_q15_eco` | 51,977,458 | 28,017,447 | 19,350,211 |
| `up2_float_eco` | 732,658,864 | 43,442,565 | 137,930,738 |
| `up2_float_tr` | 1,961,674,119 | 89,901,988 | 340,655,706 |
| `up2_q15_eco` | 67,354,778 | 47,493,112 | 28,133,275 |
| `up3_float_eco` | 1,407,620,075 | 75,330,937 | 254,903,952 |
| `up3_q15_eco` | 92,695,694 | 74,874,023 | 39,793,932 |
<!-- ICOUNT:END -->
