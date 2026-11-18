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

tap::sr::bridge::converter_to_44k1<2> down;        // 48 -> 44.1, stereo, economy
// profiles: economy() (default, 18 kHz passband) | balanced() (19 kHz,
// the pre-v0.3 default) | transparent() (120 dB pristine tier) |
// super_economy() (16 kHz voice/comms tier — audible top-octave shelf)
std::vector<float> out(down.outputs_for(n_in) * 2);
std::size_t made = down.process(in, n_in, out.data());   // noexcept, alloc-free
// ... and at end of stream:
std::vector<float> tail(down.flush_output_frames() * 2);
down.flush(tail.data());
```

The channel count is the type's (`converter_to_44k1<2>`: a compile-time
constant of at least 1, with no default; zero fails to compile, the family's
A13). Construction rejects a profile the designer refuses
(`std::invalid_argument`); `converter_to_44k1<2>::validate(profile)` is the
same verdict as a `noexcept` `const char*` reason (nullptr when it
constructs), the family's error model (root README, "Errors") — what a
build without exceptions calls before constructing.

Direction is a compile-time type (`converter_to_48k<Ch>` / `converter_to_44k1<Ch>`,
plus `_q15` / `_q31` fixed-point variants; `basic_converter<S, Ch, D, K>`
underneath), and so is the rate scale: the
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

The channel count moved into the type at A13 (`docs/CHANNELS_PLAN.md`), and its gain is
measured in instructions only: no cycle counts on hardware yet (K11, deferred to 0.7.0).
Against the baselines before it, every row held gate 0's rule, the streaming sums fell on
every target, and the engine `.text` fell 49 … 60 %. Five Cortex-M55 rows are gated at a
midpoint: `down_q15_eco`, `up_q15_eco`, `up_q15_se`, `callmax_down_q15` and
`callmax_up_q15` count about 2 % more without exceptions than with them, the code the same
until GCC's register allocation (`PLAN.md`, M7g), so each baseline is the midpoint of the
two builds' counts and both sit within ±1.2 % of it. Re-record those rows at the midpoint,
never with `--update` from one build.

<!-- ICOUNT:BEGIN -->
Executed instructions per fixed workload (`bridge/bench/icount/`), measured under QEMU with a counting plugin — deterministic, and gated in CI at ±3% against `bridge/bench/baselines.json`. A `callmax_` row is the worst single call instead of a total: the most instructions one call executes over that stream (`bench/icount/callmax_main.cpp`):

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `callmax_down_float` | 544,858 | 23,687 | 108,834 |
| `callmax_down_q15` | 12,904 | 6,555 | 7,594 |
| `callmax_up_float` | 416,269 | 18,827 | 81,575 |
| `callmax_up_q15` | 10,934 | 5,862 | 6,169 |
| `construct_down_q15_eco` | 136,278,807 | 4,259,966 | 24,938,675 |
| `construct_up_q15_eco` | 104,011,144 | 3,470,314 | 20,103,575 |
| `down_float_eco` | 1,720,700,517 | 72,960,268 | 304,480,111 |
| `down_float_tr` | 5,473,027,607 | 213,941,893 | 944,034,530 |
| `down_q15_eco` | 173,490,049 | 22,199,151 | 45,571,406 |
| `down_q15_se` | 130,702,831 | 16,706,945 | 33,123,379 |
| `down_q31_eco` | 244,091,594 | 96,803,300 | 45,467,724 |
| `up_float_eco` | 1,231,719,777 | 54,398,609 | 220,063,952 |
| `up_float_tr` | 3,110,077,618 | 125,024,952 | 533,608,001 |
| `up_q15_eco` | 133,676,365 | 18,589,538 | 35,881,082 |
| `up_q15_se` | 102,394,732 | 16,993,343 | 27,248,312 |
| `up_q31_eco` | 184,167,773 | 72,774,401 | 35,849,684 |

On the host, the float converter in 128-frame blocks at 2 and 12 channels (`bridge/bench/icount/host_main.cpp`): instructions executed by the streaming loop alone, counted under callgrind on x86-64 with GCC 13 (`scripts/icount.py --target host`), gated the same way:

| Workload | x86-64 |
|---|---:|
| `down_float_12ch` | 586,852,336 |
| `down_float_2ch` | 99,767,151 |
| `up_float_12ch` | 464,083,929 |
| `up_float_2ch` | 79,463,088 |
<!-- ICOUNT:END -->

## Footprint

One converter's heap, after construction, is a formula in its geometry (L phases,
R = ceil(L / 2) stored rows, since the mirror half is not held; T taps from `taps()`;
`c` the coefficient size, `s` the sample size, `ch` channels):

```text
held = R T c                  half phase table
     + (T + 64) ch s          history, 64 frames of append slack
     + 16 ch s                pull()'s pop scratch
