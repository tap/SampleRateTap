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
> for every ratio of the vocabulary — ↑2, ↓2, ↑3, ↓3, ↑4, ↓4, ↑6, ↓6, ↑8, ↓8
> and the mixed 3/2, 2/3, 4/3, 3/4, 8/3, 3/8 — as `converter<Ch, ratio<L, M>>` (float,
> the golden model pinned sample-for-sample against committed scipy
> `upfirdn` vectors), `converter_q15` and `converter_q31`, with the family's
> call shapes: `process`, `pull`, exact `outputs_for` / `frames_needed`,
> `flush`, `reset`, the latency as an exact rational. Each stage is one
> L-th-band filter at the pinned length of the M2 design spike
> ([`notebooks/design_spike.ipynb`](notebooks/design_spike.ipynb)), re-pinned
> since with the designer's Kaiser β for A + 4 dB (X17); its
> structural zeros are never multiplied by an interpolator or a decimator,
> so the half-band decimator costs 21 MACs per output of its 39 taps and an
> interpolator's centre phase is a copy. The chains ship too (`chain.h`):
> `basic_chain<S, Ch, R...>` runs stages in sequence with each stage designed at
> its own rate, and the 22 named multi-stage chains of the 14 × 14 coverage
> matrix (PLAN.md section 3), pinned row by row by `tests/test_matrix.cpp`.
> The fixed-point profiles are measured per stage (`tests/test_fixed_point.cpp`,
> PLAN.md section 6): exact-unity rows and full-scale DC in Q15 and Q31,
> bit-pinned tables, saturation without wrap, Q31 within 2.5e−9 of double on the
> reference noise (480 frames at peak 0.9; 1.5e−8 under full-scale drive);
> Q15 is format-limited, and its numbers are stated per stage — at Q15 use
> `economy`, where every stage of the vocabulary attains 70 dB of stopband at its own rate (a Q15 decimator
> quantizes each branch at unity and divides by M in its one rounding, so ↓6
> and ↓8 attain −71.8 / −72.9 dB, as ↑6 and ↑8 do; a mixed ratio going down
> holds its rows at a power-of-two gain, shifted out the same way; the relaxed designs a chain runs are
> measured too, and since the designer's β was re-tuned (X17) every one of them attains 70 dB as well, PLAN.md section 6). The C ABI, the executed coverage-matrix notebook and
> the instruction-count ratchet landed at M6 (below). The
> plan is authoritative: charter, the decisions R1–R16, the generated
> matrix, layout, test strategy, non-goals and risks.

## Quick start

```cpp
#include <tap/sr/rational/rational.h>

using namespace tap::sr::rational;

converter<2, down2> down;                  // 96 -> 48 kHz (any by-2), stereo, economy
// profiles: economy() (default, 3/8 of the lower rate flat) | balanced() |
// transparent() (120 dB) | super_economy()
std::vector<float> out(down.outputs_for(n_in) * 2);
std::size_t made = down.process(in, n_in, out.data());   // noexcept, alloc-free
std::vector<float> tail(down.flush_output_frames() * 2);
down.flush(tail.data());                                 // end of stream

converter<1, ratio2over3> to_32k;          // 48 -> 32 kHz: one mixed stage, 29.5 MACs per output
auto lat = to_32k.latency_output_frames(); // exact: {29, 3} output frames = 29 / 3

down4_down4_down3<float, 1> to_8k;         // 384 -> 8 kHz: the matrix's 1/48 chain, 344 MACs per output
// each stage designed at its own rate: divisors 12, 3, 1 of the chain's lowest rate (PLAN.md 3.1)
static_assert(to_8k.k_divisors[0] == tap::dsp::exact_ratio{12, 1});
chain<2, up2, ratio4over3> to_32k_from_12k; // 12 -> 32 kHz: 8/3 as the matrix factors it, stereo
```

The ratio is a compile-time type (`ratio<L, M>`, L and M of the form
2^a · 3^b in lowest terms: anything else fails to compile with the charter's
message). The channel count is the type's too, the first template argument
of the aliases (`converter<2, down2>`, `chain<2, up2, ratio4over3>`) and the
second of `basic_stage<S, Ch, R>` / `basic_chain<S, Ch, R...>`: a
compile-time constant of at least 1, never a constructor argument, and a
count of zero fails to compile (A13, `docs/CHANNELS_PLAN.md`). Construction
rejects a profile the designer refuses and a spec no length up to the
search bound meets (`std::invalid_argument`,
`std::runtime_error`); `converter<Ch, R>::validate(profile)` and a
chain's `validate` are the same verdict as a `noexcept` `const char*` reason
(nullptr when it constructs), the family's error model (root README,
"Errors") — what a build without exceptions calls before constructing. `pull(out, n, pop_fn)` is the callback-driven shape and
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
- `stage.h` — `basic_stage<S, Ch, R>`: the L-phase schedule machine (bridge's)
  for interpolators and mixed ratios, each phase row trimmed to its nonzero
  span; the M-branch commutator for decimators, the nonzero branches summed
  under one finalize. Zero-primed and causal (scipy's `upfirdn` streaming
  prefix), bit-identical for any chunking, DC gain exactly 1 in every
  format. Its count-independent state and cold code are `stage_base<S, R>`'s,
  built once per format and ratio whatever counts an image runs (A13's
  K12); only `process`, `pull` and `flush` are compiled per count, each out
  of line.
- `converter.h` — `basic_converter<S, Ch, R>` and the `converter<Ch, R>`,
  `converter_q15<Ch, R>`, `converter_q31<Ch, R>` aliases.
- `chain.h` — `basic_chain<S, Ch, R...>`: DspTap's `chain<>` over
  `basic_stage`, constructing each stage at the profile relaxed by the
  stage's design divisor (its lower rate over the chain's lowest, the
  largest 2^a · 3^b at or below it; `profile::relaxed`, the pinned
  relaxation tables of `design.h`); `chain<Ch, R...>` / `chain_q15` /
  `chain_q31`; `macs_per_output_exact()` the MACs per output as a reduced rational and
  `macs_per_output()` its double; and the 22 named multi-stage
  chains of the coverage matrix (`up2_up2<S, Ch>` … `down4_down4_down3<S, Ch>`;
  four of them, `up2_up6`, `up2_up8`, `down6_down2` and `down8_down2`, run
  no row at `economy` since the 4th-band stage was admitted and are kept for
  the per-tier matrix, which selects them at `transparent`).
