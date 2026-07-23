# RatioTap

[![CI](https://github.com/tap/RatioTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/RatioTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**Synchronous 44.1 ↔ 48 kHz sample rate conversion, as fast as possible.**

One rational ratio pair — 160/147 up, 147/160 down — one clock, and the
entire optimization budget spent on exactly that. Header-only C++20, built
on the Tap family's shared FIR substrate
([DspTap](https://github.com/tap/DspTap): Kaiser prototype design,
float/Q15/Q31 sample-format traits, measured dot-product kernels, row-sum
quantization, measurement instruments).

> **Status: milestone M3.** The float converter is in — both directions,
> push (`process`) and pull (`pull` + exact `frames_needed`) call shapes,
> pinned against committed scipy reference vectors sample-for-sample from
> the first output. Q15/Q31 aliases land with their parity battery in M4.
> [PLAN.md](PLAN.md) is the authoritative roadmap (charter, architecture
> decisions, milestones, acceptance criteria);
> [HANDOFF.md](HANDOFF.md) is the original design brief it grew from.

## The boundaries are identity, not policy

- **No other ratios.** Not 2:1, not 96→44.1, not arbitrary L/M. The public
  surface is 44.1↔48 only, which is what licenses the optimization work
  (straight-line superblock codegen, baked tables, multistage
  decomposition) to hard-commit to phase counts of exactly 147 and 160.
- **No asynchronous conversion.** If the two ends of your chain run on
  different crystals — *even at nominally 44.1-vs-48* — that is the
  [SampleRateTap](https://github.com/tap/SampleRateTap) near-unity ASRC's
  problem, reached by composition: RatioTap converts the *number*, the
  ASRC absorbs the *clock*. Which engine applies is a property of the
  clock topology, never inferred from a float ratio. The
  `bluetooth_bridge` example (milestone M6) documents the composition.
- **Speed-first.** Direction is a compile-time parameter; the default
  quality profile takes the speed side of every inaudible trade (all alias
  products confined above 20 kHz by arithmetic — see the plan's profile
  section), with a pristine 120 dB profile behind the same design path.

## Position in the Tap family

```
                    ┌────────────────────────────┐
                    │           DspTap           │  shared substrate (submodule)
                    │  kaiser design · sample    │
                    │  traits (float/Q15/Q31) ·  │
                    │  FIR dot kernels · row-sum │
                    │  quantization · analysis   │
                    └──────┬──────────────┬──────┘
                           │              │
              ┌────────────┴───┐   ┌──────┴─────────┐
              │ SampleRateTap  │   │    RatioTap    │
              │ async, near-   │   │ sync, 44.1↔48, │
              │ unity, servo   │   │ speed-first    │
              └────────────┬───┘   └──────┬─────────┘
                           │              │
                           └──── test-only│dependency:
                                golden cross-validation
```

## Build

```sh
git clone --recurse-submodules https://github.com/tap/RatioTap
cmake -S RatioTap -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Consume with `add_subdirectory` (or FetchContent) and link `tap::ratio`;
the DspTap submodule rides along automatically.

## License

MIT (see [LICENSE](LICENSE)), consistent with the family. Style is the
shared [Tap House Rules](STYLE.md), enforced by pre-commit clang-format,
the drift check, and clang-tidy in CI.
