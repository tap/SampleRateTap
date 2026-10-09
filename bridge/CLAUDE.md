# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
engine. The family's rules — the dependency rule, never routing by rate, the substrate
discipline, style, build and test — are in the root [`CLAUDE.md`](../CLAUDE.md); this file is
the engine's charter, which is what makes it different from its sibling.

## What this is

**`bridge`** (`tap::sr::bridge`, formerly RatioTap) — synchronous 44.1 ↔ 48 kHz sample rate
conversion, as fast as possible. Header-only C++20 under `include/tap/sr/bridge/`, built on the
shared FIR substrate from DspTap (`tap::dsp`). **`PLAN.md` is the authoritative roadmap** —
charter, settled architecture decisions, milestones (M0–M7, complete) and acceptance criteria;
HANDOFF.md is the original design brief with a preamble listing which of its decisions were
superseded. Read PLAN.md before implementing anything; do not re-derive decisions it has already
settled (compile-time direction, profile vocabulary, the three-leg test strategy, the pinned-eps
cross-validation design).

## The charter constraints (load-bearing)

- **44.1 ↔ 48 only.** No other ratios on the public surface, ever; internal scaffolding may be
  general where it costs nothing, but optimization work is allowed to hard-commit to
  L ∈ {147, 160}. The pair is also served at 2× and 4× (88.2 ↔ 96, 176.4 ↔ 192: the rate scale
  `K ≤ 2` of `ratio_traits<D, K>`, the family plan's follow-up 2.2) — the same machine with every
  Hz times 2^K, bit-identical by test, not a widening.
- **Synchronous only.** Async-at-44.1↔48 is the `async` engine's problem, reached by composition
  (`examples/bluetooth_bridge.cpp`). Never route by rate; the caller declares clock topology by
  choosing a type.
- **Speed-first.** Direction is compile-time. Every quality-vs-speed trade that is inaudible goes
  to speed in the default profile; the pristine profile exists behind the same design path.
- **Correctness before optimization.** Exhaustive phase coverage (all 147 and all 160 phases), an
  independent golden reference (the committed scipy vectors under `tests/reference/`), and the
  pinned-eps cross-validation against `async` gate every optimization. The cross-validation's
  independence comes from the scipy leg and the engines' structural difference (family rule R4):
  a PR that changes its tolerances leaves `tests/reference/` untouched, keeps the scipy leg
  green, and does not also change `async`'s datapath.
- **Outputs are bit-identical across codegen levers.** The ratchet (`scripts/icount.py --engine
  bridge`, `bench/baselines.json`, ten streaming workloads and two construct-only legs on
  M33/M55/Hexagon) gates every change two-sided at ±3 %; a change that moves a count re-records
  the baselines in the same PR, and an improvement beyond tolerance fails too, by design.
  Remaining levers that change the output contract stay deferred until a consumer pulls them
  (PLAN.md section 7).

## Profiles

Q15 is the flagship embedded profile (Bluetooth-adjacent M33/M55 deployments); float is the
golden model against the scipy references; Q31 tracks the float model at the format limit.
Tests are typed batteries over `float`/`int16_t`/`int32_t` with exhaustive phase sweeps rather
than statistical sampling, and measured numbers stated in comments with their provenance.

```sh
# from the repository root (this engine lives in bridge/; the root builds both engines)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTAP_SR_BRIDGE_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure -L '^bridge$'
```