- `rational.h` — the umbrella and `TAP_SR_VERSION_*` (0.7.0 since A13; 0.6.0 from the audit response, 0.5.0 from M6);
  the named ratios of the vocabulary (`up2` … `ratio3over8`) are `ratio.h`'s.

Against the general-purpose resampler libraries at four ratios of the
vocabulary — ↑2, ↓2, 3/2 and 2/3, against libsamplerate, soxr,
r8brain-free-src and SpeexDSP at the settings each needs to reach `economy`
and `transparent`, and at their best — quality, latency, host throughput and
embedded instruction counts are measured in
[docs/COMPARISON.md](docs/COMPARISON.md) (the notebook behind it:
[notebooks/rational_comparison.ipynb](notebooks/rational_comparison.ipynb);
the family index: [docs/COMPARISON.md](../docs/COMPARISON.md) at the root).

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
  is chosen by MACs (↑4 is two half-bands, and since 0.8.0 a 4th-band stage
  sits at the high-rate end of every chain by 8 and above, where it is the
  cheapest); the default `economy` profile
  takes the speed side of every inaudible trade, with a 120 dB
  `transparent` profile behind the same design path.

## Position in the Tap family

`rational` is one of the three engines of the `tap::sr` family, all built on
the same shared substrate and living in one tree:

```
                    ┌────────────────────────────┐
                    │           DspTap           │  shared substrate (submodules/dsptap)
                    │  kaiser · nyquist design · │
                    │  sample traits (float/Q15/ │
                    │  Q31) · FIR dot kernels ·  │
                    │  row-sum quantization ·    │
                    │  chain<> · analysis        │
                    └──────┬────────┬────────┬───┘
                           │        │        │
            ┌──────────────┴─┐ ┌────┴──────────┐ ┌┴────────────────┐
            │ tap::sr::async │ │ tap::sr::bridge│ │ tap::sr::rational│
            │ absorbs the    │ │ 44.1 ↔ 48 on  │ │ L/M inside one   │
            │ clock (servo)  │ │ one clock      │ │ rate family      │
            └────────────┬───┘ └──┬─────────────┘ └──────────────────┘
                         │        │
                         └── test-only ──  bridge's golden cross-validation
                             (bridge/tests/, bridge/examples/bluetooth_bridge)
```

