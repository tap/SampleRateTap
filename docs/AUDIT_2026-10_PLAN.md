# Audit response plan (v0.2)

Status: **decisions taken 2026-10-09** (section 6 records them); execution in progress; ✓ in the disposition table marks a closed row (S0 to S3 landed on the branch). Answers [`AUDIT_2026-10.md`](AUDIT_2026-10.md) (35 findings:
4 major, 14 minor, 1 disputed, 16 nits) and the cohesion assessment that followed it: the
family reads as one mind at the level of the plans and one vocabulary at the level of
`bridge` and `rational`, while `async`, the oldest stratum, still speaks the dialect it was
written in, and the prose at the seams between rounds of work lags the code. The plan is
six pull requests in a fixed order, each with its gate, and twelve decisions proposed up
front so that the work is mechanical once they are taken. Nothing here changes a datapath's
output: every engine's output hashes and the three baselines stay as they are, and the
audit's bit-identity harnesses are the gate that proves it.

The house rules apply unchanged: substrate changes land in DspTap first (root `CLAUDE.md`),
renames are clean with no aliases (D7), one version bumped in three headers and the root
`project()` together (D13), the ratchet stays within ±3 % or is re-recorded with a reason.

## 1. Decisions proposed

Each decision states the recommendation, the alternative, and what hangs on it. A decision
marked **maintainer** has a cost outside this tree and is not taken here.

- **A1 — Substrate first.** F01, F03 and F21 are DspTap defects (`chain.h`,
  `sample_traits.h`). They land there as one PR with their tests, and this tree's first PR
  is the pin bump plus the prose that cites them. Alternative: none; the substrate
  discipline is settled.
- **A2 — F01's fix shape.** `tap::dsp::chain` exposes its stages read-only (`stage<I>()`
  returns `const auto&`), its constructor calls `reset()` on every stage it takes by value,
  and the scratch-bound comment is corrected to say the bound holds for process- and
  flush-reached states. A DspTap test pins `outputs_for(n) <= n*L/M + 2` from every
  process/flush-reached state and that a pulled-then-chained stage is reset. Alternative:
  widen `max_outputs_for` to `n*L/M + L + 1` and keep the mutable accessor; it costs scratch
  on every chain to serve a use nobody has, so the recommendation is the narrower surface.
  `basic_stage`'s contract block gains the sentence the bound relies on.
- **A3 — F02's fix shape.** `schedule_entry::advance` widens to `std::uint32_t` (one entry
  per output; no hot-path cost) and `test_stage.cpp` instantiates `ratio<3, 1024>` and
  `ratio<2, 729>` through the chunking-invariance and accounting tests so the whole charter
  set is served, not just the vocabulary. Alternative: `static_assert(M / L < 256)` next to
  the charter asserts, which documents a limit instead of removing one. Recommendation:
  widen; a charter that compiles should run.
- **A4 — One meaning per profile name across the family.** New family rule, root
  `CLAUDE.md`: a profile name means one thing in every engine, every engine's ladder is
  stated in one table in the root README with its numbers, and the word is `profile`
  everywhere (`preset` retires). `async` keeps its own ladder, because its numbers are a
  different kind (image rejection through a 120 dB-class interpolated bank, not a Nyquist
  stopband), so its tiers are **not** renamed to `bridge`'s four words: that would state
  `economy` at 70 dB for an engine whose cheapest tier is 96 dB-class. What changes is the
  collision: `async`'s `economy()` (program-weighted, two-thirds compute) becomes
  `program()`, and `fast()` / `balanced()` / `transparent()` stay. Alternative: rename
  `async`'s tiers to `super_economy` / `economy` / `balanced` / `transparent` by rank; the
  words would then mean different numbers per engine, which the rule forbids. **maintainer**
  on the new name (`program` is the recommendation; `weighted` the runner-up).
