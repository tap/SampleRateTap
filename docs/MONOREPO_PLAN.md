# Monorepo plan: the `tap::sr` sample-rate family

Status: **DRAFT for adversarial review** — nothing below has been executed.
Written 2026-09-26. Once this plan is reviewed and approved, it becomes the
family-level `PLAN.md`. Until then it is the only document that describes
the change, and nothing in the repository depends on it.

This document has two jobs:

1. Record the **decisions** already taken (section 1), so the review can
   challenge them one by one rather than re-deriving them.
2. Specify the **migration** in enough detail (sections 4–7) that each step
   has a mechanical acceptance gate, and the review can find what is wrong
   or missing *before* any code moves.

Section 9 is the audit checklist: the specific questions the adversarial
review should try to break.

---

## 1. Decisions (proposed as settled; the review may reopen any)

| # | Decision | Rationale |
|---|---|---|
| D1 | **Merge SampleRateTap and RatioTap into one repository**; each engine keeps its own charter, CMake target, CI job and ratchet baselines | The engines are built to be composed and cross-checked against each other. Today that needs a test-only submodule of the sibling repo (`RatioTap/submodules/sampleratetap`), plus two DspTap pins to keep in step and a duplicated embedded toolchain. There are no external consumers yet, so the rename is free |
| D2 | **SampleRateTap is the host repository** and keeps its name | "Sample rate" describes the whole family once it is namespaced. The published book (tap.github.io/SampleRateTap) keeps its URL. SampleRateTap has more history (64 commits) than RatioTap (29) |
| D3 | **RatioTap's history is preserved** through a `git filter-repo` path rewrite and an unrelated-histories merge | `git log --follow` and `git blame` must keep working for every file of both engines |
| D4 | **Namespace `tap::sr::<engine>`**, include path `include/tap/sr/<engine>/` | Follows DspTap's rule that the path mirrors the namespace; today's `srt/` include path breaks it |
| D5 | **Engine names:** `async` (today's SampleRateTap), `ratio` (today's RatioTap); future `integer`, `pdm`, `varispeed` | `async` is the industry term (ASRC) and the family's own clock-topology vocabulary. It is the only async engine, and async at other ratios is reached by composition. Every other engine is sync, so those are named by what they convert |
| D6 | **One top-level directory per engine** (`async/`, `ratio/`, …), each with its own `include/ tests/ bench/ examples/ capi/ PLAN.md` | Keeps each charter's boundary physical, where a single shared `include/` tree would not |
| D7 | **Clean renames, no compatibility aliases** | There are no consumers yet; aliases would be permanent debt |
| D8 | **Unified C ABI prefix** `tap_sr_<engine>_*` and one shared library | One ctypes bridge serves all the notebooks |
| D9 | **CMake options** `TAP_SR_*` (e.g. `TAP_SR_BUILD_TESTS`, `TAP_SR_WERROR`), plus per-engine enables `TAP_SR_ENGINE_<ENGINE>` | Replaces `SRT_*` / `TAP_RATIO_*` |
| D10 | **DspTap stays a separate repository**, pinned once at `submodules/dsptap` | It has consumers outside the rate family (TapTools and others) |
| D11 | **Charter rule for new engines:** a capability gets a directory here when it has its own charter, optimization campaign and instruction-count ratchet. Building blocks (filter design, kernels, the `chain<>` composition template, `fractional_resampler`) go into DspTap | This was the repo-vs-substrate rule; it now decides engine directory vs. DspTap |

---

## 2. The family after the merge

| Engine | Namespace | Origin | Charter |
|---|---|---|---|
| `async` | `tap::sr::async` | SampleRateTap v0.1.0 | Asynchronous, near-unity (±`max_deviation_ppm`, default 1000 ppm): absorbs the clock |
| `ratio` | `tap::sr::ratio` | RatioTap v0.3.0 | Synchronous, 44.1 ↔ 48 kHz only (160/147, 147/160): converts the number |
| `integer` | `tap::sr::integer` | new (step 5) | Synchronous integer factors 2^a·3^b, up, down, and oversampling pairs; half-band/third-band (Nyquist L-th band) stages. Absorbs DspTap's `decimate.h` |
| `pdm` | `tap::sr::pdm` | new, when a consumer asks | 1-bit sigma-delta → PCM (MEMS mics, DSD): CIC → compensation FIR → `integer` stages |
| `varispeed` | `tap::sr::varispeed` | new, when a consumer asks | Time-varying ratio (varispeed, scrubbing, Doppler); bandlimited interpolation (Smith, CCRMA) |