[DspTap](https://github.com/tap/DspTap) (vendored at `submodules/dsptap`)
provides the L-th-band designer and the stage composition this engine is
built on, with the Kaiser design path, the float/Q15/Q31 sample-format
traits, the FIR dot kernels, the row-sum quantization and the analysis
instruments the three engines share. [`bridge`](../bridge/README.md) is the
sibling for the one cross-family pair, 44.1 ↔ 48 kHz, which a chain through
this engine's stages reaches as a type the caller writes (the coverage
matrix's cross rows, PLAN.md 3.5 / 3.6); [`async`](../async/README.md)
absorbs a clock, by composition. Which engine applies is a property of the
clock topology and the rate pair, never inferred from a float ratio
([the boundaries](#the-boundaries-are-identity-not-policy)). Each engine's
quality tiers are its own ladder, stated with its numbers in the root
README's profile table; this engine's four names are `bridge`'s, at the same
numbers.

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
`cmake -S rational/capi -B build_capi` on its own) exposes the chains to FFI
consumers in float and, through `tap_sr_rational_create_format(chain,
profile, format, channels)` with `TAP_SR_RATIONAL_FORMAT_Q15` / `_Q31`, in the
fixed-point profiles (`tap_sr_rational_process_q15` / `_q31` and the matching
flush; a call in another format than the converter's returns 0 and touches
nothing — the shape the siblings' ABIs share). `tap_sr_rational_create(chain, profile, channels)` takes
one of the 30 named within-family chains of the coverage matrix as a
constant (`TAP_SR_RATIONAL_UP2` … `TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN3`),
never a rate. The values are stable: a retired chain's value is never
reused and `create` refuses it (ten were retired in 0.8.0, listed in the
header), so the live values run from 0 to 39 with gaps and
`TAP_SR_RATIONAL_CHAIN_END` (40), one past the largest value ever assigned,
bounds them. `tap_sr_rational_create_stage(L, M, profile, divisor_num,
divisor_den, channels)` is one stage of the vocabulary, any of its sixteen
ratios, at a stated design divisor, what a chain through `bridge` composes. Each converter reports its
exact accounting, its latency and its MACs per output as exact rationals,
per-stage design lengths, and the bit-packed family version
(`tap_sr_rational_version()`). The C ABI takes the channel count at run time,
where the C++ types carry it: one and two channels run `basic_chain<S, 1, …>`
and `basic_chain<S, 2, …>`, and any other count that many one-channel
converters behind the handle, the frames split to them and interleaved back,
bit for bit the native type of that count
(`CApi.FanOutIsTheNativeConverterBitForBit`, 3, 6 and 12 channels in every
format; A13's K8, option (b)). `notebooks/tap_sr_rational_py.py` is the
ctypes binding the notebook measures the shipping C++ through
(`Chain(name, fmt="q15")` for the fixed-point formats).

### Embedded targets and the instruction-count ratchet

Every push runs this engine's emulation-sized battery on **Cortex-M33**
(QEMU mps2-an505), **Cortex-M55** (mps3-an547) and **Hexagon**
(qemu-hexagon, static musl), and gates fifteen fixed workloads — the by-2
and by-3 stages both ways in float and Q15 at `economy`, the Q15 by-4
chain and the Q15 by-8 chain (↓4 · ↓2, the 4th-band stage's, since 0.8.0),
the by-2 pair in float at `transparent`, the 2/3 stage in float and
Q15, and the Q15 by-4 chain's construction with its input fixture (its
stream removed, so the chain's row minus it is the streaming loop alone) —
and the worst single call of the ↑2, ↓2 and ↓4 stages in float and Q15,
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

The channel count moved into the type at A13 (`docs/CHANNELS_PLAN.md`), and its gain is
measured in instructions only: no cycle counts on hardware yet (K11, deferred to 0.7.0).
Against the baselines before it, three rows were accepted above gate 0's rule, and shipped
at the M55's `up2_q15_eco` −2.62 % and `up3_q15_eco` −3.70 % (+1.28 and +0.63 % at gate 0)
and the M33's `down3_float_eco` +1.29 % (+1.37 %). One row is gated at a midpoint: the M55's
`ratio23_q15_eco` counts 15,341,916 instructions with exceptions and 14,577,966 without (GCC
allocates the block's registers differently in the two builds, their GIMPLE identical:
`PLAN.md` section 6, the A13 record), so its baseline is the midpoint, 14,959,941, and both
builds sat within ±2.55 % of it. Re-record that row at the midpoint of the two builds' counts,
never with `--update` from one of them. Since X17's re-tune of the designer's β (every row
re-recorded, `PLAN.md` section 6, "The designer's β, X17") it counts 15,140,167 and 14,312,206,
the baseline is their midpoint, 14,726,186, and both builds sit within ±2.81 % of it.

<!-- ICOUNT:BEGIN -->
Executed instructions per fixed workload (`rational/bench/icount/`), measured under QEMU with a counting plugin — deterministic, and gated in CI at ±3% against `rational/bench/baselines.json`. A `callmax_` row is the worst single call instead of a total: the most instructions one call executes over that stream (`bench/icount/callmax_main.cpp`):

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `callmax_down2_float` | 107,977 | 7,605 | 21,543 |
| `callmax_down2_q15` | 6,685 | 5,624 | 3,763 |
| `callmax_down4_float` | 155,453 | 9,708 | 30,609 |
| `callmax_down4_q15` | 8,101 | 6,241 | 5,603 |
| `callmax_up2_float` | 210,879 | 12,524 | 40,873 |
| `callmax_up2_q15` | 11,488 | 9,212 | 5,880 |
| `construct_q15_eco` | 26,681,136 | 1,560,152 | 8,303,115 |
| `down2_down2_q15_eco` | 52,537,300 | 24,216,725 | 19,567,759 |
| `down2_float_eco` | 343,562,765 | 23,353,785 | 66,524,317 |
| `down2_float_tr` | 790,228,053 | 40,912,993 | 142,826,681 |
| `down2_q15_eco` | 45,602,994 | 16,879,879 | 16,905,026 |
| `down3_float_eco` | 434,872,408 | 27,445,097 | 82,591,396 |
| `down3_q15_eco` | 48,745,572 | 17,640,706 | 18,300,152 |
| `down4_down2_q15_eco` | 47,659,519 | 18,466,975 | 16,790,347 |
| `ratio23_float_eco` | 538,739,305 | 30,545,005 | 115,766,432 |
| `ratio23_q15_eco` | 48,128,267 | 14,726,186 | 19,232,880 |
| `up2_float_eco` | 650,842,262 | 38,707,334 | 123,990,515 |
| `up2_float_tr` | 1,542,330,040 | 73,546,460 | 275,750,044 |
| `up2_q15_eco` | 61,032,738 | 28,687,571 | 25,064,644 |
| `up3_float_eco` | 1,243,186,839 | 66,371,040 | 229,335,777 |
| `up3_q15_eco` | 82,905,394 | 42,624,043 | 36,815,849 |

On the host, the float converter in 128-frame blocks at 2 and 12 channels (`rational/bench/icount/host_main.cpp`): instructions executed by the streaming loop alone, counted under callgrind on x86-64 with GCC 13 (`scripts/icount.py --target host`), gated the same way:

| Workload | x86-64 |
|---|---:|
| `down2_float_12ch` | 146,817,769 |
| `down2_float_2ch` | 26,912,629 |
| `down4_float_12ch` | 194,807,923 |
| `down4_float_2ch` | 34,142,581 |
| `up2_float_12ch` | 256,387,855 |
| `up2_float_2ch` | 48,551,143 |
<!-- ICOUNT:END -->

## Footprint

One stage's heap, after construction, is a formula in its geometry (N = 2mB − 1 taps
from `taps()`, T the row length from `row_length()`, R the stored rows `k_rows`, P the
history lines per channel, which is M for a decimator and 1 otherwise; `c` the
coefficient size, `s` the sample size, `ch` channels):

```text
held = R T c                      rows
     + (T + 64) P ch s            history: one planar allocation, P x ch lines, 64 frames of append slack
     + 16 ch s                    pull()'s pop scratch
peak = max(design, 8 N + held)    the double design lives to the end of the constructor
```

A chain holds its stages, a scratch between each pair (`cap_i ch s`, where `cap_i` is
the most a 64-frame chunk can reach stage `i` with, `tap::dsp::chain`) and 64 `ch s`
of zeros for its flush. Its peak depends on the order its stages are constructed in,
which C++ leaves unspecified, so the test reports it and does not pin it. Stages at
`economy`, stereo:

| stage | N | f32 held | f32 peak | Q15 held | Q15 peak | Q31 held | Q31 peak |
|---|---:|---:|---:|---:|---:|---:|---:|
| `up2` | 39 | 960 | 1,272 | 480 | 792 | 960 | 1,272 |
| `down2` | 39 | 1,632 | 1,944 | 816 | 1,128 | 1,632 | 1,944 |
| `up3` | 59 | 1,040 | 1,512 | 520 | 992 | 1,040 | 1,512 |
| `down3` | 59 | 2,384 | 2,856 | 1,192 | 1,664 | 2,384 | 2,856 |
| `up4` | 79 | 1,120 | 1,752 | 560 | 1,192 | 1,120 | 1,752 |
| `down4` | 79 | 3,136 | 3,768 | 1,568 | 2,200 | 3,136 | 3,768 |
| `up6` | 119 | 1,280 | 2,232 | 640 | 1,592 | 1,280 | 2,232 |
| `down6` | 119 | 4,640 | 5,592 | 2,320 | 3,272 | 4,640 | 5,592 |
| `up8` | 159 | 1,440 | 2,712 | 720 | 1,992 | 1,440 | 2,712 |
| `down8` | 159 | 6,144 | 7,416 | 3,072 | 4,344 | 6,144 | 7,416 |
| `ratio3over2` | 59 | 1,040 | 1,512 | 520 | 992 | 1,040 | 1,512 |
| `ratio2over3` | 59 | 1,120 | 1,592 | 560 | 1,132 | 1,120 | 1,592 |
| `ratio4over3` | 79 | 1,120 | 1,752 | 560 | 1,192 | 1,120 | 1,752 |
| `ratio3over4` | 79 | 1,180 | 1,812 | 590 | 1,280 | 1,180 | 1,812 |
| `ratio8over3` | 159 | 1,440 | 2,712 | 720 | 1,992 | 1,440 | 2,712 |
| `ratio3over8` | 159 | 1,700 | 2,972 | 850 | 2,544 | 1,700 | 2,972 |

Across the profiles a stage grows with its pins: `up2` f32 stereo holds 896 / 960 /
1,024 / 1,440 bytes at `super_economy` / `economy` / `balanced` / `transparent`, and
`down2` 1,536 / 1,632 / 1,728 / 2,352. The named chains hold 3.3 KB (`up2_up2`,
`up2_up3`) to 10.9 KB (`up3_up4_up4`) at `economy`, f32 stereo, and up to 11.8 KB at
`transparent` (the pins of the designer's β for A + 4 dB, X17; the chains since the
4th-band stage, 0.8.0).

The figures are bytes, and the formula has no pointer-sized term (the history is one
allocation since the roadmap's A12; before it, each channel's each line was a vector of
its own, `P ch sizeof(std::vector)` more), so a 32-bit target holds the same.
`tests/test_footprint.cpp` pins the stage formulas, held and peak, for all 16 ratios
of the vocabulary, every profile and format, and every named chain's held bytes,
against DspTap's heap meter (`tap/dsp/analysis/heap_meter.h`) exactly, on the hosted legs
(not under ASan or TSan, whose runtime owns the allocator) and on Hexagon (32-bit), and checks that processing allocates nothing. On Windows x64 the allocator asks for more than the formula: MSVC's `std::allocator` pads every
request of 4096 bytes or more by 39 bytes to align it to 32 bytes, and the test counts each
allocation through DspTap's model of that (`heap_meter::allocator_request`).

### Code size and stack

The code a deployment links and the stack each call needs are gated beside the
instruction counts, by `scripts/ceilings.py` against `bench/ceilings.json`, on one
probe per format (`bench/icount/probe_main.cpp`): the by-2 pair at the default
profile, stereo, an `up2` stage and a `down2` stage each streaming 32-frame blocks
through `process()` (the `up` and `down` calls), built Release in the ratchet's tree;
and, since 0.8.0, a second probe per format the same way for the 4th-band pair, an
`up4` and a `down4` stage (the rows marked "by 4").

- **Engine `.text` and `.rodata`** are the probe's own translation unit, from its
  link map: the two stages as instantiated.
- **Image** is the whole linked probe's `.text` + `.rodata`. That includes the runtime
  it pulls in (newlib or musl, the C++ library, libgcc's soft double, libm) and the
  startup.
- **Static worst path** (M33) is the deepest chain of frames from the call. It takes
  GCC's frames (`-fstack-usage -fcallgraph-info=su`) for the compiled code and the
  disassembly's for the precompiled runtime routines the call reaches, so it bounds
  every path.
- **Measured high-water** (M33) is what the call used, painted and read under QEMU
  over a second of blocks. It is never above the bound; the script fails if it is.

Each ceiling is the recorded figure plus 3 %, rounded up to 64 bytes for sizes or 8
for stack. A change that moves a figure past its ceiling re-records the file in the
same PR, with the reason.

<!-- ceilings:begin (scripts/ceilings.py docs; do not edit by hand) -->
| target | format | engine `.text` (bytes) | ceiling | engine `.rodata` (bytes) | ceiling | image (bytes) | ceiling |
|---|---|--:|--:|--:|--:|--:|--:|
| m33 | f32 | 9,288 | 9,600 | 1,050 | 1,088 | 43,849 | 45,184 |
| m33 | q15 | 10,776 | 11,136 | 1,050 | 1,088 | 45,545 | 46,912 |
| m33 | q31 | 10,492 | 10,816 | 1,050 | 1,088 | 45,249 | 46,656 |
| m33 | f32 (by 4) | 9,308 | 9,600 | 978 | 1,024 | 43,785 | 45,120 |
| m33 | q15 (by 4) | 10,836 | 11,200 | 978 | 1,024 | 45,529 | 46,912 |
| m33 | q31 (by 4) | 10,512 | 10,880 | 978 | 1,024 | 45,209 | 46,592 |
| m55 | f32 | 8,084 | 8,384 | 1,050 | 1,088 | 43,857 | 45,184 |
| m55 | q15 | 9,480 | 9,792 | 1,050 | 1,088 | 45,561 | 46,976 |
| m55 | q31 | 9,260 | 9,600 | 1,050 | 1,088 | 45,345 | 46,720 |
| m55 | f32 (by 4) | 8,036 | 8,320 | 978 | 1,024 | 43,737 | 45,056 |
| m55 | q15 (by 4) | 9,524 | 9,856 | 978 | 1,024 | 45,537 | 46,912 |
| m55 | q31 (by 4) | 9,276 | 9,600 | 978 | 1,024 | 45,289 | 46,656 |
| hexagon | f32 | 19,700 | 20,352 | 428 | 448 | 297,048 | 305,984 |
| hexagon | q15 | 22,008 | 22,720 | 428 | 448 | 299,992 | 308,992 |
| hexagon | q31 | 20,888 | 21,568 | 428 | 448 | 298,904 | 307,904 |
| hexagon | f32 (by 4) | 22,356 | 23,040 | 356 | 384 | 299,664 | 308,672 |
| hexagon | q15 (by 4) | 26,068 | 26,880 | 356 | 384 | 303,984 | 313,152 |
| hexagon | q31 (by 4) | 23,952 | 24,704 | 356 | 384 | 301,904 | 310,976 |

| M33 call | format | static worst path (bytes) | ceiling | measured high-water (bytes) | ceiling |
|---|---|--:|--:|--:|--:|
| `up` | f32 | 160 | 168 | 160 | 168 |
| `up` | q15 | 144 | 152 | 128 | 136 |
| `up` | q31 | 144 | 152 | 144 | 152 |
| `up` | f32 (by 4) | 160 | 168 | 160 | 168 |
| `up` | q15 (by 4) | 144 | 152 | 128 | 136 |
| `up` | q31 (by 4) | 144 | 152 | 144 | 152 |
| `down` | f32 | 184 | 192 | 184 | 192 |
| `down` | q15 | 184 | 192 | 164 | 176 |
| `down` | q31 | 160 | 168 | 160 | 168 |
| `down` | f32 (by 4) | 200 | 208 | 200 | 208 |
| `down` | q15 (by 4) | 208 | 216 | 188 | 200 |
| `down` | q31 (by 4) | 184 | 192 | 184 | 192 |
<!-- ceilings:end -->

## Non-finite input and denormals

A NaN or ±Inf input sample corrupts only the outputs whose windows, stage by stage, cover it, on
its own channel, and every output after them is bit-identical to a chain's that never saw it,
because each stage is a pure FIR with nothing computed from a sample's value
(`non_finite_test.*`, `tests/test_non_finite.cpp`, in float and double: a 24-frame burst reaches
110 outputs through ↑3, 31 through ↓2 and 343 through the 8/1 chain, ↑2 · ↑4). The float datapath accumulates in double, where every float denormal is a normal number, so no
denormal arises inside a dot product; the output, rounded back to float, may carry denormals onward
(a decaying tail, a denormal input passed through), and the converter does not flush them. Q15 and Q31
have neither.