- **A5 — The async C ABI takes the siblings' shape.** `tap_sr_async_create_format(double
  sample_rate_hz, size_t target_latency_frames, int profile, int format, unsigned channels)`
  and `tap_sr_async_create(double, size_t, int profile, unsigned channels)`: profile and
  format before channels, `unsigned` channels, as `bridge` and `rational` have. Out-of-range
  profile returns NULL (F09). This breaks every existing C caller of `async`, which is why
  it rides the one breaking PR (S3) under the 0.6.0 bump. Alternative: keep the signature,
  only add the NULL on a bad profile. **maintainer**: does anything outside this tree call
  `tap_sr_async_create` today (TapTools, TapTools-Max, firmware)? If yes, the alternative.
- **A6 — Windows export.** Each C ABI header gains `TAP_SR_<ENGINE>_API`
  (`__declspec(dllexport)` under `_WIN32` while building, `visibility("default")` elsewhere,
  the shape DspTap's `dsptap_capi.h` already uses), and the MSVC leg turns the C ABI ON so
  the `CApi.*` suites gate Windows too. Alternative: state in the three headers, READMEs and
  bindings that the C ABI is built for ELF/Mach-O hosts and drop the `.dll` branch. The
  recommendation is the macro: it is twenty lines and the leg exists.
- **A7 — The symbol count becomes the test the book says it is.** One ctest entry per
  engine on ELF/Mach-O hosts (`tests/family/exported_symbols.cmake`): `nm -D --defined-only`
  on the built library, the `tap_sr_<engine>_` set compared to a committed list (14, 17,
  23 names), anything else in the table a failure. The book's sentence stays true.
- **A8 — A vocabulary gate.** `style.yml` gains a grep job over `*.md`, `*.h`, `*.cpp`,
  `*.py`, `*.yml` outside `submodules/` and `*/docs/HISTORY.md` for the retired words:
  `RatioTap`, `asrc` (outside the notebook filenames the book links), `preset`, the `ratio`
  label, "both engines", "two engines", "Status: M" lines that do not match the engine's
  plan. The list lives in one file (`scripts/retired_words.txt`) with the reason per line.
  It lands in the docs PR (S5), red until that sweep is green.
- **A9 — Version 0.6.0** for S3 (C++ and C names change). Bumped in the three umbrella
  headers and the root `project()` in that PR; S0 to S2 ship under 0.5.0.
- **A10 — Pin the tie direction at the stage.** A constructed-tie test per fixed-point
  format in `rational/tests/test_fixed_point.cpp` (an impulse amplitude chosen so one
  product's discarded bits are exactly one half for an odd stored coefficient, expected
  value computed test-side by round-half-up-and-saturate), for a unity-gain stage, a Q15
  decimator (the divided path) and a Q31 stage. It kills mutants M3, M11 and M12. `bridge`
  gets the same test on its one rounding point in the same PR, since its battery was not
  mutation-tested.
- **A11 — Notebook filenames stay.** `async/notebooks/asrc_*.ipynb` are executed artefacts
  the book links by name; renaming them re-executes nothing and breaks links. The tests
  (`test_asrc_*.cpp`) rename to `test_<topic>.cpp`; the notebook prose drops the old names.
- **A12 — The audit record lives in `docs/`.** `AUDIT_2026-10.md` and this plan stay as
  committed files; the disposition table below is updated in each PR that closes a row, the
  way PLAN.md's appendices recorded the migration audits.

## 2. The steps

Six pull requests in this order. Each names the findings it closes, its gate, and its size.
Nothing in S0 to S2 is breaking; S3 is the one breaking PR; S4 and S5 are prose and gates.

### S0 — DspTap: `chain.h` and the pin bump (A1, A2)

DspTap PR: `chain.h` stage accessor const-only, constructor resets its stages, the
scratch-bound comment and the contract block corrected (`outputs_for(frames_needed(k)) >= k`
and `outputs_for(frames_needed(k) - 1) < k`, no chain `pull`); `sample_traits.h:222-226`
overflow count corrected (F21); `tests/test_chain.cpp` pins the bound from every
process/flush-reached state and the reset-on-construction. Then this tree: pin bump,
`rational/PLAN.md` 2.5 reworded to the same inequality (F03), `stage.h`'s contract block
gains the bound sentence (F01), `chain.h`'s named chains unchanged.

- Closes: F01, F03, F21.
- Gate: DspTap battery green on every leg; here, every engine's label green, output hashes
  unchanged, ratchet within tolerance (the chain constructor's `reset()` is construction,
  measured as its own scenario). The audit's `chain_pull_asan.cpp` harness, moved into
  DspTap's tests, fails before and passes after.
- Size: small (two headers, two tests, one pin).

### S1 — rational: correctness and the test gaps (A3, A10)

- `stage.h`: `advance` widened; `test_stage.cpp` instantiates `ratio<3, 1024>` and
  `ratio<2, 729>` in the chunking and accounting suites (F02).
- Constructed-tie tests, three formats, stage level (F13, A10); the same for `bridge`.
- `Stage.ResetReproducesBitExactly` and `Chain.ResetReproducesBitExactly` gain a decimator
  (`down_3`) and a decimator-bearing chain (`down_3_down_8_down_2`) reset mid-stream at a
  nonzero decimation phase (F12, the disputed one: the host battery catches it today, the
  bare-metal legs would not).
- `compile_fail/`: a fourth case isolating `L != M` with its own message, and the clang
  message check made per-case (F07).
- `design.h`: the search-bound comment corrected (128 is 2.8× the longest pin) and the
  exception text names the bound (F08); `profile::relaxed()` validates its result and
  `stage_taps_per_phase()` throws like `design_stage` instead of returning 0 (F22).
- `test_fixed_point.cpp`: the relaxed designs' Q15 stopbands measured and stated per
  relaxation row, the 147/160 row's −67.6 dB recorded in PLAN.md section 6 as the honest
  number for that divisor (F05); stale pre-lever numbers in comments replaced (F20).
- `bare_metal_main.cpp`: the selected-test floor raised to within a few of the real count
  (88 today) and the comment rewritten; the floor's rationale (a filter typo must not pass
  green) kept (F11).
- `stage.h` / `schedule.h` (bridge): `outputs_for` / `frames_needed` document the wrap
  above `SIZE_MAX / L` and the C ABI headers say the `uint64` counts share it (F35).
- `make_reference_vectors.py` emits `// clang-format off` / `on` guards so the committed
  header regenerates byte-identical (F23); regenerate, diff must be the guard lines only.