Not engines, deliberately:

- **Timestamp-driven clock recovery** is a feature of `async`.
- **Minimum-phase and IIR low-latency tiers** are *profiles* of `ratio` and
  `integer`.
- An **offline FFT tier** would be a `transparent+` profile, built only if
  someone asks for it.

### 2.1 Rate coverage with `ratio` + `integer`

Every standard rate is 44.1k·2^a or 48k·2^a·3^b (b ∈ {0, −1}). Target coverage:

| From → To | Chain (proposed) | Intermediate rates |
|---|---|---|
| 48 ↔ 44.1 | `ratio` | — |
| 96 → 44.1 | `integer` ↓2 → `ratio` ↓ | 48 |
| 192 → 44.1 | `integer` ↓4 → `ratio` ↓ | 96, 48 |
| 44.1 → 96 / 192 | `ratio` ↑ → `integer` ↑2 / ↑4 | 48 |
| 48 → 16 / 8 | `integer` ↓3 / ↓6 | — |
| 44.1 → 16 | `ratio` ↑ → `integer` ↓3 | 48 |
| 16 → 48 | `integer` ↑3 | — |
| 48 → 32 | `integer` ↑2 → ↓3 | 96 |
| 44.1 → 32 | `ratio` ↑ → `integer` ↑2 → ↓3 | 48, 96 |
| 44.1 ↔ 22.05 / 11.025 | `integer` ↓2 / ↓4 (and ↑) | — |
| 88.2 ↔ 96, 176.4 ↔ 192 | **open — see Q3** | — |

**Chain invariant (proposed):** no intermediate rate may be lower than
min(input, output), so the chain never throws away band it must deliver.
48 → 32 is therefore ↑2 then ↓3, never ↓3 then ↑2, which would cut the band
at 8 kHz.

The coverage matrix is generated and pinned by tests, not written by hand.
For each pair it records the MACs per output, total latency and measured
floor. **It is not produced in this migration.** It arrives with `integer`
(step 5).

---

## 3. Inventory of the two repositories (measured 2026-09-26)

Branch `claude/sample-rate-expansion-strategies-ezqzu6` in both repositories,
at SampleRateTap `2b4dff1` and RatioTap `349ab7b`.

### 3.1 Shipped headers

| Today | After |
|---|---|
| `SampleRateTap/include/srt/{asrc,pi_servo,polyphase_filter,sample_traits,spsc_ring,srt}.h` (1 525 lines) | `async/include/tap/sr/async/…` |
| `SampleRateTap/include/srt/detail/kaiser.h` (26 lines: a re-export shim of `tap::dsp` kaiser) | **deleted**; callers include `tap/dsp/kaiser.h` directly |
| `RatioTap/include/tap/ratio/{converter,design,phase_table,ratio,schedule}.h` (820 lines) | `ratio/include/tap/sr/ratio/…` |

`srt/sample_traits.h` layers on `tap/dsp/sample_traits.h` rather than
duplicating it. It moves unchanged. Folding it into DspTap is out of scope.

### 3.2 Pins

- Both repositories record DspTap at **`0eb09fa`**. The pins agree, so step 1
  needs no pin reconciliation.
- RatioTap also pins `submodules/sampleratetap` at `2b4dff1`, which equals
  SampleRateTap's HEAD. After the merge the pin disappears, and the
  cross-validation compiles against the in-tree `async` engine **at the same
  tree**.
- Note: in this session's checkouts the working-tree submodules sit at
  `28a34a1` / `5315689`, not the recorded pins. The migration must build
  against the **recorded** pins (`git submodule update --init` before every
  gate).

### 3.3 Duplicated infrastructure: measured divergence

RatioTap's copies were ported from SampleRateTap. The measured differences
are **cosmetic only**: comments, mdBook `ANCHOR` markers (present only on
the SampleRateTap side) and name prefixes.

