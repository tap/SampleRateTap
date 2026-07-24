# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**RatioTap** — synchronous 44.1 ↔ 48 kHz sample rate conversion, as fast as possible. Header-only
C++20 under `include/tap/ratio/`, namespace `tap::ratio`, built on the shared Tap-family FIR
substrate from DspTap (`submodules/dsptap`, linked as `tap::dsp`).

**PLAN.md is the authoritative roadmap** — charter, settled architecture decisions, milestones
(M0–M7), and acceptance criteria. HANDOFF.md is the original design brief with a preamble listing
which of its decisions were superseded. Read PLAN.md before implementing anything; do not
re-derive decisions it has already settled (compile-time direction, profile vocabulary, the
three-leg test strategy, the pinned-eps cross-validation design).

Current state: **v0.1 (M6 complete), M7a measurement harness landed.** Design/schedule/tables
(M2), the streaming converter for float/Q15/Q31 with committed scipy reference vectors (M3/M4),
the golden cross-validation against SampleRateTap at pinned eps (M5, test-only submodule), the
bluetooth_bridge example + C ABI + executed demo notebook (M6), and the embedded CI matrix +
instruction-count ratchet (M7a: Cortex-M33/M55 + Hexagon under QEMU, eight fixed workloads gated
two-sided ±3% against `bench/baselines.json` — `scripts/icount.py`). Next: the M7 levers, one
measured PR each, superblock codegen first — see PLAN.md section 7. Any change that moves a
workload's count beyond ±3% must re-record baselines (`icount.py --update` per target) in the
same PR; an *improvement* beyond tolerance fails the gate too, by design.

## The charter constraints (load-bearing)

- **44.1↔48 only.** No other ratios on the public surface, ever; internal scaffolding may be
  general where it costs nothing, but optimization work is allowed to hard-commit to L ∈ {147, 160}.
- **Synchronous only.** Async-at-44.1↔48 is SampleRateTap's problem, reached by composition
  (the future `bluetooth_bridge` example). Never route by rate; the caller declares clock
  topology by choosing a type.
- **Speed-first.** Direction is compile-time. Every quality-vs-speed trade that is inaudible
  goes to speed in the default profile; the pristine profile exists behind the same design path.
- **Correctness before optimization.** Exhaustive phase coverage (all 147 and all 160 phases),
  an independent golden reference (scipy/soxr vectors), and the pinned-eps cross-validation
  against SampleRateTap gate every optimization that follows.

## Substrate discipline

Shared code (design math, sample traits, kernels, quantization, measurement instruments) lives in
DspTap and lands there FIRST; this repo bumps the submodule pin. Do not fork substrate code into
this repo — that divergence is exactly what DspTap exists to prevent. Q15 is the flagship
embedded profile (Bluetooth-adjacent M33/M55 deployments); float is the golden model against
scipy references.

## Style

`STYLE.md` is the shared Tap house style; `.clang-format` and `.clang-tidy` enforce it and CI runs
both (plus a drift check that the config files match the canonical taphouse copies — never edit
them locally). Run `pre-commit install` once per clone; on Claude Code web the checked-in
SessionStart hook (`.claude/hooks/session-start.sh`) does this and initializes the submodule at
session start. clang-tidy compiles with a *clang* front end — code that GCC accepts can still
fail there, and clang's `-Wconversion` implies `-Wsign-conversion` where GCC's does not, so treat
the tidy job and a local clang `-Werror` build as second compilers before pushing.

## Build & test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTAP_RATIO_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
scripts/tidy.sh          # local mirror of the CI clang-tidy gate
```

Tests are GoogleTest (FetchContent), and the conventions to preserve as the engine lands: contract
tests named for the promise they pin, typed batteries over `float`/`int16_t`/`int32_t`, exhaustive
phase sweeps rather than statistical sampling, and measured numbers stated in comments with their
provenance.