peak = max(held, R T c + 8 L T + 8 T + q 8 T)
       the design: the table, the double prototype, one double row,
       and the fixed-point quantizer's remainder (q = 1 for Q15 and Q31)
```

| profile | direction | T | f32 held | f32 peak | Q15 held | Q15 peak | Q31 held | Q31 peak |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `super_economy` | 44k1 → 48k | 28 | 9,824 | 45,024 | 4,912 | 40,768 | 9,824 | 45,248 |
| `super_economy` | 48k → 44k1 | 40 | 12,800 | 59,200 | 6,400 | 53,600 | 12,800 | 59,520 |
| `economy` | 44k1 → 48k | 38 | 13,104 | 61,104 | 6,552 | 55,328 | 13,104 | 61,408 |
| `economy` | 48k → 44k1 | 58 | 18,272 | 85,840 | 9,136 | 77,720 | 18,272 | 86,304 |
| `balanced` | 44k1 → 48k | 44 | 15,072 | 70,752 | 7,536 | 64,064 | 15,072 | 71,104 |
| `balanced` | 48k → 44k1 | 78 | 24,352 | 115,440 | 12,176 | 104,520 | 24,352 | 116,064 |
| `transparent` | 44k1 → 48k | 96 | 32,128 | 154,368 | 16,064 | 139,776 | 32,128 | 155,136 |
| `transparent` | 48k → 44k1 | 184 | 56,576 | 272,320 | 28,288 | 246,560 | 56,576 | 273,792 |

The doubled and quadrupled rates (K = 1, 2) are the same machine and hold the same
bytes. The figures are bytes, stereo, and the formula has no pointer-sized term:
since A13 the histories are a `std::array` of `ch` vectors in the object (K6), where
before them an outer vector held them (`ch sizeof(std::vector)` more), so a 32-bit
target holds the same. `tests/test_footprint.cpp` pins each formula against DspTap's heap meter
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
| m33 | f32 | 15,164 | 15,680 | 1,544 | 1,600 | 49,185 | 50,688 |
| m33 | q15 | 16,620 | 17,152 | 1,544 | 1,600 | 50,825 | 52,352 |
| m33 | q31 | 16,264 | 16,768 | 1,544 | 1,600 | 50,473 | 52,032 |
| m55 | f32 | 12,776 | 13,184 | 1,544 | 1,600 | 46,345 | 47,744 |
| m55 | q15 | 17,876 | 18,432 | 1,544 | 1,600 | 51,745 | 53,312 |
| m55 | q31 | 15,600 | 16,128 | 1,544 | 1,600 | 49,465 | 51,008 |
| hexagon | f32 | 38,088 | 39,232 | 1,228 | 1,280 | 314,240 | 323,712 |
| hexagon | q15 | 40,052 | 41,280 | 1,228 | 1,280 | 316,832 | 326,400 |
| hexagon | q31 | 39,440 | 40,640 | 1,228 | 1,280 | 316,128 | 325,632 |

| M33 call | format | static worst path (bytes) | ceiling | measured high-water (bytes) | ceiling |
|---|---|--:|--:|--:|--:|
| `up` | f32 | 192 | 200 | 192 | 200 |
| `up` | q15 | 176 | 184 | 156 | 168 |
| `up` | q31 | 168 | 176 | 164 | 176 |
| `down` | f32 | 192 | 200 | 192 | 200 |
| `down` | q15 | 176 | 184 | 156 | 168 |
| `down` | q31 | 168 | 176 | 156 | 168 |
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