| File | Changed lines | Nature |
|---|---|---|
| `.clang-format`, `.clang-tidy`, `STYLE.md`, `.pre-commit-config.yaml`, `.claude/hooks/session-start.sh`, `.claude/settings.json`, `scripts/tidy.sh` | 0 | identical |
| `platform/armv8m_startup.c` | 21 | comments, copyright line, book anchors |
| `platform/mps2_an505/*.ld`, `platform/mps3_an547/*.ld` | 5 each | book anchors, provenance comment |
| `cmake/arm-cortex-m33-mps2.cmake`, `…-m55-mps3.cmake`, `hexagon-linux-musl.cmake` | 10 / 21 / 12 | `SRT_`/`TAP_RATIO_` option names, comments, anchors |
| `scripts/icount.py` | 19 | binary prefix `srt_icount_*` vs `ratio_icount_*`; markers `SRT_INSN_COUNT` / `SRT_ICOUNT_DONE` vs `RATIO_*` |
| `tools/qemu_insn_plugin/insn_count.c` | 13 | to be diffed line by line in step 2 (**audit item A6**) |

Keep the SampleRateTap copies, because they carry the book anchors, and
parameterize the prefixes.

Present in only one repository:

- **SampleRateTap only:** `cmake/r8brain.cmake`, `tools/compare_shim`,
  `bench/compare`, `compare.yml`, `ci-arm64.yml` (native weak-memory + TSan),
  `book-pages.yml`, `book/`, `docs/`, `scripts/update_*_docs.py`,
  `scripts/book_figures*`, `examples/pico2_*`.
- **RatioTap only:** `scripts/fetch_hexagon_toolchain.sh`,
  `tools/reference/make_reference_vectors.py`, `tests/reference/`,
  `HANDOFF.md`, and the CI dedup scheme (branch-filtered `push` +
  `pull_request`, commits `f1e566a` and `94775b0`).

### 3.4 The book

`book/src` pulls in live code with `{{#include path:ANCHOR}}`, and CI fails
on a stale anchor. By path:

- `include/srt/*` — 42 includes (`polyphase_filter.h` 17, `sample_traits.h` 8, `pi_servo.h` 8, `asrc.h` 5, `spsc_ring.h` 4)
- `submodules/dsptap/…` — 21 (unaffected by the move)
- `platform/`, `cmake/`, `tools/`, `tests/` — 20

Every non-DspTap include path changes in step 3. This is a mechanical
rewrite, but it is gated by the book build (`mdbook build`, warnings are
errors).

### 3.5 CI and the ratchet

| Job | SampleRateTap | RatioTap |
|---|---|---|
| Host matrix (GCC, Clang, AppleClang, MSVC) | `build-and-test` | `build-test` |
| Sanitizers (ASan+UBSan, TSan) | yes | yes |
| Linux arm64 native + TSan | `ci-arm64.yml` | — |
| Hexagon / M55 / M33 under QEMU | yes | yes |
| `icount-ratchet` | 7 workloads × {m33, m55, hexagon}; README table drift check | 10 workloads × {m33, m55, hexagon} (RatioTap's CLAUDE.md still says eight) |
| Resampler comparison | `compare-smoke`, `compare.yml` | — |
| Style (clang-format, tidy, drift) | yes | yes |
| Book | `book`, `book-pages.yml` | — |

Baselines: `SampleRateTap/bench/baselines.json`, `RatioTap/bench/baselines.json`.
Both are keyed by target, then by workload. They are **not** merged into
one file (see step 2).

### 3.6 C ABI

`srt_capi.h` (≈8 exported functions) and `ratio_capi.h` (≈11) are
consumed through ctypes: RatioTap's notebooks via `notebooks/ratiotap_py.py`,
and SampleRateTap's via ctypes code inline in each notebook (three of its
four notebooks).

### 3.7 Outside references to fix

- **DspTap:** `README.md`, `CLAUDE.md`, `STYLE.md`, `platform/README.md`,
  `docs/audit-fft-and-code-smells.md` and five test files mention
  SampleRateTap or RatioTap by name.
- **RatioTap:** `CLAUDE.md`, `PLAN.md`, `HANDOFF.md`, `README.md` URLs.
- **SampleRateTap:** `README.md` ("Position in the Tap family"), `book/`,
  `docs/COMPARISON.md`, `docs/PERFORMANCE.md`, `Doxyfile`.

---

## 4. Target layout

