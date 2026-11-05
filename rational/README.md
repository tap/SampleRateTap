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
> bit-pinned tables, saturation without wrap, Q31 within 3.4e−9 of double on the
> reference noise (480 frames at peak 0.9; 1.6e−8 under full-scale drive);
> Q15 is format-limited, and its numbers are stated per stage — at Q15 use
> `economy`, where every stage of the vocabulary attains 70 dB of stopband at its own rate (a Q15 decimator
> quantizes each branch at unity and divides by M in its one rounding, so ↓6
> and ↓8 attain −71.5 / −71.7 dB, as ↑6 and ↑8 do; a mixed ratio going down
> holds its rows at a power-of-two gain, shifted out the same way; the relaxed designs a chain runs are
> measured too, and the `economy` half-band at the 147/160 divisor attains −67.6 dB, PLAN.md section 6). The C ABI, the executed coverage-matrix notebook and
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
message). Construction rejects zero channels, a profile the designer refuses
and a spec no length up to the search bound meets (`std::invalid_argument`,
`std::runtime_error`); `converter<R>::validate(channels, profile)` and a
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
  `chain_q31`; `macs_per_output_exact()` the MACs per output as a reduced rational and
  `macs_per_output()` its double; and the 20 named multi-stage
  chains of the coverage matrix (`up_2_up_2<S>` … `down_3_down_8_down_2<S>`).
- `rational.h` — the umbrella and `TAP_SR_VERSION_*` (0.6.0 since the audit response; 0.5.0 from M6);
  the named ratios of the vocabulary (`up_2` … `ratio_3_8`) are `ratio.h`'s.

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
  is chosen by MACs (↑4 is two half-bands); the default `economy` profile
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
one of the 28 named within-family chains of the coverage matrix as a
constant (`TAP_SR_RATIONAL_UP_2` … `TAP_SR_RATIONAL_DOWN_3_DOWN_8_DOWN_2`),
never a rate; `tap_sr_rational_create_stage(L, M, profile, divisor_num,
divisor_den, channels)` is one stage of the vocabulary at a stated design
divisor, what a chain through `bridge` composes. Each converter reports its
exact accounting, its latency and its MACs per output as exact rationals,
per-stage design lengths, and the bit-packed family version
(`tap_sr_rational_version()`). `notebooks/tap_sr_rational_py.py` is the
ctypes binding the notebook measures the shipping C++ through
(`Chain(name, fmt="q15")` for the fixed-point formats).

### Embedded targets and the instruction-count ratchet

Every push runs this engine's emulation-sized battery on **Cortex-M33**
(QEMU mps2-an505), **Cortex-M55** (mps3-an547) and **Hexagon**
(qemu-hexagon, static musl), and gates fourteen fixed workloads — the by-2
and by-3 stages both ways in float and Q15 at `economy`, the Q15 by-4
chain, the by-2 pair in float at `transparent`, the 2/3 stage in float and
Q15, and the Q15 by-4 chain's construction with its input fixture (its
stream removed, so the chain's row minus it is the streaming loop alone) —
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
Executed instructions per fixed workload (`rational/bench/icount/`), measured under QEMU with a counting plugin — deterministic, and gated in CI at ±3% against `rational/bench/baselines.json`. A `callmax_` row is the worst single call instead of a total: the most instructions one call executes over that stream (`bench/icount/callmax_main.cpp`):

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `callmax_down2_float` | 118,191 | 8,131 | 23,943 |
| `callmax_down2_q15` | 7,224 | 5,735 | 4,268 |
| `callmax_up2_float` | 231,726 | 14,175 | 44,683 |
| `callmax_up2_q15` | 12,391 | 9,607 | 6,304 |
| `construct_q15_eco` | 26,795,397 | 1,563,967 | 8,330,417 |
| `down2_down2_q15_eco` | 55,160,639 | 24,552,975 | 22,071,551 |
| `down2_float_eco` | 373,681,206 | 24,882,828 | 72,885,988 |
| `down2_float_tr` | 970,180,513 | 48,302,560 | 174,817,755 |
| `down2_q15_eco` | 47,081,268 | 17,059,962 | 18,048,480 |
| `down3_float_eco` | 467,685,055 | 29,237,795 | 90,250,094 |
| `down3_q15_eco` | 49,852,601 | 18,116,226 | 19,549,385 |
| `ratio23_float_eco` | 591,237,741 | 33,460,544 | 125,408,437 |
| `ratio23_q15_eco` | 49,645,786 | 15,451,527 | 18,689,641 |
| `up2_float_eco` | 712,003,791 | 43,649,236 | 134,224,931 |
| `up2_float_tr` | 1,902,619,470 | 90,108,510 | 336,949,711 |
| `up2_q15_eco` | 63,617,031 | 29,508,255 | 26,236,602 |
| `up3_float_eco` | 1,366,708,994 | 75,348,550 | 249,845,161 |
| `up3_q15_eco` | 87,322,867 | 44,313,203 | 38,272,880 |

On the host, the float converter in 128-frame blocks at 2 and 12 channels (`rational/bench/icount/host_main.cpp`): instructions executed by the streaming loop alone, counted under callgrind on x86-64 with GCC 13 (`scripts/icount.py --target host`), gated the same way:

| Workload | x86-64 |
|---|---:|
| `down2_float_12ch` | 161,390,269 |
| `down2_float_2ch` | 28,551,355 |
| `up2_float_12ch` | 282,912,211 |
| `up2_float_2ch` | 54,413,191 |
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
| `up_2` | 43 | 992 | 1,336 | 496 | 840 | 992 | 1,336 |
| `down_2` | 43 | 1,680 | 2,024 | 840 | 1,184 | 1,680 | 2,024 |
| `up_3` | 65 | 1,080 | 1,600 | 540 | 1,060 | 1,080 | 1,600 |
| `down_3` | 65 | 2,456 | 2,976 | 1,228 | 1,748 | 2,456 | 2,976 |
| `up_6` | 143 | 1,408 | 2,552 | 704 | 1,864 | 1,408 | 2,552 |
| `down_6` | 143 | 4,928 | 6,072 | 2,464 | 3,608 | 4,928 | 6,072 |
| `up_8` | 191 | 1,600 | 3,128 | 800 | 2,344 | 1,600 | 3,128 |
| `down_8` | 191 | 6,528 | 8,056 | 3,264 | 4,792 | 6,528 | 8,056 |
| `ratio_3_2` | 65 | 1,080 | 1,600 | 540 | 1,060 | 1,080 | 1,600 |
| `ratio_2_3` | 65 | 1,168 | 1,688 | 584 | 1,246 | 1,168 | 1,688 |
| `ratio_4_3` | 87 | 1,168 | 1,864 | 584 | 1,280 | 1,168 | 1,864 |
| `ratio_3_4` | 87 | 1,220 | 1,916 | 610 | 1,392 | 1,220 | 1,916 |
| `ratio_8_3` | 191 | 1,600 | 3,128 | 800 | 2,344 | 1,600 | 3,128 |
| `ratio_3_8` | 191 | 1,920 | 3,448 | 960 | 3,064 | 1,920 | 3,576 |

Across the profiles a stage grows with its pins: `up_2` f32 stereo holds 928 / 992 /
1,056 / 1,632 bytes at `super_economy` / `economy` / `balanced` / `transparent`, and
`down_2` 1,584 / 1,680 / 1,776 / 2,640. The named chains hold 3.4 KB (`up_2_up_3`) to
12.7 KB (`up_2_up_8_up_3`) at `economy`, f32 stereo, and up to 14.1 KB at
`transparent`.

The figures are bytes, and the formula has no pointer-sized term (the history is one
allocation since the roadmap's A12; before it, each channel's each line was a vector of
its own, `P ch sizeof(std::vector)` more), so a 32-bit target holds the same.
`tests/test_footprint.cpp` pins the stage formulas, held and peak, for all 14 ratios
of the vocabulary, every profile and format, and every named chain's held bytes,
against DspTap's heap meter (`tap/dsp/analysis/heap_meter.h`) exactly, on the hosted legs
(not under ASan or TSan, whose runtime owns the allocator) and on Hexagon (32-bit), and checks that processing allocates nothing. On Windows x64 the allocator asks for more than the formula: MSVC's `std::allocator` pads every
request of 4096 bytes or more by 39 bytes to align it to 32 bytes, and the test counts each
allocation through DspTap's model of that (`heap_meter::allocator_request`).

### Code size and stack

The code a deployment links and the stack each call needs are gated beside the
instruction counts, by `scripts/ceilings.py` against `bench/ceilings.json`, on one
probe per format (`bench/icount/probe_main.cpp`): the by-2 pair at the default
profile, stereo, an `up_2` stage and a `down_2` stage each streaming 32-frame blocks
through `process()` (the `up` and `down` calls), built Release in the ratchet's tree.

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
| m33 | f32 | 10,948 | 11,328 | 996 | 1,088 | 45,481 | 46,848 |
| m33 | q15 | 13,152 | 13,568 | 996 | 1,088 | 47,897 | 49,344 |
| m33 | q31 | 12,680 | 13,120 | 996 | 1,088 | 47,417 | 48,896 |
| m55 | f32 | 10,088 | 10,432 | 996 | 1,088 | 45,833 | 47,232 |
| m55 | q15 | 12,292 | 12,672 | 996 | 1,088 | 48,345 | 49,856 |
| m55 | q31 | 11,664 | 12,032 | 996 | 1,088 | 47,721 | 49,216 |
| hexagon | f32 | 25,704 | 26,496 | 428 | 448 | 303,144 | 312,256 |
| hexagon | q15 | 32,496 | 33,472 | 428 | 448 | 310,600 | 319,936 |
| hexagon | q31 | 31,596 | 32,576 | 428 | 448 | 309,704 | 319,040 |

| M33 call | format | static worst path (bytes) | ceiling | measured high-water (bytes) | ceiling |
|---|---|--:|--:|--:|--:|
| `up` | f32 | 192 | 200 | 192 | 200 |
| `up` | q15 | 192 | 200 | 176 | 184 |
| `up` | q31 | 200 | 208 | 200 | 208 |
| `down` | f32 | 208 | 216 | 208 | 216 |
| `down` | q15 | 200 | 208 | 184 | 192 |
| `down` | q31 | 184 | 192 | 184 | 192 |
<!-- ceilings:end -->

## Non-finite input and denormals

A NaN or ±Inf input sample corrupts only the outputs whose windows, stage by stage, cover it, on
its own channel, and every output after them is bit-identical to a chain's that never saw it,
because each stage is a pure FIR with nothing computed from a sample's value
(`non_finite_test.*`, `tests/test_non_finite.cpp`, in float and double: a 24-frame burst reaches
114 outputs through ↑3, 33 through ↓2 and 401 through the 8/1 chain). The float datapath accumulates in double, where every float denormal is a normal number, so no
denormal arises inside a dot product; the output, rounded back to float, may carry denormals onward
(a decaying tail, a denormal input passed through), and the converter does not flush them. Q15 and Q31
have neither.