- `rational.h`'s status line and `rational/PLAN.md` section 4's promised files: the
  cross-validation test and `examples/multirate_chain.cpp` are either written (the example
  is a dozen lines: `chain<up_2, ratio_4_3>` on a tone, printed latency) or struck from the
  layout with the reason (F18, F34). Recommendation: write the example, strike the
  cross-validation row (section 5 already says why it is not repeated here).
- Closes: F02, F05, F07, F08, F11, F12, F13, F18, F20, F22, F23, F28, F34, F35.
- Gate: rational label green on the three hosts and the embedded legs; the seventeen
  mutants re-run with the audit's harness, zero survivors; reference vectors regenerate
  byte-identical; output hashes unchanged; ratchet within tolerance (the widened field is
  not on the hot path; if M33 moves, re-record with the reason).
- Size: medium (six headers touched, five test files, one generator).

### S2 — The three C ABIs: the symbol test and the export macro (A6, A7)

- `tests/family/exported_symbols.cmake` and three committed symbol lists; a ctest entry per
  engine under its label on Linux and macOS (F04). The book's sentence then describes a
  test that exists.
- `TAP_SR_<ENGINE>_API` in the three headers; the MSVC leg builds the C ABI and runs
  `CApi.*` (F10). If the MSVC leg shows a real blocker (a `size_t` / `unsigned` mismatch,
  a `dllimport` consumer issue), the alternative of A6 is written into the headers instead,
  in this PR, so the claim and the leg agree either way.
- `bridge` and `rational` C headers state their thread contract in `async`'s words (one
  thread per handle, or the exact affinity) (F24).