```
SampleRateTap/
├── CMakeLists.txt          project(SampleRateTap); umbrella target tap::sr
├── CLAUDE.md               family-level guidance (new; SampleRateTap has none today)
├── PLAN.md                 this document, promoted after review
├── README.md               family front page: which engine, the coverage matrix
├── STYLE.md .clang-* .pre-commit-config.yaml .claude/   (unchanged, identical today)
├── submodules/dsptap       the ONE pin
├── cmake/  platform/       shared embedded toolchains (SampleRateTap copies)
├── tools/qemu_insn_plugin/ shared
├── scripts/                icount.py (engine-aware), tidy.sh, fetch_hexagon_toolchain.sh,
│                           book/doc updaters
├── book/                   one book; ratio chapters are follow-up work
├── notebooks/sr_py.py      one ctypes bridge over the unified C ABI
├── capi/                   tap_sr C ABI: one shared library, per-engine sources
├── async/
│   ├── PLAN.md             the async charter (drawn from today's README/docs)
│   ├── include/tap/sr/async/
│   ├── tests/  bench/  examples/  notebooks/  docs/
│   └── CMakeLists.txt      target tap::sr::async
└── ratio/
    ├── PLAN.md  HANDOFF.md (RatioTap's, history preserved)
    ├── include/tap/sr/ratio/
    ├── tests/  bench/  examples/  notebooks/  tools/reference/
    └── CMakeLists.txt      target tap::sr::ratio
```

Dependency rule, enforced by CMake:

- `ratio` → `tap::dsp` only.
- `async` → `tap::dsp` only.
- An engine may depend on another **only in tests and examples**
  (`ratio/tests` → `tap::sr::async` for cross-validation; the
  `bluetooth_bridge` example composes both).
- A shipped header that includes a sibling engine is a CI failure: a
  header-isolation test compiles each engine's headers against `tap::dsp`
  alone.

---

## 5. Migration steps

Each step is one or more commits on
`claude/sample-rate-expansion-strategies-ezqzu6` in SampleRateTap. Each has
a gate that must be green before the next begins. Nothing reaches `main`
until a PR is reviewed.

### Step 0 — Freeze and snapshot

- Record the green state of both repositories at the SHAs above:
  - `ctest` pass lists per host;
  - `icount.py` output per target (these must match the committed
    baselines);
  - C ABI function lists.
- Merge nothing into either repository's `main` until step 4 lands. If
  anything must land, it lands in SampleRateTap only, and step 1 is re-cut.

**Gate:** both repositories are green on CI at the recorded SHAs, and the
snapshot artifacts are committed under `docs/migration/` (removed in step 4).

### Step 1 — History-preserving import (no content changes)

1. `git mv` SampleRateTap's engine files (`include/`, `tests/`, `bench/`,
   `examples/`, `tools/capi`, `tools/compare_shim`, `notebooks/`, `docs/`)
   into `async/`. Shared files stay at the root.
2. In a scratch clone of RatioTap, run
   `git filter-repo --to-subdirectory-filter ratio/`.
3. Merge with `git merge --allow-unrelated-histories`.
4. Remove the duplicate shared files from `ratio/`: `.clang-*`, `STYLE.md`,
   `.pre-commit-config.yaml`, `.claude/`, `scripts/tidy.sh`, `LICENSE` (see
   Q5), `.gitmodules`, `submodules/`.
5. Keep, for now: `ratio/cmake`, `ratio/platform`,
   `ratio/tools/qemu_insn_plugin`, `ratio/scripts/icount.py`. These are
   deduplicated in step 2, so this step stays content-free.
6. Minimal CMake glue: a root `CMakeLists.txt` that `add_subdirectory`s
   `async/` and `ratio/`, with each keeping its **current** target and
   option names. Point `ratio`'s `srt_headers` at `async/include`, which
   replaces the `submodules/sampleratetap` path.

**Gate:**

- `git log --follow` works on a sample of files from both engines.
- Host build and `ctest` pass lists equal the step-0 snapshot (same test
  names, same count).
- Every QEMU leg is green.
- `icount.py` for **both** engines reproduces its committed baselines
  **exactly** (0 %). Nothing that reaches codegen has changed, so any drift
  is a bug in the import.

### Step 2 — Shared infrastructure

- Delete `ratio/cmake`, `ratio/platform` and
  `ratio/tools/qemu_insn_plugin`; `ratio` uses the root copies. Merge any
  non-cosmetic difference in `insn_count.c` first (**A6**).
