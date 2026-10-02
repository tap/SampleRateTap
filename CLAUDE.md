# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**SampleRateTap** — the `tap::sr` sample-rate family: one tree, three header-only C++20 engines
over one substrate. `async/` (`tap::sr::async`) is the asynchronous near-unity converter that
*absorbs the clock*; `bridge/` (`tap::sr::bridge`) is the synchronous 44.1 ↔ 48 kHz converter that
*converts the number*; `rational/` (`tap::sr::rational`) is the synchronous small-factor L/M
converter within a rate family (L, M ∈ {2^a·3^b}), chains of Nyquist stages, at M2 of its plan
(ratio types and the pinned stage designs; the stages follow). All build on DspTap (`submodules/dsptap`,
`tap::dsp`), pinned once at the root. Each engine has its own `README.md`, `PLAN.md` and `CLAUDE.md`;
read the engine's before touching its code.

**`PLAN.md` is the family plan**: the charters and the coverage rule (section 2), the settled
decisions D1–D16 (section 1), the layout (section 4), and the record of the 2026 migration that
made this tree (sections 5–6). Do not re-derive what it settles.

## The rules that hold the family together (load-bearing)

- **Never route by rate (D12).** The caller declares clock topology by choosing a type: `async`
  for independent clocks, `bridge` for the fixed 44.1/48 pair, `rational` for a small-factor L/M
  inside one family (`ratio<L, M>`, a compile-time type whose charter is a `static_assert`), and
  their composition for 44.1 ↔ 48 across independent clocks (`bridge/examples/bluetooth_bridge.cpp`)
  or across families (a chain through `bridge`, written by the caller). There is no
  `(in_hz, out_hz)` lookup anywhere, `async` is never chained behind one, and no rate is served by
  an engine whose charter does not name it (the 1000/1001 pull-down rates sit inside async's
  ±1000 ppm and must still never be served by it).
- **The dependency rule (PLAN.md 4.2).** Each engine depends on `tap::dsp` only. A sibling appears
  only under an engine's `tests/` and `examples/`; `capi/` is per engine; nothing shipped links two
  engines (the umbrella `tap::sr` is the caller's choice). The root `tests/` enforces it per engine
  — link interface, per-header isolation compile, include grep against both siblings, pinned header
  count — as ctest entries under the engine's label, so every job runs them. Engine-owned datapaths stay with their
  engine (D11): `bridge` never includes async's bank or blend.
- **Cross-validation stays independent (R4).** bridge's golden cross-validation against async gets
  its independence from the scipy leg and from the engines' structural difference, never from
  repository separation. A PR that changes cross-validation tolerances leaves
  `bridge/tests/reference/` untouched, keeps the scipy leg green, and does not also change async's
  datapath.
- **Substrate discipline.** Shared code (design math, sample traits, kernels, quantization,
  measurement instruments) lands in DspTap first; this tree bumps the submodule pin. Never fork
  substrate code into an engine.
- **One version (D13).** `TAP_SR_VERSION_*` is 0.4.0, defined token-identically in each umbrella
  header (checked by `tests/family/version_macros.cpp`) and returned bit-packed by each C ABI's
  `tap_sr_<engine>_version()` (pinned by `CApi.VersionIsBitPacked`). Tags are `vX.Y.Z`; bump all
  three headers and the root `project()` together (0.5.0 at `rational`'s M6).
- **Clean renames, no aliases (D7).** Retired options fail the configure
  (`cmake/retired_options.cmake`); retired override macros hit an `#error`. Do not add aliases.

## Build & test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTAP_SR_ASYNC_WERROR=ON -DTAP_SR_BRIDGE_WERROR=ON -DTAP_SR_RATIONAL_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure                # every engine + the family tests
ctest --test-dir build --output-on-failure -L '^async$'   # or -L '^bridge$' / -L '^rational$'
scripts/tidy.sh                                           # local mirror of the CI clang-tidy gate
```

Every configure builds every engine; select an engine at ctest time by label, never by a build
option (there are none per engine except `TAP_SR_<ENGINE>_WERROR`). Tests are GoogleTest with the
engine prefix and label (`async.` / `bridge.` / `rational.`, D16); the conventions to keep are contract tests
named for the promise they pin, typed batteries over `float`/`int16_t`/`int32_t`, exhaustive sweeps
over statistical sampling, and measured numbers stated with their provenance.

**Embedded legs.** CI runs every engine's battery on Cortex-M33 and M55 under `qemu-system-arm`
(`cmake/arm-cortex-*.cmake`, one-shot `bare_metal_main.cpp` per engine) and on Hexagon under
`qemu-hexagon`, and the instruction-count ratchet gates every workload of async and bridge
(`rational`'s arrive at its M6) two-sided at
±3 % (`scripts/icount.py --engine async|bridge --target m33|m55|hexagon`, baselines in
`<engine>/bench/baselines.json`). A change that moves a count beyond tolerance re-records the
baseline in the same PR; an improvement beyond tolerance fails too, by design. Guest markers
(`SRT_ICOUNT_DONE`, `RATIO_ICOUNT_DONE`) are part of the counted binaries and never change.

## Style

`STYLE.md` is the shared Tap house style; `.clang-format` and `.clang-tidy` enforce it and CI runs
both, plus a drift check that these files match the canonical taphouse copies — never edit them
locally. Run `pre-commit install` once per clone; on Claude Code web the SessionStart hook
(`.claude/hooks/session-start.sh`) does this and initializes the submodule. clang-tidy compiles
with a *clang* front end and clang's `-Wconversion` implies `-Wsign-conversion`, so treat the tidy
job and a local clang `-Werror` build as second compilers before pushing. Banners: every C/C++ and
Python source starts with the SPDX line and `Copyright 2026 Timothy Place and the SampleRateTap
contributors` (D14).

## History

The tree merged SampleRateTap v0.1.0 (now `async/`) and RatioTap v0.3.0 (now `bridge/`) in 2026
with both histories intact; `bridge/docs/HISTORY.md` maps RatioTap's commits and pull requests to
their imported SHAs, and `.git-blame-ignore-revs` lists the bulk reformats and the migration's
rename commits. **Bisect across the merge with `git bisect start --first-parent`.** The
migration's gates and their run record are in `PLAN.md` sections 5–6 and `docs/MIGRATION_RUNS.md`.

## Consumers & release flow

DspTap changes land in DspTap first, then this tree bumps `submodules/dsptap`. The book
(`book/`, https://tap.github.io/SampleRateTap/) is published from `main` by `book-pages`; the
notebooks are committed executed against the shipping C++ through each engine's C ABI and
binding (`async/notebooks/`, `bridge/notebooks/tap_sr_bridge_py.py`) — re-execute them when the
behaviour they measure changes.