- `async` C ABI: NULL on an out-of-range profile and the test row (F09), without changing
  the signature yet (that is S3, A5).
- Closes: F04, F09, F10, F24, F33 (the async PLAN count sentence).
- Gate: every `CApi.*` suite green on four hosts including Windows; the symbol test green
  and shown red once by deliberately dropping the visibility preset in a scratch build.
- Size: small to medium (CMake and three headers; the MSVC leg is the unknown).

### S3 — The async vocabulary pass, 0.6.0 (A4, A5, A9, A11)

The one breaking PR. `async` is brought to the family's vocabulary without touching its
datapath or its ladder's numbers.

- C++: `preset` becomes `profile` in every identifier and comment; `filter_spec::economy()`
  becomes `filter_spec::program()`; the retired names are gone, not aliased (D7).
- C ABI: A5's signature if accepted, else only the parameter name; the three async notebooks
  and `figure_digest.py` updated and re-executed only if a measured number moves (none
  should: the datapath is untouched).
- Tests: `test_asrc_*.cpp` become `test_lock.cpp`, `test_program.cpp`, `test_quality.cpp`,
  `test_quality_16k.cpp`; the ctest prefix is unchanged (`async.`).
- `async.h`'s umbrella comment rewritten as the third of three engines in `rational.h`'s
  voice (charter in one paragraph, the boundaries as identity, the composition with
  `bridge`); `sample_traits.h`'s overview drops "RatioTap" and "ASRC-specific" for the family
  names.
- `async/README.md` restructured to the sibling shape: Quick start; "The boundaries are
  identity, not policy" (its charter sentence, the composition rule); Position in the Tap
  family; Build; the measured sections it alone has (latency, performance, platform
  support, sample types) kept under those headings; version 0.6.0 (F15). `rational/README.md`
  gains the "Position in the Tap family" section the other two have.
- `bluetooth_bridge.cpp:102` and the book's Part 5 lose "RatioTap"; the README latency
  figure stated once at 1.93 ms (F29).
- Version 0.6.0 in three headers and `project()`; `version_macros.cpp` re-pinned;
  `CApi.VersionIsBitPacked` expects 0x600.
- Closes: F15, F29, the cohesion items (vocabulary, umbrella header, README shape, test
  names, residue in async).
- Gate: every label green on every leg; output hashes of all three engines unchanged;
  ratchet unchanged (names only); the A/B stream harness from the audit
  (`ab_async.cpp`, `ab_bridge.cpp`) md5-identical before and after; the book builds; the
  retired names return zero hits outside `HISTORY.md` and the appendices.
- Size: medium, mostly mechanical; the README rewrite is the prose.

### S4 — The docs sweep

After S3 so the prose describes the final state. No code.

- Root `README.md`: three engines in every place that says two (layout, labels, test names,
  ratchet, build flags, dependency rule); the one profile table (A4) (F17).
- Root `PLAN.md`: status line (`DRAFT v3.1 … not started` to the executed record), D13 at
  the current version, the "Next" paragraph updated to say the Q15 levers shipped and the
  current numbers (F19, F30).
- `async/README.md` and `bridge/README.md` leftovers: the retired `ratio` label, the
  duplicated flag line, `macs_per_output` versus `macs_per_output_exact` (F16, F31).
- `docs/Doxyfile`: the rational engine's headers in `INPUT`, the family description (F14).
- `stage.h:77-78` and `rational/README.md:34`: "Q31 within 3.4e−9" stated with its input
  (the reference noise, 480 frames at peak 0.9), with the full-scale 1.6e−8 beside it (F06).
- The book: Part 7's "the symbol count is a test" now true (after S2); Part 4's `create`
  prose after fc2949e (F32); Part 5's RatioTap paragraphs to `bridge` (S3 may have taken
  these).
- `ci.yml` / `style.yml` / `ci-arm64.yml` comments: three engines; the Hexagon exclusion
  comment names all four excluded groups (F26, F27).
