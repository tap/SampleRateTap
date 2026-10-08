# bridge — synchronous 44.1 ↔ 48 kHz (formerly RatioTap)

[![CI](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](../LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**Synchronous 44.1 ↔ 48 kHz sample rate conversion, as fast as possible.**

One rational ratio pair — 160/147 up, 147/160 down — one clock, and the
entire optimization budget spent on exactly that. Header-only C++20, built
on the Tap family's shared FIR substrate
([DspTap](https://github.com/tap/DspTap): Kaiser prototype design,
float/Q15/Q31 sample-format traits, measured dot-product kernels, row-sum
quantization, measurement instruments).

> **Status: v0.3 (profile-ladder re-pin).** The default `economy` profile
> moved to an 18 kHz passband at **58/38 taps — 26%/14% fewer MACs and
> −25%/−14% storage** than the previous default, with every 70 dB contract
> bound re-measured and held (the 18–19 kHz shelf moves into the
> transition band; the former economy design continues unchanged as
> `balanced` for content that needs that shelf flat). v0.1 shipped the
> converter for all three sample formats: float (the golden model, pinned
> against committed scipy reference vectors sample-for-sample), Q31
> (tracks float within −147 dB), and Q15 (format-limited: pair it with
> `economy`, which is both cheaper *and* quieter than `transparent` at 16
> bits), plus the golden cross-validation against the family's `async`
> engine (every phase, floor at the one deliberate design difference), the
> `bluetooth_bridge` example, the C ABI (`capi/`: float for the notebooks,
> and Q15 / Q31 through `tap_sr_bridge_create_format` with the `_q15` /
> `_q31` process and flush, each format pinned bit for bit against
> `basic_converter`; `notebooks/tap_sr_bridge_py.py` takes `fmt="q15"`),
> and the executed demo notebook.
> v0.2 was the measured optimization campaign — superblock walk, committed
> compile-time trip counts, symmetry-halved tables, each gated by the
> instruction-count ratchet, outputs bit-identical throughout: **Q15
> −59%/−60% and float −35%/−37% on Cortex-M55, Q31 −26%/−27% on
> Cortex-M33, Q15 −13%/−10% on Hexagon**. The remaining PLAN §7 levers
> (multistage, minimum-phase, IIR, FFT) change the output contract and
> stay deferred until a consumer needs them.
> [PLAN.md](PLAN.md) is the authoritative roadmap (charter, architecture
> decisions, milestones, acceptance criteria, per-lever measurements);
> [HANDOFF.md](HANDOFF.md) is the original design brief it grew from.

## Quick start

```cpp
#include <tap/sr/bridge/ratio.h>

tap::sr::bridge::converter_to_44k1 down(2);        // 48 -> 44.1, stereo, economy
// profiles: economy() (default, 18 kHz passband) | balanced() (19 kHz,
// the pre-v0.3 default) | transparent() (120 dB pristine tier) |
// super_economy() (16 kHz voice/comms tier — audible top-octave shelf)
std::vector<float> out(down.outputs_for(n_in) * 2);
std::size_t made = down.process(in, n_in, out.data());   // noexcept, alloc-free
// ... and at end of stream:
std::vector<float> tail(down.flush_output_frames() * 2);
down.flush(tail.data());
```

Direction is a compile-time type (`converter_to_48k` / `converter_to_44k1`,
plus `_q15` / `_q31` fixed-point variants), and so is the rate scale: the
pair at 2× and 4× is `converter_to_96k` / `converter_to_88k2` and
`converter_to_192k` / `converter_to_176k4` (with the same fixed-point
variants), the same machine with every Hz times 2^K and the same profiles,
whose edges are fractions of the rate. `pull(out, n, pop_fn)` is the
callback-driven shape, and `frames_needed(n)` is exact arithmetic. For
44.1↔48 across *independent clocks* (a Bluetooth chip on its own crystal),
compose with the family's `async` engine — `examples/bluetooth_bridge.cpp` is the
documented recipe: +200 ppm crystal, servo locked, 997 Hz recovered
exactly, 1.9 ms total latency.

## The boundaries are identity, not policy

- **No other ratios.** Not 2:1, not 96→44.1, not arbitrary L/M. The public
  surface is the 44.1↔48 pair — at its base rates and at 2× and 4×
  (`converter_to_96k` / `converter_to_88k2`, `converter_to_192k` /
  `converter_to_176k4`: the rate scale `K` of `ratio_traits`, the same
  machine with every Hz times 2^K, bit-identical by test) — which is what
  licenses the optimization work (straight-line superblock codegen, baked
  tables, multistage decomposition) to hard-commit to phase counts of
  exactly 147 and 160.
- **No asynchronous conversion.** If the two ends of your chain run on
  different crystals — *even at nominally 44.1-vs-48* — that is the
  family's [`async`](../async/README.md) engine's problem, reached by
  composition: `bridge` converts the *number*, `async` absorbs the *clock*. Which engine applies is a property of the
  clock topology, never inferred from a float ratio. The
  `bluetooth_bridge` example (milestone M6) documents the composition.
- **Speed-first.** Direction is a compile-time parameter; the default
  quality profile takes the speed side of every inaudible trade (all alias
  products confined above 20 kHz by arithmetic — see the plan's profile
  section), with a pristine 120 dB profile behind the same design path.

## Position in the Tap family

```
                    ┌────────────────────────────┐
                    │           DspTap           │  shared substrate (submodules/dsptap)
                    │  kaiser design · sample    │
                    │  traits (float/Q15/Q31) ·  │
                    │  FIR dot kernels · row-sum │
                    │  quantization · analysis   │
                    └──────┬──────────────┬──────┘
                           │              │
              ┌────────────┴───┐   ┌──────┴─────────┐
              │ tap::sr::async │   │ tap::sr::bridge│
              │ async, near-   │   │ sync, 44.1↔48, │
              │ unity, servo   │   │ speed-first    │
              └────────────┬───┘   └──────┬─────────┘
                           │              │
                           └── test-only ─┘  bridge's golden cross-validation
                               (bridge/tests/, bridge/examples/bluetooth_bridge)
```

## Build

```sh
git clone --recurse-submodules https://github.com/tap/SampleRateTap
cmake -S SampleRateTap -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure -L '^bridge$'
```

This engine lives in `bridge/` of the SampleRateTap family repository; the
root build configures both engines, and the `ratio` label selects this one's
tests. Consume with `add_subdirectory` (or FetchContent) and link
`tap::sr::bridge`; the DspTap submodule at the repository root rides along
automatically.

### Embedded targets and the instruction-count ratchet

The deployment cores are CI targets, not aspirations: every push runs the
emulation-sized test suite on **Cortex-M33** (QEMU mps2-an505 — Raspberry
Pi Pico 2 class), **Cortex-M55** (mps3-an547) and **Hexagon**
(qemu-hexagon, static musl), and gates ten fixed conversion workloads
(direction × float/Q15/Q31 at the economy profile, plus four profile
variants) against committed per-target instruction
counts (`bench/baselines.json`, two-sided ±3%), measured by the family's
shared harness (`scripts/icount.py --engine bridge`, `tools/qemu_insn_plugin/`)
from the repository root:
The counts are deterministic, so the M7 optimization campaign in
[PLAN.md](PLAN.md) lands one measured lever at a time:

```sh
cmake -B build-m55 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
      -DTAP_SR_BUILD_TESTS=OFF -DTAP_SR_BUILD_EXAMPLES=OFF \
      -DTAP_SR_BUILD_TESTS=OFF -DTAP_SR_BUILD_EXAMPLES=OFF \
      -DTAP_SR_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55 -j
python3 scripts/icount.py --engine bridge --target m55 --build-dir build-m55 \
      --plugin libinsncount.so
```

<!-- ICOUNT:BEGIN -->
Executed instructions per fixed workload (`bridge/bench/icount/`), measured under QEMU with a counting plugin — deterministic, and gated in CI at ±3% against `bridge/bench/baselines.json`:

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `down_float_eco` | 1,720,707,553 | 73,794,800 | 304,636,182 |
| `down_float_tr` | 5,473,297,976 | 214,977,684 | 944,365,072 |
| `down_q15_eco` | 173,755,176 | 22,404,812 | 45,676,947 |
| `down_q15_se` | 130,967,926 | 17,749,381 | 33,235,098 |
| `down_q31_eco` | 244,985,862 | 96,697,327 | 45,622,550 |
| `up_float_eco` | 1,231,730,349 | 55,451,902 | 220,214,287 |
| `up_float_tr` | 3,110,380,470 | 126,303,581 | 533,810,335 |
| `up_q15_eco` | 133,914,472 | 19,095,905 | 35,990,378 |
| `up_q15_se` | 103,390,933 | 17,493,186 | 27,424,077 |
| `up_q31_eco` | 184,954,171 | 72,647,042 | 36,007,820 |
<!-- ICOUNT:END -->

## License

MIT (see [LICENSE](../LICENSE)), consistent with the family. Style is the
shared [Tap House Rules](../STYLE.md), enforced by pre-commit clang-format,
the drift check, and clang-tidy in CI.
