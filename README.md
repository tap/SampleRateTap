# SampleRateTap — the `tap::sr` sample-rate family

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
| **`rational`** | `tap::sr::rational` | Synchronous small-factor L/M *within* a rate family (L, M ∈ {2^a·3^b}: ↑2, ↓3, 2/3, …), as chains of Nyquist (L-th-band) stages, the ratio a compile-time type. **Converts the number.** M1–M4 landed (ratio types, pinned designs, the single stages for every ratio of the vocabulary, the named chains and the 182-row coverage matrix generated from the measured lengths); fixed point and the C ABI follow its plan. | [`rational/`](rational/README.md) |

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
| `TAP_SR_BUILD_CAPI` | OFF | the engines' C ABI shared libraries (`libtap_sr_async_capi`, `libtap_sr_bridge_capi`; `rational`'s at its M6) |
| `TAP_SR_BUILD_ICOUNT_BENCH` | OFF | the engines' instruction-count ratchet workloads (`rational`'s at its M6) |
| `TAP_SR_BUILD_BENCHMARKS`, `TAP_SR_BUILD_COMPARE_BENCH`, `TAP_SR_BUILD_COMPARE_SHIM` | OFF | the async engine's host-only benchmarks and comparison tooling |
| `TAP_SR_ASYNC_WERROR`, `TAP_SR_BRIDGE_WERROR`, `TAP_SR_RATIONAL_WERROR` | OFF | warnings as errors, per engine |

A retired pre-family option (`SRT_*`, `TAP_RATIO_*`) fails the configure
loudly (`cmake/retired_options.cmake`) rather than dropping a gate silently.

**Version.** One family version, `TAP_SR_VERSION_{MAJOR,MINOR,PATCH}`
(0.4.0), defined identically in each engine's umbrella header and returned
bit-packed — `(major << 16) | (minor << 8) | patch` — by each C ABI's
`tap_sr_async_version()` / `tap_sr_bridge_version()`. Tags are `vX.Y.Z`.

## Build and test

```sh
git clone --recurse-submodules https://github.com/tap/SampleRateTap
cmake -S SampleRateTap -B build -DCMAKE_BUILD_TYPE=Release \
      -DTAP_SR_ASYNC_WERROR=ON -DTAP_SR_BRIDGE_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure              # everything
ctest --test-dir build --output-on-failure -L '^async$' # one engine
ctest --test-dir build --output-on-failure -L '^bridge$'
scripts/tidy.sh                                         # the CI clang-tidy gate, locally
```

Tests are GoogleTest, named `async.<Suite>.<Test>` / `bridge.<Suite>.<Test>`
and labelled by engine. The family's own tests (`tests/`, under both labels)
enforce the dependency rule below and the version macros. CI runs the
battery on Linux (GCC, Clang, ASan/UBSan, TSan), macOS and Windows, on
Cortex-M33 and Cortex-M55 under `qemu-system-arm`, on Hexagon under
`qemu-hexagon`, and on arm64; the instruction-count ratchet
(`scripts/icount.py --engine async|bridge`) gates every workload of both
engines two-sided at ±3 % against the committed `<engine>/bench/baselines.json`.

## Layout

```
CMakeLists.txt          the family: options (D9), tap::dsp once, both engines, tap::sr, tests/
PLAN.md  CLAUDE.md      the family plan (charters, coverage rule, decisions) and working rules
LICENSE  STYLE.md       MIT; the shared Tap house style (.clang-format, .clang-tidy)
submodules/dsptap       the substrate, pinned once
cmake/  platform/       toolchains (Cortex-M33/M55, Hexagon), bare-metal startup, retired-option tripwire
scripts/                icount.py (the ratchet), tidy.sh, fetch_hexagon_toolchain.sh, doc updaters
tools/qemu_insn_plugin  the QEMU instruction-counting plugin
tests/                  the family's own tests: the dependency rule and the version macros
book/                   the async engine's book (published at https://tap.github.io/SampleRateTap/)
docs/                   Doxyfile; the migration's run record
async/                  include/tap/sr/async  tests  bench  examples  capi  notebooks  docs  README  PLAN
bridge/                 include/tap/sr/bridge tests  bench  examples  capi  notebooks  docs  README  PLAN
```

## The dependency rule

`async` and `bridge` each depend on `tap::dsp` only. An engine may reach its
sibling only under its own `tests/` and `examples/` — bridge's golden
cross-validation against async, and `bluetooth_bridge` — never from a
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
