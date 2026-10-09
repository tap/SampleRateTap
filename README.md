# <picture><source media="(prefers-color-scheme: dark)" srcset=".github/icon-dark.svg"><img src=".github/icon-light.svg" width="40" height="40" alt="" align="top"></picture> SampleRateTap — the `tap::sr` sample-rate family

[![CI](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

One tree, three engines, one substrate. Header-only C++20, namespace `tap::sr`,
built on the Tap family's shared FIR substrate [DspTap](submodules/dsptap)
(`tap::dsp`: Kaiser design, sample-format traits, FIR dot kernels, row-sum
quantization, measurement instruments).

| Engine | Namespace | What it does | Where |
|---|---|---|---|
| **`async`** | `tap::sr::async` | Asynchronous, near-unity (±`max_deviation_ppm`, default 1000 ppm): two clock domains at nominally the same rate, one thread pushing at the input clock and one pulling at the output clock. **Absorbs the clock.** | [`async/`](async/README.md) |
| **`bridge`** | `tap::sr::bridge` | Synchronous 44.1 ↔ 48 kHz (160/147 up, 147/160 down) and the pair at 2× and 4× (88.2 ↔ 96, 176.4 ↔ 192), direction and rate scale fixed at compile time, speed-first with Q15/Q31 profiles for M33/M55-class targets. **Converts the number.** | [`bridge/`](bridge/README.md) |
| **`rational`** | `tap::sr::rational` | Synchronous small-factor L/M *within* a rate family (L, M ∈ {2^a·3^b}: ↑2, ↓3, 2/3, …), as chains of Nyquist (L-th-band) stages, the ratio a compile-time type. **Converts the number.** M1–M6 landed (ratio types, pinned designs, the single stages for every ratio of the vocabulary, the named chains and the 182-row coverage matrix generated from the measured lengths, the fixed-point profiles measured per stage, the C ABI, the executed matrix notebook and the instruction-count ratchet): its plan is complete. | [`rational/`](rational/README.md) |

The engines never route by rate. The caller declares the clock topology by
choosing a type: `async` when the clocks are independent, `bridge` when the
ratio is the fixed 44.1/48 pair, `rational` when it is a small-factor L/M
inside one family, and their composition — `bridge` converts the number,
`async` absorbs the clock — for 44.1 ↔ 48 across independent
clocks ([`bridge/examples/bluetooth_bridge.cpp`](bridge/examples/bluetooth_bridge.cpp)).
`async` is never chained behind a lookup, and no rate is ever served by an
engine whose charter does not name it (`PLAN.md`, D12 and section 2). The
engines planned next — `pdm`, `varispeed` — get their own charters and
directories the same way.

## Profiles

Each engine's quality tiers are its own ladder, and a profile name means one
thing across the family (the 2026-10 audit response, A4): `bridge` and
`rational` share four names at the same numbers, a Nyquist stopband and a
passband edge; `async`'s tiers are image rejection through its interpolated
polyphase bank, a different kind of number, so they keep their own names
(`preset` retired for `profile` in 0.6.0, and its program-weighted tier is
`program`, not `economy`).

| Engine | Profile | What the number is | Numbers |
|---|---|---|---|
| `bridge`, `rational` | `super_economy` | stopband A, passband edge as a fraction of the lower rate | 70 dB, 1/3 (16 kHz at 48) |
| | `economy` (default) | | 70 dB, 3/8 (18 kHz at 48) |
| | `balanced` | | 70 dB, 19/48 (19 kHz at 48) |
| | `transparent` | | 120 dB, 5/12 (20 kHz at 48) |
| `async` | `fast` | prototype stopband through the bank (image rejection); passband flat to 20 kHz at 48 | 96 dB-class, the legacy budget tier |
| | `balanced` (default) | | 120 dB, L = 256 × T = 48 |
| | `transparent` | | 140 dB-class, the long filter |
| | `program` | program-weighted: two-thirds of balanced's compute at balanced-class rejection where program energy lives; worst-case single sine near Nyquist 96 dB-class | the measured trade in `async/README.md` and the book's epilogue |

Every number above is measured and pinned by the engine's own tests
(`bridge/include/tap/sr/bridge/design.h`, `rational/include/tap/sr/rational/design.h`,
`async/include/tap/sr/async/polyphase_filter.h` state them with their
provenance); the fixed-point profiles' attained numbers are stated per
stage in each engine's plan.

## Quick start

```cmake
add_subdirectory(SampleRateTap)               # or FetchContent; submodules: recursive
target_link_libraries(app PRIVATE tap::sr::async)    # one engine
target_link_libraries(app PRIVATE tap::sr::bridge)   # another
target_link_libraries(app PRIVATE tap::sr::rational) # the third
target_link_libraries(app PRIVATE tap::sr)           # all of them (the umbrella)
```

```cpp
#include <tap/sr/async/async.h>   // tap::sr::async::converter, converter_q15, converter_q31
#include <tap/sr/bridge/ratio.h>  // tap::sr::bridge::converter_to_48k, converter_to_44k1, ...
#include <tap/sr/rational/rational.h> // tap::sr::rational::converter<ratio<L, M>>, converter_q15, ...
```

Every configure builds every engine. CI selects an engine only when it runs
tests, by ctest label. The family options are `TAP_SR_*`:

| Option | Default | Builds |
|---|---|---|
| `TAP_SR_BUILD_TESTS` | ON | the engines' tests and the family's own (`tests/`) |
| `TAP_SR_BUILD_EXAMPLES` | ON | the engines' examples |
| `TAP_SR_BUILD_CAPI` | OFF | the engines' C ABI shared libraries (`libtap_sr_async_capi`, `libtap_sr_bridge_capi`, `libtap_sr_rational_capi`) |
| `TAP_SR_BUILD_ICOUNT_BENCH` | OFF | every engine's instruction-count ratchet workloads |
| `TAP_SR_BUILD_BENCHMARKS`, `TAP_SR_BUILD_COMPARE_BENCH`, `TAP_SR_BUILD_COMPARE_SHIM` | OFF | the async engine's host-only benchmarks; the engines' resampler comparison benchmarks; the comparison notebooks' competitor shims (`tools/compare/`) |
| `TAP_SR_ASYNC_WERROR`, `TAP_SR_BRIDGE_WERROR`, `TAP_SR_RATIONAL_WERROR` | OFF | warnings as errors, per engine |

A retired pre-family option (`SRT_*`, `TAP_RATIO_*`) fails the configure
loudly (`cmake/retired_options.cmake`) rather than dropping a gate silently.

**Version.** One family version, `TAP_SR_VERSION_{MAJOR,MINOR,PATCH}`
(0.6.0, the minor bump the async vocabulary pass brought; 0.5.0 was the third engine's C ABI), defined
identically in each engine's umbrella header and returned bit-packed —
`(major << 16) | (minor << 8) | patch` — by each C ABI's
`tap_sr_async_version()` / `tap_sr_bridge_version()` /
`tap_sr_rational_version()`. Tags are `vX.Y.Z`.

## Build and test

```sh
git clone --recurse-submodules https://github.com/tap/SampleRateTap
cmake -S SampleRateTap -B build -DCMAKE_BUILD_TYPE=Release \
      -DTAP_SR_ASYNC_WERROR=ON -DTAP_SR_BRIDGE_WERROR=ON -DTAP_SR_RATIONAL_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure                 # everything
ctest --test-dir build --output-on-failure -L '^async$'    # one engine
ctest --test-dir build --output-on-failure -L '^bridge$'
ctest --test-dir build --output-on-failure -L '^rational$'
scripts/tidy.sh                                            # the CI clang-tidy gate, locally
```

Tests are GoogleTest, named `<engine>.<Suite>.<Test>` and labelled by
engine. The family's own tests (`tests/`, under every label) enforce the
dependency rule below, the version macros and, when the C ABIs are built,
each library's exported symbol set. CI runs the
battery on Linux (GCC, Clang, ASan/UBSan, TSan), macOS and Windows, on
Cortex-M33 and Cortex-M55 under `qemu-system-arm`, on Hexagon under
`qemu-hexagon`, and on arm64; the instruction-count ratchet
(`scripts/icount.py --engine async|bridge|rational`) gates every workload of
every engine two-sided at ±3 % against the committed `<engine>/bench/baselines.json`.

## Layout

```
CMakeLists.txt          the family: options (D9), tap::dsp once, the three engines, tap::sr, tests/
PLAN.md  CLAUDE.md      the family plan (charters, coverage rule, decisions) and working rules
LICENSE  STYLE.md       MIT; the shared Tap house style (.clang-format, .clang-tidy)
submodules/dsptap       the substrate, pinned once
cmake/  platform/       toolchains (Cortex-M33/M55, Hexagon), bare-metal startup, retired-option tripwire
scripts/                icount.py (the ratchet), tidy.sh, fetch_hexagon_toolchain.sh, doc updaters
tools/qemu_insn_plugin  the QEMU instruction-counting plugin
tools/compare           the resampler comparisons' competitor tooling (fetch recipes, notebook shims)
tests/                  the family's own tests: the dependency rule, the version macros, the exported symbols
book/                   the family's book (published at https://tap.github.io/SampleRateTap/, the
                        print edition beside it as SampleRateTap.pdf; book/print builds it)
docs/                   Doxyfile; the family comparison index (COMPARISON.md); the migration's run record; the 2026-10 audit and its response plan
async/                  include/tap/sr/async  tests  bench  examples  capi  notebooks  docs  README  PLAN
bridge/                 include/tap/sr/bridge tests  bench  examples  capi  notebooks  docs  README  PLAN
rational/               include/tap/sr/rational tests bench examples capi notebooks tools README  PLAN
```

## The dependency rule

`async`, `bridge` and `rational` each depend on `tap::dsp` only. An engine
may reach a sibling only under its own `tests/` and `examples/` — bridge's
golden cross-validation against async, `bluetooth_bridge`, and rational's
coverage matrix composing bridge for the cross-family rows — never from a
shipped header; `capi/` is per engine. `tests/` enforces this per engine:
the link interface is exactly `tap::dsp`, every public header compiles in
its own translation unit with only `tap::dsp` and the engine's include
directory on the path, no public header names `srt/`, a sibling's
`tap/sr/<engine>/` or `../`, and the public header count is pinned. Shared
code (design math, traits, kernels, quantization, instruments) lands in
DspTap first; this tree bumps the submodule pin.

## Provenance and license

MIT (`LICENSE`: Timothy Place and the SampleRateTap contributors). The tree
merged two repositories in 2026 with their histories intact: SampleRateTap
v0.1.0 became the `async` engine and RatioTap v0.3.0 the `bridge` engine
(`bridge/docs/HISTORY.md` maps RatioTap's commits and pull requests to their
imported SHAs). Bisect across that merge with `git bisect start
--first-parent`. The migration's gate runs are recorded in
`docs/MIGRATION_RUNS.md`.
