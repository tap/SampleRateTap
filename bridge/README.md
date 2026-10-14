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

Construction rejects zero channels and a profile the designer refuses
(`std::invalid_argument`); `converter_to_44k1::validate(channels, profile)`
is the same verdict as a `noexcept` `const char*` reason (nullptr when it
constructs), the family's error model (root README, "Errors") — what a
build without exceptions calls before constructing.

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
exactly, 1.93 ms total latency.

Against the general-purpose resampler libraries at exactly this ratio pair
— libsamplerate, soxr, r8brain-free-src and SpeexDSP at the settings each
needs to reach `economy` and `transparent`, and at their best — quality,
latency, host throughput and embedded instruction counts are measured in
[docs/COMPARISON.md](docs/COMPARISON.md) (the notebook behind it:
[notebooks/bridge_comparison.ipynb](notebooks/bridge_comparison.ipynb); the
family index: [docs/COMPARISON.md](../docs/COMPARISON.md) at the root).

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

## Build

```sh
git clone --recurse-submodules https://github.com/tap/SampleRateTap
cmake -S SampleRateTap -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure -L '^bridge$'
```

This engine lives in `bridge/` of the SampleRateTap family repository; the
root build configures every engine, and the `bridge` label selects this one's
tests. Consume with `add_subdirectory` (or FetchContent) and link
`tap::sr::bridge`; the DspTap submodule at the repository root rides along
automatically.

### Embedded targets and the instruction-count ratchet

The deployment cores are CI targets, not aspirations: every push runs the
emulation-sized test suite on **Cortex-M33** (QEMU mps2-an505 — Raspberry
Pi Pico 2 class), **Cortex-M55** (mps3-an547) and **Hexagon**
(qemu-hexagon, static musl), and gates ten fixed conversion workloads
(direction × float/Q15/Q31 at the economy profile, plus four profile
variants) and the Q15 economy converter's construction alone in each
direction against committed per-target instruction
counts (`bench/baselines.json`, two-sided ±3%), measured by the family's
shared harness (`scripts/icount.py --engine bridge`, `tools/qemu_insn_plugin/`)
from the repository root:
The counts are deterministic, so the M7 optimization campaign in
[PLAN.md](PLAN.md) lands one measured lever at a time:

```sh
cmake -B build-m55 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
      -DTAP_SR_BUILD_TESTS=OFF -DTAP_SR_BUILD_EXAMPLES=OFF \
      -DTAP_SR_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55 -j
python3 scripts/icount.py --engine bridge --target m55 --build-dir build-m55 \
      --plugin libinsncount.so
```

<!-- ICOUNT:BEGIN -->
Executed instructions per fixed workload (`bridge/bench/icount/`), measured under QEMU with a counting plugin — deterministic, and gated in CI at ±3% against `bridge/bench/baselines.json`. A `callmax_` row is the worst single call instead of a total: the most instructions one call executes over that stream (`bench/icount/callmax_main.cpp`):

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `callmax_down_float` | 544,727 | 23,948 | 108,885 |
| `callmax_down_q15` | 12,984 | 6,616 | 7,629 |
| `callmax_up_float` | 416,124 | 19,183 | 81,626 |
| `callmax_up_q15` | 11,011 | 6,037 | 6,209 |
| `construct_down_q15_eco` | 136,279,322 | 4,260,534 | 24,941,141 |
| `construct_up_q15_eco` | 104,011,630 | 3,470,962 | 20,108,264 |
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

On the host, the float converter in 128-frame blocks at 2 and 12 channels (`bridge/bench/icount/host_main.cpp`): instructions executed by the streaming loop alone, counted under callgrind on x86-64 with GCC 13 (`scripts/icount.py --target host`), gated the same way:

| Workload | x86-64 |
|---|---:|
| `down_float_12ch` | 593,181,679 |
| `down_float_2ch` | 99,664,796 |
| `up_float_12ch` | 470,621,528 |
| `up_float_2ch` | 79,352,295 |
<!-- ICOUNT:END -->

## Footprint

One converter's heap, after construction, is a formula in its geometry (L phases,
R = ceil(L / 2) stored rows, since the mirror half is not held; T taps from `taps()`;
`c` the coefficient size, `s` the sample size, `ch` channels):

```text
held = R T c                  half phase table
     + (T + 64) ch s          history, 64 frames of append slack
     + 16 ch s                pull()'s pop scratch
     + ch sizeof(std::vector)
peak = max(held, R T c + 8 L T + 8 T + q 8 T)
       the design: the table, the double prototype, one double row,
       and the fixed-point quantizer's remainder (q = 1 for Q15 and Q31)
```