- `rational/PLAN.md` 5: the swapped passband deviations, the measured host time (F28).
- `STYLE.md` banner template: this copy is canonical from taphouse, so the fix is a taphouse
  PR and a drift-check bump, noted here and not done in this tree (F25).
- Closes: F06, F14, F16, F17, F19, F25 (referred), F26, F27, F30, F31, F32.
- Gate: the book builds (site and print); the A8 grep is run locally and returns zero before
  S5 turns it into a job.
- Size: medium in files, small in risk.

### S5 — The gates against recurrence (A8)

- `style.yml`: the retired-words job over the tree, list in `scripts/retired_words.txt`.
- `tests/family/`: a README quick-start compile test (the three READMEs' first code block
  extracted and compiled as a TU against the umbrella headers) so a quick start that stops
  compiling is a red check, not an audit finding.
- Root `CLAUDE.md`: the A4 rule, and one line that a change to any engine's shared
  vocabulary updates the root README's table and the other engines' READMEs in the same PR.
- Closes: nothing numbered; prevents the class of F15 to F19, F26, F29 to F34.
- Gate: both new checks green on `main`, each shown red once on a scratch branch.
- Size: small.

## 3. Disposition of every finding

| ID | Sev | Finding (short) | Step | Action |
|---|---|---|---|---|
| F01 | M | chain scratch bound breaks after a pulled stage is chained | S0 ✓ | DspTap: const accessor, reset on construction, comment, test |
| F02 | M | `uint8_t` advance truncates M ≥ 256 L | S1 ✓ | widen to `uint32_t`, test `ratio<3,1024>` |
| F03 | M | chain contract equation and pull do not exist | S0 ✓ | DspTap wording; rational PLAN 2.5 |
| F04 | M | symbol count called a test, none exists | S2 ✓ | `exported_symbols` ctest per engine |
| F05 | m | relaxed designs' Q15 stopband unmeasured | S1 ✓ | measure, state per row, record 147/160 |
| F06 | m | 3.4e−9 stated without provenance | S4 | state the input |
| F07 | m | compile_fail never isolates L ≠ M | S1 ✓ | fourth case, per-case clang check |
| F08 | m | search-bound comment stale, exception silent on bound | S1 ✓ | correct, name the bound |
| F09 | m | async accepts any preset | S2 ✓ | NULL on out-of-range, test |
| F10 | m | bindings name a DLL MSVC cannot export | S2 ✓ | export macro, MSVC capi leg ON |
| F11 | m | bare-metal floor 8 vs 88 | S1 ✓ | floor near the real count |
| F12 | m (disputed) | reset tests skip decimators | S1 ✓ | add decimator and chain |
| F13 | m | no stage-level tie pin (3 mutants survive) | S1 ✓ | constructed-tie tests |
| F14 | m | Doxygen omits rational | S4 | `INPUT`, description |
| F15 | m | async README 0.1.0 | S3 ✓ | 0.6.0 with the restructure |
| F16 | m | bridge README retired `ratio` label | S4 | fix |
| F17 | m | root README two engines | S4 | three engines, profile table |
| F18 | m | rational PLAN layout promises missing files | S1 ✓ | write the example, strike the row |
| F19 | m | root PLAN "Next" stale | S4 | update |
| F20 | n | stale pre-lever numbers in test comments | S1 ✓ | replace |
| F21 | n | Q31 pre-shift rationale count wrong | S0 ✓ | DspTap comment |
| F22 | n | `relaxed()` can invalidate; `stage_taps_per_phase` returns 0 | S1 ✓ | validate, throw |
| F23 | n | reference header is the clang-format reflow | S1 ✓ | guards in the generator |
| F24 | n | bridge/rational C headers state no thread contract | S2 ✓ | state it |
| F25 | n | banner rule unenforced; STYLE.md template pre-D14 | S4 | taphouse PR, referred |
| F26 | n | two-engine wording in CI comments | S4 | fix |
| F27 | n | Hexagon exclusion comment incomplete | S4 | fix |
| F28 | n | passband deviations swapped in PLAN 5 | S1 ✓ | fix with the measurement |
| F29 | n | bluetooth_bridge latency 1.9 / 2.0 / 1.93; "RatioTap" | S3 ✓ | one figure, family name |
| F30 | n | root PLAN status line and D13 version stale | S4 | update |
| F31 | n | duplicated flag line; "exact" accessor name | S4 | fix |
| F32 | n | book's `create` prose predates fc2949e | S4 | fix |
| F33 | n | async PLAN function count reads as 15 | S2 ✓ | fix |
| F34 | n | `rational.h` says Status: M3 | S1 ✓ | current status |
| F35 | n | `outputs_for` wraps above SIZE_MAX / L undocumented | S1 ✓ | document |
| C1 | — | `preset` vs `profile`; `economy` means two things | S3 ✓ | A4 |
| C2 | — | async C ABI shape differs from the siblings | S3 ✓ | A5 (maintainer) |
| C3 | — | async umbrella header and README in the old voice | S3 ✓ | rewrite to the sibling shape |
| C4 | — | `test_asrc_*` names; "RatioTap" residue | S3 ✓ | rename; A11 |
| C5 | — | `test_skeleton.cpp` is an M1 scaffold kept as a test | S1 | keep, retitled "substrate wiring"; it is the bridge's smoke test and costs nothing |
| C6 | — | no gate catches drift at the seams | S5 | A8, README compile test, the CLAUDE.md rule |