- Make `scripts/icount.py` engine-aware: `--engine async|ratio`, a binary
  prefix `tap_sr_<engine>_icount_*`, and uniform markers `TAP_SR_INSN_COUNT` /
  `TAP_SR_ICOUNT_DONE`. Baselines move to `<engine>/bench/baselines.json`,
  with the same keys.
- One CI workflow:
  - The host matrix and sanitizers build every engine.
  - `icount-ratchet` runs as a matrix over engine × target, each engine
    against its own baselines.
  - `ci-arm64`, `compare`, `book` and style stay as they are.
  - Adopt RatioTap's CI dedup scheme family-wide.
- One DspTap pin: already at root since step 1; verify there is no second
  checkout.

**Gate:** as for step 1, plus **0 % icount drift** for both engines on all
three targets. The toolchain files and startup code are now shared, so any
drift means the deduplication changed codegen.

### Step 3 — Renames

In one commit per rename class, each gated separately:

1. **Include paths:** `srt/…` → `tap/sr/async/…`; `tap/ratio/…` →
   `tap/sr/ratio/…`. Delete `srt/detail/kaiser.h` and point its callers at
   `tap/dsp/kaiser.h`.
2. **Namespaces:** `tap::samplerate` → `tap::sr::async`; `tap::ratio` →
   `tap::sr::ratio`. Optionally, `async_sample_rate_converter` →
   `tap::sr::async::converter` (**Q2**).
3. **CMake:**
   - Targets `tap::sr::async`, `tap::sr::ratio`, umbrella `tap::sr`.
   - Options `TAP_SR_*`.
   - Remove `SampleRateTap::SampleRateTap`, `tap::samplerate`, `tap::ratio`
     (D7).
4. **C ABI:**
   - `srt_*` → `tap_sr_async_*`; ratio's → `tap_sr_ratio_*`.
   - One library, `capi/`.
   - One bridge, `notebooks/sr_py.py`.
   - `srt_version()` → `tap_sr_version()`, plus a per-engine version (**Q4**).
5. **Book:** rewrite the include paths (section 3.4), move anchors to the
   new paths, and fix figure scripts.
6. **Ratchet names:** workload and binary renames. Baseline **keys stay
   unchanged**, so history remains comparable.

**Gate:**

- Tidy gate and a local clang `-Werror` build pass.
- `mdbook build` passes with warnings as errors.
- `ctest` lists equal the snapshot, modulo mechanical renames (a mapping
  table is committed).
- icount drift is 0 %. A symbol rename must not move codegen; if a
  namespace-length change shifts the instruction count of anything
  (it should not), it is investigated rather than re-recorded.
- Every notebook re-executes clean against the new bridge, with outputs
  equal to the committed ones (numbers identical, only paths and names
  differ).

### Step 4 — Documentation and archival

- Promote this document to `PLAN.md`. Write the family `CLAUDE.md`
  (combining both repositories' guidance, and the dependency rule of
  section 4). Rewrite `README.md` as the family front page. Add per-engine
  `PLAN.md`s.
- DspTap: update the references in section 3.7, as a DspTap PR.
- Delete `docs/migration/`.
- **By the user, on GitHub:** archive RatioTap with a README pointer. The
  repository rename is not needed (D2).

**Gate:** a PR to SampleRateTap `main` that is green on every job; the DspTap
docs PR merged or approved.

### Step 5 — First new engine: `integer` (separate plan)

`integer` is out of scope for this migration and gets its own PLAN.md
reviewed the same way. The sequence already decided:

1. The L-th band design lands in DspTap.
2. `decimate.h` moves out of DspTap into `integer/` (a DspTap PR first; its
   only in-repo users are its tests and the C ABI).
3. The `chain<>` template lands in DspTap.
4. The coverage matrix of section 2.1 becomes a test.

---

## 6. What stays unchanged (explicit non-goals)

- No algorithm, coefficient, filter design or process-loop change anywhere.
  Every output stays bit-identical, and every icount stays within 0 %.
- No new engine, profile or API function.
- DspTap content is untouched; only its docs change.
- Versioning is not unified in this migration (**Q4**).