| profile | direction | T | f32 held | f32 peak | Q15 held | Q15 peak | Q31 held | Q31 peak |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `super_economy` | 44k1 → 48k | 28 | 9,872 | 45,024 | 4,960 | 40,768 | 9,872 | 45,248 |
| `super_economy` | 48k → 44k1 | 40 | 12,848 | 59,200 | 6,448 | 53,600 | 12,848 | 59,520 |
| `economy` | 44k1 → 48k | 38 | 13,152 | 61,104 | 6,600 | 55,328 | 13,152 | 61,408 |
| `economy` | 48k → 44k1 | 58 | 18,320 | 85,840 | 9,184 | 77,720 | 18,320 | 86,304 |
| `balanced` | 44k1 → 48k | 44 | 15,120 | 70,752 | 7,584 | 64,064 | 15,120 | 71,104 |
| `balanced` | 48k → 44k1 | 78 | 24,400 | 115,440 | 12,224 | 104,520 | 24,400 | 116,064 |
| `transparent` | 44k1 → 48k | 96 | 32,176 | 154,368 | 16,112 | 139,776 | 32,176 | 155,136 |
| `transparent` | 48k → 44k1 | 184 | 56,624 | 272,320 | 28,336 | 246,560 | 56,624 | 273,792 |

The doubled and quadrupled rates (K = 1, 2) are the same machine and hold the same
bytes. The figures are bytes on a 64-bit host, stereo; a 32-bit target holds
`sizeof(std::vector)` (12 instead of 24) less per history vector and nothing else
changes. `tests/test_footprint.cpp` pins each formula against DspTap's heap meter
(`tap/dsp/analysis/heap_meter.h`) exactly, on the hosted legs (not under ASan or TSan, whose runtime owns the allocator) and on Hexagon
(32-bit), and checks that processing allocates nothing. On Windows x64 the allocator asks for more than the formula: MSVC's `std::allocator` pads every
request of 4096 bytes or more by 39 bytes to align it to 32 bytes, and the test counts each
allocation through DspTap's model of that (`heap_meter::allocator_request`).


### Code size and stack

The code a deployment links and the stack each call needs are gated beside the
instruction counts, by `scripts/ceilings.py` against `bench/ceilings.json`, on one
probe per format (`bench/icount/probe_main.cpp`): the duplex pair at the default
profile, stereo, a 44.1 → 48 kHz converter and a 48 → 44.1 kHz one each streaming
32-frame blocks through `process()` (the `up` and `down` calls), built Release in the
ratchet's tree.

- **Engine `.text` and `.rodata`** are the probe's own translation unit, from its
  link map: the two converters as instantiated.
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
| m33 | f32 | 31,080 | 32,064 | 1,544 | 1,600 | 65,097 | 67,072 |
| m33 | q15 | 32,688 | 33,728 | 1,544 | 1,600 | 66,897 | 68,928 |
| m33 | q31 | 31,876 | 32,896 | 1,544 | 1,600 | 66,089 | 68,096 |
| m55 | f32 | 28,136 | 28,992 | 1,544 | 1,600 | 61,689 | 63,552 |
| m55 | q15 | 36,840 | 37,952 | 1,544 | 1,600 | 70,705 | 72,832 |
| m55 | q31 | 31,520 | 32,512 | 1,544 | 1,600 | 65,377 | 67,392 |
| hexagon | f32 | 93,212 | 96,064 | 1,228 | 1,280 | 369,520 | 380,608 |
| hexagon | q15 | 97,712 | 100,672 | 1,228 | 1,280 | 374,608 | 385,856 |
| hexagon | q31 | 97,284 | 100,224 | 1,228 | 1,280 | 374,192 | 385,472 |

| M33 call | format | static worst path (bytes) | ceiling | measured high-water (bytes) | ceiling |
|---|---|--:|--:|--:|--:|
| `up` | f32 | 320 | 336 | 216 | 224 |
| `up` | q15 | 288 | 304 | 268 | 280 |
| `up` | q31 | 272 | 288 | 172 | 184 |
| `down` | f32 | 328 | 344 | 216 | 224 |
| `down` | q15 | 288 | 304 | 268 | 280 |
| `down` | q31 | 272 | 288 | 172 | 184 |
<!-- ceilings:end -->

## Non-finite input and denormals

A NaN or ±Inf input sample corrupts only the outputs whose window covers it, on its own channel:
a burst of n frames reaches at most ⌈(n + taps)·L/M⌉ + 1 outputs, and every output after them is
bit-identical to a converter's that never saw the burst, because the converter is a pure FIR with
nothing computed from a sample's value (`NonFinite.*`, `tests/test_non_finite.cpp`: 67 outputs
up and 75 down for a 24-frame burst at `economy`). The float datapath accumulates in double, where every float denormal is a normal number, so no
denormal arises inside a dot product; the output, rounded back to float, may carry denormals onward
(a decaying tail, a denormal input passed through), and the converter does not flush them. Q15 and Q31 have neither.

## License

MIT (see [LICENSE](../LICENSE)), consistent with the family. Style is the
shared [Tap House Rules](../STYLE.md), enforced by pre-commit clang-format,
the drift check, and clang-tidy in CI.