## 4. Sequencing and what each PR must not do

```
S0 (DspTap, then pin)  →  S1 (rational)  →  S2 (C ABI)  →  S3 (async, 0.6.0)  →  S4 (docs)  →  S5 (gates)
```

- S0 before S1: S1's `stage.h` contract sentence cites the DspTap wording.
- S2 before S3: S3's signature change (A5) rides on S2's export macro and symbol lists, so
  the symbol test is updated once.
- S4 after S3: prose describes the final names.
- S5 last: its grep is red until S4.
- No PR changes a datapath. The gate in every PR is the output hashes of all three engines
  and the ratchet; a moved hash is a finding, not a re-record.
- No PR touches `bridge/tests/reference/` or the cross-validation tolerances (R4).
- After each merge: repoint consumer pins per the root `CLAUDE.md` release flow; after S3,
  tag `v0.6.0`.

## 5. Non-goals

- Renaming `async`'s ladder to `bridge`'s words (A4 says why).
- Changing `async`'s push/pull FIFO shape to process/flush/reset: it absorbs a clock, the
  shape is its charter, and the family plan (D12) wants the difference visible.
- Running the embedded legs or the ratchet outside CI for this work; CI is the gate.
- Re-executing notebooks whose measured behaviour did not change.
- Reworking the book's Parts I to V beyond the stale names and the two sentences the audit
  named.

## 6. Decisions taken (2026-10-09)

The maintainer accepted every recommendation in section 1:

1. **A4**: `async`'s program-weighted tier is `filter_spec::program()`.
2. **A5**: the async C ABI takes the siblings' shape in S3 under 0.6.0. (No consumer of
   `tap_sr_async_create` outside this tree was named; the C notebooks in `async/notebooks/`
   are updated in the same PR.)
3. **A6**: the export macro and the MSVC C ABI leg ON.
4. **S1 / F18**: `examples/multirate_chain.cpp` is written; the cross-validation row is
   struck.
5. The `asrc` word is dropped from the async notebooks' prose cells in S3 (the notebook
   filenames stay, A11). Applied as: the retired `preset` word and the C ABI's old argument
   order left the notebooks' code and prose; the acronym ASRC, the generic name of what the
   engine is, stays wherever it means that (the engine's README defines it), as do the
   wrapper class `Asrc` in the notebooks' code cells. `scripts/book_figures.py` keeps the
   rendered titles that name "presets" until the figures are next regenerated (no
   matplotlib in this environment); its comments and identifiers changed.