---

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | A step silently drops a test (e.g. a CMake glob moves) | The gate compares **named** test lists against the step-0 snapshot, not just "green" |
| R2 | Infrastructure dedup changes codegen (a startup or linker-script difference) | Divergence measured as cosmetic (section 3.3); gate at 0 % icount drift on every target |
| R3 | Book anchors break across the move | `mdbook build` with warnings as errors is part of the step 3 gate; the paths are enumerated (section 3.4) |
| R4 | Cross-validation passes against a different SampleRateTap than before | RatioTap pins `2b4dff1` == SampleRateTap HEAD; step 1 must start from exactly that tree |
| R5 | `filter-repo` rewrites RatioTap commit SHAs, breaking references in its docs and commit messages ("M7d", PR numbers) | SHAs cited in `ratio/PLAN.md` are listed and annotated with a pointer to the archived RatioTap repository |
| R6 | Two ratchets in one CI multiply QEMU time and cache contention | The matrix runs engine × target in parallel; the existing digest-keyed toolchain cache is shared |
| R7 | Merged `main` history interleaves two engines' commits and confuses `git bisect` | Accepted; `--first-parent` bisect on `main` is unaffected |
| R8 | The monorepo erodes charter boundaries over time | The dependency rule is enforced by CMake plus the header-isolation test (section 4); per-engine PLAN.md files |

---

## 8. Open questions for the review

- **Q1.** Should `ratio` keep `HANDOFF.md` in-tree (history), or retire it to
  the archived repository?
- **Q2.** Rename `async_sample_rate_converter` → `tap::sr::async::converter`
  for symmetry with `ratio`'s `converter`, or keep the descriptive name?
- **Q3.** 88.2 ↔ 96 and 176.4 ↔ 192. Either widen `ratio`'s charter to the
  2× and 4× rates (its designs are specified in Hz, so this is a
  re-specification), or chain `integer` ↓2 → `ratio` → `integer` ↑2, which
  breaks the chain invariant of section 2.1 by passing through 44.1 on a
  48-family path. Decide before `integer`'s plan.
- **Q4.** Versioning: SampleRateTap is 0.1.0 and RatioTap is 0.3.0. Options
  are one family version, per-engine versions, or both.
- **Q5.** License and copyright lines differ ("SampleRateTap contributors"
  vs "Timothy Place and the RatioTap contributors"). Unify to one holder
  line family-wide?
- **Q6.** Does the family README absorb the "Position in the Tap family"
  material from both repositories, or link to per-engine READMEs?

## 9. Adversarial audit checklist

The review should try to **break** each item, not confirm it.

- **A1 — Decisions:** is any of D1–D11 wrong, or inconsistent with the
  DspTap, RatioTap or SampleRateTap CLAUDE.md/PLAN.md rules
  ("substrate lands in DspTap first", "never route by rate", "correctness
  before optimization")?
- **A2 — Chain invariant:** is "no intermediate rate below min(in, out)"
  the right rule? Find a standard rate pair it forbids that must be
  supported, or one it allows that loses band.
- **A3 — History:** does the `filter-repo` + unrelated-merge recipe keep
  `--follow`/`blame` for *every* file? (RatioTap's history contains no
  renames today (`git log --diff-filter=R` is empty); re-check at cut time.)
- **A4 — Gates:** is each gate actually sufficient to detect a regression
  the step could introduce? Name a regression that passes a gate.
- **A5 — Bit-identity claims:** are "0 % icount drift" gates achievable?
  Consider whether any symbol-name length, section order or string table
  reaches the measured instruction stream (the icount counts executed
  instructions, not size).
- **A6 — Infrastructure:** diff `insn_count.c`, the startup code and the
  linker scripts line by line. Is any difference functional?
- **A7 — Cross-validation:** after the merge, is `ratio`'s cross-validation
  still an *independent* check, or does sharing one tree with `async` create
  a common-mode failure the separate repositories prevented?
- **A8 — Dependency rule:** can the header-isolation test be bypassed? Does
  `bluetooth_bridge` pull `async` into a shipped target anywhere?
- **A9 — Inventory completeness:** is anything in either repository missing
  from section 3 (files, workflows, secrets, Pages configuration, badges,
  issue references, the Pico 2 examples' build paths)?
- **A10 — Reversibility:** if the migration is abandoned after step 2, is
  RatioTap still intact and usable? (It should be: nothing touches the
  RatioTap repository until step 4.)
- **A11 — Consumers:** is it really true that nothing outside these
  repositories consumes `srt/`, `tap::samplerate`, `tap::ratio` or the C
  ABIs (TapTools, TapTools-Max, MuTap, AmbiTap, any Pages or notebook
  links)?
