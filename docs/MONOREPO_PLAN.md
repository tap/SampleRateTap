# Monorepo plan: the `tap::sr` sample-rate family

Status: **DRAFT v2.1. Revised after adversarial audit; open questions answered; nothing executed.**

- v1 (2026-09-26, `11f2a94`): the first draft.
- v2 (2026-09-26): folds in the five-reviewer adversarial audit. It had 78
  findings, of which 6 were blockers.
- v2.1 (2026-09-26): records the user's answers to Q2, Q4, Q5, Q7 and Q8
  (D13–D15) and the confirmed outside-repository facts.
- Appendix A maps every finding ID (DEC-, GIT-, GATE-, INF-, INV-) to where
  it landed in this document, or to why it was rejected.

Once this plan is approved, it becomes the family-level `PLAN.md`. Until
then it is the only document that describes the change, and nothing depends
on it.

How to read it:

- Section 1 holds the decisions.
- Section 2 is the family, the charters and the coverage rules.
- Section 3 is the measured inventory.
- Section 4 is the target layout and a file-by-file disposition table.
- Section 5 defines the gates once.
- Section 6 is the migration: pre-work step P, then steps 0–5, each
  referencing those gates.
- Sections 7–9 are the non-goals, risks and open questions.

---

## 1. Decisions

| # | Decision | Rationale / supersession |
|---|---|---|
| D1 | **Merge SampleRateTap and RatioTap into one repository**; each engine keeps its own charter, CMake target, CI coverage and ratchet baselines | The engines are built to be composed and cross-checked against each other. **This supersedes** HANDOFF.md preamble item 1 ("separate repo") and RatioTap PLAN.md §2 (SampleRateTap as a test-only dependency). Those were decided when the family had two engines and no plan for more. With five engines planned, the cost of pins, duplicated harnesses and cross-repository test dependencies grows with every addition. There are no external consumers, so the rename is free |
| D2 | **SampleRateTap is the host repository** and keeps its name | "Sample rate" names the whole family once it is namespaced. The published book (tap.github.io/SampleRateTap) keeps its URL. It is also the larger history: **136** commits on `main` (full clone), against RatioTap's 29 |
| D3 | **RatioTap's history is preserved** through a `git filter-repo` path rewrite that also rewrites `.gitmodules` throughout history (section 6, step 1), plus an unrelated-histories merge. **The final PR is merged with a merge commit, never squash or rebase** | `git log --follow` and `git blame` keep working for every file. A squash would erase all 29 imported commits (GIT-1) |
| D4 | **Namespace `tap::sr::<engine>`**, include path `include/tap/sr/<engine>/` | Follows DspTap's rule that the path mirrors the namespace. **This supersedes** the rename agreed in RatioTap PLAN.md (`include/srt/` → `include/tap/samplerate/`) and extends the taphouse convention of one `tap::<library>` sub-namespace per repository to two levels. The two-level form is **accepted** (user decision, 2026-09-26). The convention note goes into taphouse's `STYLE.md` through a taphouse PR (step 5) |
| D5 | **Engine names:** `async` (today's SampleRateTap), `ratio` (today's RatioTap); future `integer`, `pdm`, `varispeed` | `async` is the industry term (ASRC) and the family's own clock-topology vocabulary. It is the only async engine, and async at other ratios is reached by composition. Every other engine is sync and is named by what it converts. Known wrinkle: in code with `using namespace tap::sr`, these names sit beside `std::ratio` and `std::async`. The house style already avoids namespace-wide `using` directives |
| D6 | **One top-level directory per engine** (`async/`, `ratio/`, …), each with its own `include/ tests/ bench/ examples/ capi/ notebooks/ README.md PLAN.md` | Keeps each charter's boundary physical, where a single shared `include/` tree would not |
| D7 | **Clean renames, no compatibility aliases.** Removed user-facing override macros get an `#error` **tripwire**, not an alias | Aliases would be permanent debt. A tripwire makes a stale `-DSRT_CP_MIN_CHANNELS=…` a loud error rather than a silent no-op (GATE-14) |
| D8 | **C ABI prefix `tap_sr_<engine>_*`, one shared library per engine** (`tap_sr_async_capi`, `tap_sr_ratio_capi`), one bridge module per engine | Per-engine libraries keep `capi/` inside the engine boundary, so no shipped artifact links two engines (DEC-14). A combined notebook library can come later if a notebook needs both engines |
| D9 | **CMake options `TAP_SR_*`**: `TAP_SR_BUILD_TESTS`, `…_EXAMPLES`, `…_CAPI`, `…_ICOUNT_BENCH`, and per-engine `TAP_SR_<ENGINE>_WERROR`. There is **no per-engine enable switch in the migration** | A per-engine WERROR keeps ratio's MSVC `/WX` gate, which async has not triaged yet (INF-11). Per-engine enables could silently drop the cross-validation (DEC-12), so they are deferred. If they are ever added, ratio tests ON with async OFF is a `FATAL_ERROR` |
| D10 | **DspTap stays a separate repository**, pinned once at `submodules/dsptap` | It has consumers outside the rate family: TapTools, and MuTap through the `LogMel`/`Decimator` C ABI |
| D11 | **Charter rule for new engines:** a capability gets an engine directory here when it has its own charter, optimization campaign and ratchet. Building blocks (filter design math, kernels, the `chain<>` template) go into DspTap. **Engine-owned datapaths stay with their engine.** In particular, `fractional_resampler`, the polyphase bank and the blend stratum are `async`'s and never move to DspTap | Agrees with RatioTap PLAN.md Appendix A ("the blend stratum does not move"). Keeping async's datapath out of DspTap is also what keeps `ratio`'s cross-validation oracle outside `ratio`'s reach (DEC-6) |
| D12 | **No routing by rate, ever, including through composition.** `chain<>` is a caller-named, compile-time composition of **synchronous** stages. There is no `(in_hz, out_hz) → engine` lookup, `async` is never selected by a chain, and the coverage matrix *documents* chains without dispatching them | Keeps HANDOFF preamble item 4 ("factory dropped; clock topology is routed by type choice") intact as the family grows (DEC-8) |
| D13 | **One family version**, starting at **0.4.0**: `project(SampleRateTap VERSION 0.4.0)`, macros `TAP_SR_VERSION_{MAJOR,MINOR,PATCH}`, one C function `tap_sr_version()` encoded `(M<<16)\|(m<<8)\|p`, and tags `vX.Y.Z` | User decision, 2026-09-26. 0.4.0 sits above both current versions (async 0.1.0, ratio 0.3.0), so neither engine appears to go backwards. The bit-packed encoding is RatioTap's, already pinned by `test_skeleton.cpp`, and has room above 99. Any engine change bumps the family version |
| D14 | **One copyright holder line family-wide:** "Copyright 2026 Timothy Place and the SampleRateTap contributors" in the root `LICENSE` and in every file banner | User decision, 2026-09-26. Both repositories' notices name the same author, and RatioTap's contributors become SampleRateTap contributors when the histories merge, so this is a restatement, not a relicensing. `ratio/LICENSE` is kept until the root `LICENSE` carries the unified line (step 1c), then deleted in the same commit |
| D15 | **`async_sample_rate_converter` → `tap::sr::async::converter`** | User decision, 2026-09-26. Matches `tap::sr::ratio::converter`; the namespace already says "async" |

---

## 2. The family after the merge

| Engine | Namespace | Origin | Charter |
|---|---|---|---|
| `async` | `tap::sr::async` | SampleRateTap v0.1.0 | Asynchronous, near-unity (±`max_deviation_ppm`, default 1000 ppm): absorbs the clock |
| `ratio` | `tap::sr::ratio` | RatioTap v0.3.0 | Synchronous **160/147 pair at 44.1·2^k ↔ 48·2^k** (k = 0, 1, 2): converts the number. See 2.2 |
| `integer` | `tap::sr::integer` | new, separate plan | Synchronous **rational L/M with L, M ∈ {2^a·3^b}**: integer up, down, oversampling pairs, and 2/3 · 3/2 steps. Nyquist (L-th band) stages. Does **not** absorb DspTap's `decimate.h` (DEC-7) |
| `pdm` | `tap::sr::pdm` | new, when a consumer asks | 1-bit sigma-delta → PCM (MEMS mics, DSD): CIC → compensation FIR → `integer` stages |
| `varispeed` | `tap::sr::varispeed` | new, when a consumer asks | Time-varying ratio: bandlimited interpolation (Smith, CCRMA) |

Not engines:

- **Timestamp-driven clock recovery** is an `async` feature.
- **Minimum-phase and IIR low-latency tiers** are profiles of `ratio` and
  `integer`.
- **An offline FFT tier** would be a `transparent+` profile, built only if
  someone asks.
- **`decimate.h`** stays in DspTap as the speech front end MuTap consumes.
  `integer` builds on the same L-th-band design math beside it. Moving it
  would need its own consumer plan, with a MuTap pin.

### 2.1 Coverage rule: passband, not Nyquist

v1's invariant "no intermediate rate below min(in, out)" is withdrawn
(DEC-3). It allowed band loss the profiles already take (`economy` is flat
to 18 kHz), and it forbade harmless chains: 176.4 → 44.1 → 48 would have
had to run `ratio` at 4× the rate.

**The rule instead, per chain and profile:**

1. The chain's passband edge is the **minimum over its stages** of each
   stage's passband edge (in Hz at that stage's rate).
2. That edge must be **≥ the passband the profile declares for the pair**.
3. Every intermediate Nyquist frequency must be **> the declared passband
   plus the next stage's transition band**.
4. The coverage-matrix test pins the resulting edge, MACs per output and
   latency for every pair.

**Supported rates:** 8, 11.025, 12, 16, 22.05, 24, 32, 44.1, 48, 88.2, 96,
176.4, 192 and 384 kHz. That is every rate of the form 44.1k·2^a or
48k·2^a·3^b that the family supports.

**Explicitly excluded, with the reason:**

- 37.8 and 50.4 kHz (ratios of 7).
- The 1000/1001 video pull-down rates (44.056 and 47.952 kHz). These are
  *synchronous* ratios whose 999 ppm offset happens to fall inside
  `async`'s ±1000 ppm. They must **never** be served by `async`, because
  that would be routing by rate. If they are ever supported, it is as a
  sync engine.

**The full 14 × 14 matrix is generated, not hand-picked.** It arrives with
`integer`'s plan, with every pair marked supported, excluded or
not-yet-supported. Illustrative rows (all respect 1–3):

| From → To | Chain |
|---|---|
| 48 ↔ 44.1 | `ratio` |
| 96 ↔ 88.2, 192 ↔ 176.4 | `ratio` at k = 1, 2 (section 2.2) |
| 96 → 44.1 | `integer` ↓2 → `ratio` |
| 176.4 → 48 | `integer` ↓4 → `ratio` (the passband rule allows 44.1 intermediate) |
| 48 → 88.2 | `integer` ↑2 → `ratio` k = 1 |
| 44.1 → 16 | `ratio` → `integer` ↓3 |
| 48 → 32 | `integer` 2/3 (one rational stage, not ↑2 then ↓3) |

### 2.2 `ratio` at 2× and 4× rates: resolves v1's Q3

The Hz values in `ratio`'s profiles feed only a normalized cutoff
(`design.h:138`). The converter is a pure sample-count transformer, and the
charter bans other **ratios** ("not 2:1, not 96→44.1"), not the same ratio
at a multiple of the rate. Fed 88.2 kHz, today's tables give a 36 kHz
passband, and alias products land above 40.2 kHz.

**Decision:**

- Restate the charter as "160/147 at 44.1·2^k ↔ 48·2^k".
- Document profile edges as fractions of the rate, with the Hz figures as
  the k = 0 labels.
- Add a contract test that the table designed at k = 1 labels is
  **bit-identical** to the k = 0 table.

This is a charter and documentation change in `ratio`, made after the
migration (it is not part of it). The `direction::up_to_48k` /
`down_to_44k1` names are reconsidered then.

---

## 3. Inventory (measured 2026-09-26; corrected in v2)

The migration runs on **fresh, full GitHub clones**. The session checkouts
are unfit for it:

- SampleRateTap's session checkout is **shallow** (8 grafts), which is why
  v1 reported 64 commits.
- Both checkouts have **stale local `main`** branches: SampleRateTap
  `0922541`, RatioTap `94775b0`.
- Remote truth: SampleRateTap `main` = `2b4dff1` (136 commits); RatioTap
  `main` = `349ab7b` (29 commits, no renames in history, no tags). Neither
  repository has tags.
- RatioTap's leftover remote branch `claude/sample-rate-solutions-comparison-mqc190`
  (merged PR #17) is not imported. It should be deleted.

### 3.1 Shipped headers

| Today | After |
|---|---|
| `include/srt/{asrc,pi_servo,polyphase_filter,sample_traits,spsc_ring,srt}.h` (1 525 lines) | `async/include/tap/sr/async/…` |
| `include/srt/detail/kaiser.h` (26 lines: re-exports `tap::dsp` kaiser into `tap::samplerate::detail`) | **Deleted in step 3.** `polyphase_filter.h:153,157` qualifies the calls as `tap::dsp::`. `tests/test_kaiser.cpp` (9 tests, `using namespace tap::samplerate::detail`) repoints to `tap::dsp`, keeping test names. The path citations in the book, the bibliography, `book_figures.py:7` and `asrc_rbj_analysis.ipynb` are rewritten (INV-7) |
| `include/tap/ratio/{converter,design,phase_table,ratio,schedule}.h` (820 lines) | `ratio/include/tap/sr/ratio/…` |

`srt/sample_traits.h` layers on `tap/dsp/sample_traits.h` and moves
unchanged.

### 3.2 Pins

- Both repositories record DspTap at **`0eb09fa`**.
- RatioTap pins `submodules/sampleratetap` at `2b4dff1` = SampleRateTap
  `main`.
- Every gate runs `git submodule update --init --recursive` first. Session
  hooks move checkouts off their recorded pins (observed in this session),
  so the gates must not trust the checkout.

### 3.3 Duplicated infrastructure

There are **three** copies of the embedded harness: SampleRateTap,
RatioTap, **and DspTap**. DspTap has `cmake/arm-cortex-m33-mps2.cmake`,
`platform/`, `tools/qemu_insn_plugin/` and `scripts/icount.py`, which
differ from SampleRateTap's by 40–54 lines. The merge removes one of the
three. Consuming the harness from `submodules/dsptap` instead is the natural
next deduplication. It is **out of scope** here because DspTap's copy has
diverged, and adopting it would change async's and ratio's codegen inputs
(DEC-9).

SampleRateTap vs RatioTap, line by line:

| File | Class | Detail |
|---|---|---|
| `.clang-*`, `STYLE.md`, `.pre-commit-config.yaml`, `.claude/**`, `scripts/tidy.sh`, `.github/pull_request_template.md` | identical | |
| `platform/armv8m_startup.c`, `platform/*/**.ld` | cosmetic | Comments, copyright line, book `ANCHOR`s. Memory map, heap, MSPLIM, vectors, `_sbrk` and atomics are byte-identical |
| `cmake/arm-cortex-m33/m55-*.cmake`, `hexagon-linux-musl.cmake` | **functional (one variable)** | Flags are identical. `set(SRT_BARE_METAL ON)` vs `set(TAP_RATIO_BARE_METAL ON)` selects each engine's one-shot test mode and the gtest `GTEST_HAS_*` definitions (INF-1) |
| `tools/qemu_insn_plugin/insn_count.c` | **functional (host-side marker)** | Prints `SRT_INSN_COUNT` vs `RATIO_INSN_COUNT`, which `icount.py` parses. Host-side, so it never affects the guest count |
| `scripts/icount.py` | **functional (prefix, marker)** | `srt_icount_*`/`SRT_*` vs `ratio_icount_*`/`RATIO_*`. Tolerance logic identical |
| `tests/bare_metal_main.cpp` | **per-engine, never deduplicated** | Engine-specific filter, floor (15 vs 25) and completion marker |
| `.github/workflows/style.yml` | **functional** | RatioTap's configures tests and icount ON, excludes `submodules` and `_deps`, and reads file lists line-wise. SampleRateTap's configures nothing and would lint **zero** TUs in a monorepo (INF-10). RatioTap's body wins |
| `scripts/fetch_hexagon_toolchain.sh` (RatioTap only) | **functional** | Checks the pin and `SHA256SUMS` unconditionally. SampleRateTap's inline copies skip `SHA256SUMS` or make the pin check conditional (INF-13). RatioTap's script wins |

Present in only one repository:

- **SampleRateTap:** `cmake/r8brain.cmake`, `tools/compare_shim`,
  `bench/compare`, `compare.yml`, `ci-arm64.yml`, `book-pages.yml`,
  `book/`, `docs/`, `scripts/update_*_docs.py`, `scripts/book_figures*`,
  `examples/pico2_*`, `.git-blame-ignore-revs`.
- **RatioTap:** `scripts/fetch_hexagon_toolchain.sh`, `tools/reference/`,
  `tests/reference/`, `HANDOFF.md`, `CLAUDE.md`, `notebooks/requirements.txt`,
  the `build_capi/` rule in `.gitignore`, and the CI dedup scheme (RatioTap
  `f1e566a`, `94775b0`; rewritten SHAs are recorded in `ratio/docs/HISTORY.md`,
  step 1b).

GoogleTest: both repositories use the same pin (`f8d7d77`, v1.14.0) and the
same FetchContent name. A merged tree configures, and all 151 tests pass
(measured in a scratch tree).

### 3.4 The book

- **84** `{{#include}}` directives:
  - `include/srt` 42
  - `submodules/dsptap` 21
  - `tools/capi` 6
  - `tests` 4
  - `platform/` 7, `cmake/` 2, `tools/qemu_insn_plugin` 1
- **52 of them break at step 1**, not step 3, because step 1 moves their
  targets.
- The staleness check is inline in `ci.yml` (job `book`): `mdbook build`
  with a `warning|error` grep, plus an image-reference check.
  `book-pages.yml` repeats the build and runs `doxygen docs/Doxyfile`
  (`INPUT = include README.md`).
- The prose also carries **23 files** of `srt/…`, `tap::samplerate` and
  `SRT_*` identifiers that mdbook cannot check (GATE-16), and **5 links** to
  RatioTap as a live repository, including `git clone …/RatioTap` build
  commands (`part5/scaling.md:194,352-355`).

### 3.5 CI (corrected)

| Job | SampleRateTap | RatioTap |
|---|---|---|
| Host matrix | GCC, Clang, AppleClang, MSVC (MSVC `werror: OFF`) | same, **MSVC with `-DTAP_RATIO_WERROR=ON`** |
| Sanitizers | ASan+UBSan, TSan | **ASan+UBSan only**, WERROR ON |
| Linux arm64 native + TSan | `ci-arm64.yml`: **currently tests nothing** (`-R 'SpscRing'`; the suite is `spsc_ring`) | — |
| Hexagon / M55 / M33 correctness | yes. Hexagon `-E 'AsrcQuality\|AsrcLock\|TwoThreadStress\|TransparentPrototypeMeetsSpec\|MultiChannel\.\|Feasibility\|Reset\.\|ConfigValidation'`, serial | yes. Hexagon `-E 'BadProfilesThrow\|LatencyAndValidation'`, `-j 4` |
| `icount-ratchet` | 7 workloads × {m33, m55, hexagon}; README table drift check | 10 workloads × {m33, m55, hexagon} (RatioTap's CLAUDE.md still says eight) |
| bench-smoke, compare-smoke, clang-format job, book | yes | **no** (clang-format by pre-commit only) |
| Triggers / concurrency | push + PR, cancel-in-progress by ref | push to `main` + PR + dispatch (dedup scheme) |
| Actions pinning | by SHA | by tag |

Branch protection: `main` is unprotected in both repositories (checked
through the GitHub API; rulesets were not checked), so job renames break
nothing today.

### 3.6 C ABI (exact)

- **async:** 8 functions, `srt_{version,create,destroy,push,pull,status,designed_latency_seconds,reset_from_consumer}`,
  with opaque `SrtHandle`, library `libsrt_capi.so`. `srt_version`
  encodes `M*10000+m*100+p`.
- **ratio:** 11 functions, opaque `ratio_converter`, library
  `libratio_capi.so`. `ratio_version` encodes `(M<<16)|(m<<8)|p`, pinned by
  `test_skeleton.cpp`.
- **Separately:** the r8brain comparison shim exports `srt_r8b_oneshot` and
  `srt_r8b_latency_frames` from its own library.
- **Bridges:** RatioTap's notebooks use `notebooks/ratiotap_py.py`.
  SampleRateTap's use inline ctypes in 3 of 4 notebooks; `asrc_rbj_analysis`
  uses `scripts/book_figures.py`. Both bridges build only when the library
  is missing, so a stale library would be measured silently (GATE-10).

### 3.7 Outside references

- **DspTap:** 25 files, **comments and docs only**. No code, test, CMake or
  submodule dependency.
  - Most stay true after the merge, because the SampleRateTap name survives.
  - Only path citations (`include/srt/…`, `tests/support/`,
    `docs/PERFORMANCE.md`) and mentions of RatioTap as a live repository
    change.
  - These change through a DspTap PR after the merge. `STYLE.md` changes
    only through **taphouse**, because it is a drift-checked copy.
  - Close DspTap's open item on `TAP_DSP_CP_MIN_CHANNELS`
    (`docs/audit-fft-and-code-smells.md:392,458,668`) at the same time as
    `SRT_CP_MIN_CHANNELS`.
- **SampleRateTap:** RatioTap URLs in `README.md:407,432`,
  `book/src/part0/two-crystals.md:173`, `part5/scaling.md:194,352-355` and
  `docs/COMPARISON.md:234`. The README quotes stale cross-validation figures
  (−109/−99 dB; the current v0.3 floors are about −98/−90 dB).
- **Outside this session** (confirmed by the user, 2026-09-26):
  - **TapTools, TapTools-Max, MuTap, AmbiTap, OscTap** do not submodule or
    reference either repository's headers, targets or C ABIs. D7's "no
    consumers" premise holds.
  - **taphouse's `sync.sh` includes RatioTap.** It must be removed from the
    target list before RatioTap is archived (step 5), or syncs to the
    archive fail. `drift-check.yml` and the catalog README are updated in the
    same taphouse PR.
  - **"Create a merge commit" is allowed** on SampleRateTap (D3, step 4).
  - Still to check at step 5: open RatioTap issues and PRs, and the Pages
    source setting. The repository is private while its Pages site is
    public.

---

## 4. Target layout

```
SampleRateTap/
├── CMakeLists.txt          NEW root: project(SampleRateTap), enable_testing(),
│                           add_subdirectory(submodules/dsptap) ONCE, then engines
├── CLAUDE.md  PLAN.md  README.md   NEW family-level files (step 4)
├── LICENSE                 unified holder line (D14)
├── STYLE.md .clang-* .pre-commit-config.yaml .claude/ .github/  (shared)
├── .git-blame-ignore-revs  repaired (step P.1), extended after step 4
├── submodules/dsptap       the one pin
├── cmake/  platform/  tools/qemu_insn_plugin/   shared embedded harness
├── scripts/                icount.py (engine-aware), tidy.sh,
│                           fetch_hexagon_toolchain.sh (RatioTap's)
├── book/                   one book; ratio chapters are follow-up work
├── docs/                   family docs: PLAN (this file), Doxyfile (both engines)
├── async/
│   ├── CMakeLists.txt  README.md  PLAN.md
│   ├── include/tap/sr/async/   tests/   bench/ (icount, compare)   examples/
│   ├── capi/   notebooks/   docs/ (PERFORMANCE, COMPARISON, HARDWARE_TESTING)
│   └── tools/compare_shim/  cmake/r8brain.cmake
└── ratio/
    ├── CMakeLists.txt  README.md  PLAN.md  HANDOFF.md  CLAUDE.md
    ├── include/tap/sr/ratio/   tests/ (+reference/)   bench/   examples/
    ├── capi/   notebooks/ (+requirements.txt)   tools/reference/   docs/HISTORY.md
```

### 4.1 File disposition

| Path today | Destination | Step |
|---|---|---|
| SRT `CMakeLists.txt`, `README.md` | `async/CMakeLists.txt`, `async/README.md` (pure move; new root files in a *later* commit, see GIT-5) | step 1a |
| SRT `include/ tests/ bench/ examples/ notebooks/` | `async/…` | step 1a |
| SRT `tools/capi/`, `tools/compare_shim/` | `async/capi/`, `async/tools/compare_shim/` | step 1a |
| SRT `cmake/r8brain.cmake` | `async/cmake/r8brain.cmake` (async-only) | step 1a |
| SRT `docs/{PERFORMANCE,COMPARISON,HARDWARE_TESTING}.md` | `async/docs/` | step 1a |
| SRT `docs/Doxyfile`, `docs/MONOREPO_PLAN.md` | **stay** in root `docs/` | — |
| SRT `book/`, `scripts/`, `cmake/arm-*`, `cmake/hexagon-*`, `platform/`, `tools/qemu_insn_plugin/`, dotfiles, `LICENSE`, `STYLE.md`, `.github/` | **stay** at root | — |
| RatioTap, whole tree | `ratio/…` via filter-repo | step 1b |
| `ratio/.gitmodules`, `ratio/submodules/*` | rewritten into the root `.gitmodules` throughout history, then removed at the merge | step 1b |
| `ratio/.clang-*`, `STYLE.md`, `.pre-commit-config.yaml`, `.claude/`, `scripts/tidy.sh`, `.github/pull_request_template.md` | deleted (identical to root) | step 1b merge commit |
| `ratio/.github/workflows/ci.yml` | ported into root `.github/workflows/ci.yml` as ratio jobs | step 1c |
| `ratio/.github/workflows/style.yml` | its body replaces root `style.yml` | step 1c |
| `ratio/scripts/fetch_hexagon_toolchain.sh` | root `scripts/` | step 1c |
| `ratio/.gitignore` | `build_capi/` rule merged into root `.gitignore`; file deleted | step 1c |
| `ratio/tools/capi/` | `ratio/capi/` | step 3 |
| `ratio/cmake/`, `ratio/platform/`, `ratio/tools/qemu_insn_plugin/`, `ratio/scripts/icount.py` | deleted (root copies) | step 2 |
| `ratio/CLAUDE.md` | kept, build commands corrected at step 1c; reduced to the ratio charter at step 4 | step 1c, step 4 |
| `ratio/LICENSE` | deleted in the same commit that puts D14's unified line in the root `LICENSE` | step 1c |
| `ratio/notebooks/requirements.txt` | superseded by the root lockfile | step P.3 |
| `docs/migration/` (snapshots) | created at root `docs/migration/` (not moved, since `docs/` stays) and deleted at the end | step 0, step 4 |

### 4.2 Dependency rule and its enforcement

- `async` and `ratio` each depend on `tap::dsp` only.
- An engine may depend on another **only** in `tests/` and `examples/`:
  `ratio/tests` uses `tap::sr::async` for the cross-validation, and the
  `bluetooth_bridge` example composes both.
- `capi/` is per-engine (D8) and never links a sibling.

Enforcement, since v1's "enforced by CMake" named no mechanism (DEC-13):

1. **Configure-time assertion:** `INTERFACE_LINK_LIBRARIES` of each engine
   target is exactly `tap::dsp`.
2. **Install-tree isolation test:** install each engine alone, together with
   dsptap, into a staging prefix. Then compile every one of its public
   headers in its own TU from that prefix. Sibling source paths do not
   exist there.
3. **Grep gate on `*/include/**`:** rejects `srt/`, `tap/sr/<other>/`,
   `../` and `__has_include` of a sibling.
4. **Header glob with pinned count:** a header added outside the list fails
   the test.

---

## 5. Gates (defined once; the steps reference them)

Every gate is measured **against the step-0 snapshot** and not against
committed files. This separates migration effects from toolchain drift on
unpinned `ubuntu-latest` apt packages (GATE-3). Where a gate compares
builds, **step 0's SHA and step N's SHA are built in the same job, with the
same toolchain and the same binary paths**.

| ID | Gate | Catches |
|---|---|---|
| G1 | **Test multiset:** `ctest --show-only=json-v1` → multiset of (engine label, test name), equal to the snapshot. Every ctest call carries `--no-tests=error`, and each engine's tests carry a `LABELS` value | Dropped tests, including the duplicate `FixedPoint.FullScaleSineDoesNotWrapQ15` that exists in both engines (GATE-7, INF-12, GIT-12, INV-2) |
| G2 | **On-target test multiset:** the multiset of `[ RUN      ] Suite.Name` lines from each QEMU leg's gtest log (M33, M55, Hexagon), equal to the snapshot | M33/M55 register one ctest per binary, and the floors (15/25) hide losses of up to 5/17 tests (GATE-5). Also the Hexagon exclusion regexes (GATE-8) |
| G3 | **Exact icount:** `icount.py --exact` means integer equality, and the **set** of measured workloads equals the set of baseline keys. A missing baselines file is fatal. `--update` is forbidden during the migration | v1's "0 %" was undefined, since the default tolerance is ±3 % and the output rounds to `+0.00%` (GATE-1). Missing workloads passed silently (GATE-2) |
| G4 | **Codegen identity:** `objdump -d --no-show-raw-insn` of every icount and test binary, addresses stripped (plus a symbol map for renames), equal to step 0 | Stronger than icount and independent of QEMU |
| G5 | **Output identity:** new per-engine host tests (added at step P.2) FNV-hash the **full** output of every direction × format × profile workload and pin the hash. QEMU `checksum=` lines equal the snapshot exactly | icount is data-independent, so a changed coefficient passes G3. The existing checksum is weak and is never read (GATE-4) |
| G6 | **Cross-validation lines:** the printed `[ measured ] cross-validation …` lines are byte-identical to the snapshot | Loosened tolerances (DEC-11) |
| G7 | **Compile flags:** per-TU flags in `compile_commands.json` equal to the snapshot, modulo path prefixes | E.g. `-std=gnu++20` → `-std=c++20` flips GCC's `-ffp-contract`, which changes FMA on M33/M55 while the host tests stay green (GATE-15) |
| G8 | **Book:** `mdbook build` clean, plus the image check | Broken anchors |
| G9 | **Retired-identifier grep:** zero hits for the retired names (`srt/`, `tap::samplerate`, `tap/ratio`, `tap::ratio`, `SRT_`, `TAP_RATIO_`, `srt_`, `ratio_capi`, …) outside an allowlist (history docs, `HISTORY.md`, the plan) | Stale prose and code that mdbook and the compiler cannot see (GATE-16). Applies from step 3 commit 8 |
| G10 | **C ABI symbols:** `nm -D` of each capi library equals the snapshot under the committed name map | The step-0 ABI snapshot was otherwise never used (GATE-17) |
| G11 | **Notebooks:** re-executed in the pinned environment (step P.3) from a fresh clone, with bridges that always rebuild. Compared through a normalizer that keeps text outputs only and drops timing lines, PNGs and paths | Wall-clock cells, unpinned numpy/scipy and stale-library loading made "identical outputs" unattainable or vacuous (GATE-10) |
| G12 | **History:** `git log --follow` and `git blame` spot checks on a fixed file list from both engines. Blame of a moved file does **not** attribute all lines to a migration commit | Lost blame when a move and a recreate share a commit (GIT-5) |
| G13 | **Every CI job ran:** each job in the workflow reports on the gated SHA (run IDs recorded in `docs/migration/runs.md`). None is skipped or cancelled | Cancel-in-progress and trigger filters that test only tips (GATE-11, INF-9) |

A step's gate is a subset of G1–G13, listed with the step.

---

## 6. Migration

All work happens on `claude/sample-rate-expansion-strategies-ezqzu6` in
SampleRateTap, in **fresh full clones**. It proceeds **one commit per
push**, with a **draft PR open from the start** so every push runs CI
(G13). Nothing reaches `main` until step 4's PR. RatioTap is not written
to until step 5.

### Step P — Pre-work: harden both repositories *before* the snapshot

The snapshot must not record existing breakage as "green" (GATE-6, INV-8).
Each item below is a normal PR to the affected repository, merged before
step 0.

- **P.1 Repair existing breakage:**
  - `ci-arm64.yml:61`: fix `-R 'SpscRing'` → `spsc_ring`.
  - Pico 2 examples: add the dsptap include path (broken since `5315689`),
    and build them in CI once.
  - `scripts/book_figures_trace.cpp`: update it to the current API, or
    retire its "before" panel as a committed image.
  - `.git-blame-ignore-revs`: replace the dangling `34bb89e…` with
    `b84020e738f771c7689ffc1e592f8448b9ce063f` and `e2f5a48`.
  - README cross-validation figures: update to the current floors.
- **P.2 Harden the harness,** in both repositories, with identical changes:
  - `icount.py --exact`, plus the workload-set equality and the fatal
    missing-baselines check (G3).
  - Run Hexagon under a **fixed `argv[0]` and an empty environment**
    (`qemu-hexagon -0 w` with `env -i`). Static musl's startup walks
    `argv[0]` and `envp`, so the binary path and job environment otherwise
    enter the count (GATE-1). **Re-record the Hexagon baselines once**,
    with the delta stated.
  - `--no-tests=error` on every ctest call.
  - Per-engine `LABELS` on the tests.
  - The G5 full-output hash tests.
- **P.3 Pin a notebook environment:**
  - Add a lockfile with numpy, scipy, matplotlib, jupyter, samplerate and
    soxr.
  - Make the bridges always rebuild.
  - Re-execute every notebook in that environment and confirm its text
    outputs equal the committed ones before they become the baseline.
  - Mark `asrc_rbj_analysis` cell 18 (wall-clock timings) as excluded.

### Step 0 — Freeze and snapshot

- Record the tips: SampleRateTap `main` and RatioTap `main` after step P.
- Assert that the full clones are not shallow
  (`git rev-parse --is-shallow-repository` = `false`) and that the RatioTap
  tip is the expected SHA.
- Take G1–G11 snapshots and commit them under root `docs/migration/`.
- No merges to either `main` until step 4. If one is unavoidable, it lands in
  SampleRateTap only and step 1 is re-cut. The filter-repo output is
  deterministic, so a re-cut is cheap.

### Step 1 — Import (three commits)

**1a — Pure move** (one commit; `git show -M --stat` shows zero
insertions and deletions):

- `git mv` SampleRateTap's engine files per section 4.1, including
  `CMakeLists.txt` → `async/CMakeLists.txt` and `README.md` →
  `async/README.md`.
- Nothing else in this commit, so blame survives (GIT-5).

**1b — Import RatioTap** (one merge commit):

```sh
git clone https://github.com/tap/RatioTap rt && cd rt
test "$(git rev-parse main)" = "<RatioTap tip from step 0>"
git filter-repo --path-rename :ratio/ --path-rename ratio/.gitmodules:.gitmodules \
  --blob-callback '
if blob.data.startswith(b"[submodule \"submodules/"):
    blob.data = (blob.data
        .replace(b"path = submodules/", b"path = ratio/submodules/")
        .replace(b"[submodule \"submodules/", b"[submodule \"ratio/submodules/"))'
cd ../SampleRateTap
git fetch ../rt main:ratio-import
git merge --allow-unrelated-histories --no-commit ratio-import
git checkout --ours .gitmodules            # the one expected conflict (add/add)
git rm -r --cached ratio/submodules        # the engine uses the root dsptap pin
git rm -f <ratio duplicates per 4.1>       # -f: they are staged by the merge
git commit                                 # message records the RatioTap tip SHA
```

This command was verified in a dry run. The rewritten history keeps
`.gitmodules` at the root with rewritten paths, so every imported RatioTap
commit can still initialize its submodules. The naive
`--to-subdirectory-filter` recipe orphans the gitlinks and breaks
`git submodule update` everywhere (GIT-2).

Also:

- Generate `ratio/docs/HISTORY.md` from `.git/filter-repo/commit-map`: old
  SHA → new SHA → `https://github.com/tap/RatioTap/pull/N` for all 29
  commits. This preserves PR linkage lost to the squash merges and the PR
  number collisions (GIT-8).
- Optionally, use `--message-callback` to rewrite bare `DspTap #38` to
  `tap/DspTap#38`.
- If tags exist at cut time, add `--tag-rename '':'ratio/'`.

**1c — Build glue and path fix-ups.** No codegen-reaching changes. G4
and G7 prove that.

- **Root `CMakeLists.txt`:**
  - `project(SampleRateTap)` and `enable_testing()`.
  - `add_subdirectory(submodules/dsptap)` once.
  - Force the engines' `*_BUILD_TESTS`/`*_EXAMPLES` ON when the root is top
    level. Their `PROJECT_IS_TOP_LEVEL` defaults evaluate false under a root
    project, and v1's glue ran **0 tests while ctest exited 0** (INF-3).
  - `add_subdirectory(async)` and `add_subdirectory(ratio)`.
- **Engine CMakeLists:**
  - Wrap `add_subdirectory(submodules/dsptap)` in `if(NOT TARGET tap::dsp)`.
    This also keeps standalone engine builds working.
  - Ratio's `srt_headers` → `async/include` (kept `SYSTEM`).
  - `${PROJECT_SOURCE_DIR}/cmake/r8brain.cmake` → `async/cmake/`.
  - Standalone capi entry points fixed (ratio's `add_subdirectory(../..)`).
- **Bare-metal variables:** the root toolchain files set **both**
  `SRT_BARE_METAL` and `TAP_RATIO_BARE_METAL` until step 3 unifies them.
  The `GTEST_HAS_*` definitions move to root scope under that condition,
  so the gtest library and both engines' test TUs agree (INF-1).
- **CI** (INF-4, GIT-4):
  - Port RatioTap's `ci.yml` jobs into the root workflow as ratio jobs, with
    ratio's options, its Hexagon `-E` list and `-j 4`, ratio's MSVC and
    sanitizer WERROR, and ratio's icount against `ratio/bench/baselines.json`.
  - Replace root `style.yml` with RatioTap's body, configuring **both**
    engines with tests and icount ON, and failing on an empty TU list.
  - Adopt `scripts/fetch_hexagon_toolchain.sh` for every cache writer.
  - Move the RatioTap-only files per 4.1.
- **Paths:**
  - The book's 52 includes (G8).
  - `book-pages.yml` path filters and Doxyfile `INPUT`.
  - The `bench-smoke` binary path, `compare.yml` build paths,
    `icount.py --baselines` per engine, `update_icount_docs.py` →
    `async/bench/baselines.json` and `async/README.md` (Q8). ratio gains the
    same table in `ratio/README.md`, and the script takes `--engine`.
  - Notebook `REPO` roots and `sys.path` entries.
  - The 11 README relative links.
  - `ratio/CLAUDE.md` build commands.
- **LICENSE:** in one commit, the root `LICENSE` takes D14's unified line
  and `ratio/LICENSE` is deleted. The notice MIT requires is never absent
  (GIT-13).

**Gate 1:**

- G1, G2, G3 (both engines, all targets), G4, G5, G6, G7, G8, G11, G12,
  G13.
- Every notebook bridge builds standalone.

### Step 2 — Shared infrastructure

- Delete ratio's copies of `cmake/`, `platform/`, `tools/qemu_insn_plugin/`
  and `scripts/icount.py` (all cosmetic after step 1c).
- `icount.py` becomes engine-aware: `--engine async|ratio` sets its
  `<engine>_icount_*` glob, baselines path and marker regex.
  - **Guest-printed strings do not change.** `SRT_ICOUNT_DONE` and
    `RATIO_ICOUNT_DONE` stay byte-identical, because guest `printf` scans
    format text per character and a longer marker moves the count (GATE-1,
    INF-7).
  - The host-side plugin marker becomes `TAP_SR_INSN_COUNT`, which is
    harmless.
- **Ratchet CI:** one job **per target** measures both engines. This shares
  the toolchain, the plugin build and the qemu-hexagon cache, and uses
  `fail-fast: false` so each target stays independent evidence (INF-8).
  - Build the plugin once, as an artifact.
  - Docs freshness runs as its own async-only job.
  - `compare.yml` is updated in the same commit, and runs once by
    `workflow_dispatch` as part of this gate.
- CI dedup: adopt RatioTap's scheme with `workflow_dispatch` and
  `cancel-in-progress: ${{ github.ref != 'refs/heads/main' }}`. Actions are
  SHA-pinned (SampleRateTap's pins).

**Gate 2:** G1–G7 and G13, plus one manual `compare.yml` run.

### Step 3 — Renames (one commit per class, each gated)

1. **Paths:** `srt/…` → `tap/sr/async/…`; `tap/ratio/…` → `tap/sr/ratio/…`;
   `ratio/tools/capi` → `ratio/capi`. Delete `srt/detail/kaiser.h` per 3.1.
2. **Namespaces:** `tap::samplerate` → `tap::sr::async`; `tap::ratio` →
   `tap::sr::ratio`; test namespaces `srt_test` / `ratio_ref` as decided.
   `async_sample_rate_converter` → `converter` (D15).
3. **clang-format reflow:** its own commit. The namespace rename reflows
   22 files (+112/−114) through alignment columns (GIT-6).
4. **Macros:**
   - `SRT_VERSION_*`, `TAP_RATIO_VERSION_*` → `TAP_SR_VERSION_*`, set to
     0.4.0 (D13).
   - `SRT_RESTRICT`, `SRT_Q15_SMLALD`, `SRT_CHANNEL_PARALLEL`,
     `TAP_RATIO_MIRRORED_DOT_ATTR`: renamed, or replaced by their
     `TAP_DSP_*` originals where they are pure aliases.
   - `SRT_CP_MIN_CHANNELS` (a user override, documented in the book):
     renamed with an `#error` tripwire (D7).
   - `SRT_SC_*`, `RATIO_SC_*`, `SRT_CMP_*`, `*_TESTS_COMPLETE`,
     `*_BARE_METAL` (unified to `TAP_SR_BARE_METAL`), `SRT_PICO2_*`.
   - **Guest-printed icount markers stay unchanged** (step 2).
5. **CMake:**
   - Targets `tap::sr::async`, `tap::sr::ratio` and umbrella `tap::sr`.
   - Internal targets (`srt_warnings`, `srt_tests`, `srt_bench*`,
     `srt_alsa_bridge`, `srt_r8brain`, `srt_r8b_shim`, `tap_ratio*`,
     `srt_headers`, …).
   - Options → `TAP_SR_*` per D9. Old public targets removed (D7).
6. **C ABI:**
   - `srt_*` → `tap_sr_async_*`, `ratio_*` → `tap_sr_ratio_*`.
   - Handle types, header names and library names.
   - The shim's exports.
   - `srt_version` and `ratio_version` → one `tap_sr_version()`, bit-packed
     (D13); `test_skeleton.cpp` re-pins it at 0.4.0. Bridges renamed per D8.
7. **Ratchet workload binaries:** prefix only (`tap_sr_<engine>_icount_*`).
   Workload names do not change, because the key is the basename minus the
   prefix (GATE-12, INV-5).
8. **Book and docs prose:** the 23 files and the RatioTap URLs, including
   rewriting the `git clone …/RatioTap` instructions.

**Gate for each commit:** G1 (the name-map table must be **empty** unless
a suite rename is listed explicitly; GATE-17), G2, G3, G4 (with the symbol
map), G5, G6, G7, G8, G10, G11 and G13. G9 applies from commit 8.

After the PR merges (step 4), append the SHAs of commits 1–3 to
`.git-blame-ignore-revs`, together with the rewritten RatioTap reformat
commit (`c0894cf`, looked up in the commit map).

### Step 4 — Documentation and the PR

- **Root files:**
  - `PLAN.md` promoted from this document.
  - Family `CLAUDE.md`, which carries the dependency rule, D12, **`git bisect
    start --first-parent`** (GIT-9), and the cross-validation separation
    rule (section 8, R4).
  - Family `README.md`.
- **Per-engine files:** `PLAN.md` and `README.md`. `ratio/CLAUDE.md` is
  reduced to the ratio charter.
- **Delete `docs/migration/`,** keeping `runs.md` as history if wanted.
- **Mark the PR ready.** It is merged with **"Create a merge commit"**
  (enable it in repository settings if disabled). **Never squash or
  rebase** (D3, GIT-1).
- **Post-merge gate:**
  - `git rev-list --count origin/main` ≥ 136 + 29 + N.
  - `git log --follow ratio/include/tap/sr/ratio/converter.h` reaches
    RatioTap's M7d commit.

### Step 5 — Outside the repository

- A DspTap PR for the comment and doc references (3.7).
- A taphouse PR for `STYLE.md`'s macro example and `sync.sh`.
- The user archives RatioTap with a README pointer and deletes its leftover
  branch.
- The user completes the outside-consumer checks in 3.7 before the RatioTap
  archive.

### Next — First new engine: `integer` (separate plan)

`integer` is out of scope and gets its own reviewed PLAN.md. That plan
covers:

- L-th-band design math in DspTap.
- `chain<>` in DspTap under D12.
- The `integer` engine.
- The generated 14 × 14 coverage matrix test (2.1).

`ratio`'s 2^k charter restatement (2.2) is a small separate change,
sequenced before it.

---

## 7. Non-goals

- No algorithm, coefficient, design or process-loop change. Outputs stay
  bit-identical (G5). Codegen stays identical (G4), and so do instruction
  counts (G3), except the one Hexagon re-record in step P.2, which is a
  harness change made before the snapshot.
- No new engine, profile or API function.
- DspTap **code** is untouched. DspTap docs and comments change only
  through a DspTap PR (step 5), and `STYLE.md` only through taphouse.
- No release is cut during the migration. 0.4.0 (D13) is set in step 3 and
  tagged `v0.4.0` after step 4 merges.
- The third harness copy (DspTap's) is not adopted (3.3).

## 8. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | A step silently drops a test or workload | G1 and G2 compare multisets. G3 requires set equality of workloads. `--no-tests=error` everywhere |
| R2 | Infrastructure deduplication or build glue changes codegen | G4 (disassembly), G7 (flags) and G3 (exact), all as same-job A/B against step 0 |
| R3 | Book and docs rot | G8 from step 1; G9 from step 3 commit 8 |
| R4 | The cross-validation loses its independence | Independence comes from leg 2 (scipy vectors) and from the structural difference between the engines, never from repository separation. Both already share DspTap. What the merge removes is the pin bump as a separately reviewed event. Replacements: G6 during the migration, and a **permanent family rule** in CLAUDE.md. A PR that changes cross-validation tolerances must leave `ratio/tests/reference/` untouched, must keep the scipy leg green, and must not also change async's datapath (`polyphase_filter.h`, `sample_traits.h`). `ratio` never includes async's bank or blend (D11) |
| R5 | Rewritten SHAs and colliding PR numbers | `ratio/docs/HISTORY.md` (step 1b). Rewriting `DspTap #38` in messages is optional |
| R6 | CI load: about 21 jobs, which exceeds the 20-concurrent limit of GitHub Free, and macOS minutes cost 10× | The ratchet runs per target rather than engine × target (step 2). Raise the M33/M55 correctness timeouts, or split them per engine if two one-shot suites exceed 30 min |
| R7 | The merge interleaves histories for `git bisect` | `git bisect start --first-parent` is required and documented. Pre-merge ratio regressions are bisected in-tree (possible thanks to the `.gitmodules` rewrite) or in the archive |
| R8 | Charter boundaries erode | The four-part enforcement in 4.2, D11, D12 and per-engine PLAN.md files |
| R9 | The final PR is squash-merged | D3; the merge-commit instruction in step 4; the post-merge history gate |
| R10 | Toolchain drift mid-migration is blamed on a step | All gates are same-job A/B against the step-0 SHA (section 5) |

## 9. Open questions

None. All were resolved; the decisions are recorded where they apply.

| Q | Resolution |
|---|---|
| Q1 | `HANDOFF.md` stays in `ratio/` (v2) |
| Q2 | Rename to `converter`: D15 (user, 2026-09-26) |
| Q3 | `ratio` at 2^k rates: section 2.2 (v2) |
| Q4 | One family version, 0.4.0, bit-packed encoding: D13 (user) |
| Q5 | One unified holder line: D14 (user) |
| Q6 | Per-engine READMEs plus a family README (v2) |
| Q7 | Two-level namespace accepted: D4 (user) |
| Q8 | Icount tables in each engine's README: step 1c (user) |

---

## Appendix A — Audit disposition

Reviewers: **DEC** (decisions and charters), **GIT** (history mechanics,
dry-run on scratch clones), **GATE** (gates and bit-identity, host builds),
**INF** (infrastructure and CI, simulated merged tree), **INV** (inventory
completeness). Severity: B blocker, M major, m minor, n nit.

**Rejected:** no finding was rejected outright.

**Partly deferred:**

- DEC-9's harness-from-DspTap is deferred (3.3).
- DEC-10's `std::` shadowing is noted, not acted on (D5).
- INF-14's per-engine timeout split is conditional (R6).

| ID | Sev | Finding (short) | Disposition |
|---|---|---|---|
| DEC-1 | M | Q3 premise wrong: `ratio` is rate-normalized | 2.2; Q3 resolved |
| DEC-2 | M | Invariant forbids more pairs than listed | 2.1: generated full matrix |
| DEC-3 | M | Nyquist criterion wrong | 2.1: passband rule |
| DEC-4 | m | "Every standard rate" false (37.8, 50.4, pull-down) | 2.1 exclusions |
| DEC-5 | n | Table rows; 2/3 needs a rational stage | 2.1 rows; `integer` charter L/M |
| DEC-6 | M | `fractional_resampler` to DspTap contradicts settled M0 | D11 |
| DEC-7 | M | Moving `decimate.h` breaks MuTap / D10 | 2; the `integer` plan (section 6, Next) |
| DEC-8 | M | `chain<>` risks a rate-routing factory | D12 |
| DEC-9 | m | D1 supersession unrecorded; third harness copy | D1; 3.3 |
| DEC-10 | n | D4 supersedes agreed rename; `std::` shadowing | D4, D5 |
| DEC-11 | M | Independence misattributed; R4 guards the wrong thing | R4; G6 |
| DEC-12 | M | Per-engine enables can drop leg 3 | D9 |
| DEC-13 | M | Header-isolation bypassable | 4.2 |
| DEC-14 | m | One shared capi links both engines | D8 |
| DEC-15 | M | Step-1 CMake glue drops ratio tests | step 1c |
| DEC-16 | n | Stale pin checkout; stale README figures | 3.2; step P.1 |
| GIT-1 | B | Squash merge erases imported history | D3; step 4; R9 |
| GIT-2 | M | Orphan submodule gitlinks under subdir filter | step 1b corrected recipe |
| GIT-3 | M | Inventory from a shallow clone | D2; 3; step 0 |
| GIT-4 | M | Step 1 not content-free | step 1c |
| GIT-5 | M | Move+recreate in one commit loses blame | step 1a; G12 |
| GIT-6 | M | Namespace rename reflow needs own commit + ignore-revs | step 3.3 |
| GIT-7 | m | `.git-blame-ignore-revs` dangling | step P.1 |
| GIT-8 | M | PR linkage lost; `#NN` collisions | step 1b HISTORY.md; R5 |
| GIT-9 | M | Bisect needs `--first-parent` | step 4; R7 |
| GIT-10 | m | Import branch; stale local refs | 3; step 0 |
| GIT-11 | m | Tags | 3; step 1b |
| GIT-12 | m | Duplicate ctest names | G1 |
| GIT-13 | m | Deleting ratio LICENSE drops notice | D14; 4.1; step 1c |
| GIT-14 | n | Plan swept into `async/docs` | 4.1 |
| GATE-1 | B | 0 % icount unattainable (marker, Hexagon argv/env) | G3; step P.2; step 2 |
| GATE-2 | M | icount.py passes on missing workloads | G3; step P.2 |
| GATE-3 | M | Toolchain drift vs committed baselines | Section 5 A/B; G4 |
| GATE-4 | M | icount ≠ bit-identity | G5; step P.2 |
| GATE-5 | M | Bare-metal: one ctest name, loose floors | G2 |
| GATE-6 | M | arm64 TSan runs zero tests | step P.1; `--no-tests=error` |
| GATE-7 | M | Duplicate test name defeats set compare | G1 |
| GATE-8 | M | Hexagon exclusion lists unguarded | G2; step 1c |
| GATE-9 | M | Book/notebooks/docs break at step 1 | step 1c; G8, G11 |
| GATE-10 | M | Notebook gate unattainable | step P.3; G11 |
| GATE-11 | M | CI does not gate each commit | Section 6 preamble; G13 |
| GATE-12 | m | v1 step 3.6 contradicts key derivation | step 3.7 |
| GATE-13 | m | BARE_METAL variable functional | 3.3; step 1c; step 3.4 |
| GATE-14 | m | Override macro silently ignored | D7; step 3.4 |
| GATE-15 | m | Glue may change compile flags | G7 |
| GATE-16 | m | Stale prose unchecked | G9 |
| GATE-17 | n | Mapping-table loophole; ABI snapshot unused | step 3 gate; G10 |
| INF-1 | M | BARE_METAL coupling; gtest ODR mismatch | step 1c; step 3.4 |
| INF-2 | B | Merged tree does not configure (dsptap twice) | step 1c |
| INF-3 | B | 0 tests, ctest exits 0 | step 1c; G1 |
| INF-4 | B | ratio has no CI between steps 1 and 2 | step 1c |
| INF-5 | M | Book/Doxyfile/book-pages break at step 1 | step 1c; G8 |
| INF-6 | M | Hard-coded paths in jobs and compare.yml | step 1c; step 2 |
| INF-7 | M | Marker rename moves guest count | step 2 |
| INF-8 | M | Matrix fail-fast; duplicated setup | step 2 |
| INF-9 | M | Dedup scheme stops branch CI | Section 6 preamble; step 2 |
| INF-10 | M | style.yml functional; tidy would lint 0 TUs | 3.3; step 1c |
| INF-11 | M | CI table wrong; lost WERROR/Hexagon gates | 3.5; D9; step 1c |
| INF-12 | m | Duplicate test name | G1 |
| INF-13 | m | Toolchain fetch hardening | 3.3; step 1c; step 2 |
| INF-14 | m | Job count vs concurrency; timeouts | R6 |
| INF-15 | n | Stale branch/pin facts; cancel-in-progress on main | 3; step 2 |
| INV-1 | B | Step 1 cannot build/test without CMake edits | step 1c |
| INV-2 | M | Duplicate ctest name | G1 |
| INV-3 | M | Root-level consumers break at step 1 | step 1c |
| INV-4 | M | "Cosmetic only" false | 3.3 |
| INV-5 | M | Marker rename breaks 0 % gate; key contradiction | step 2; step 3.7 |
| INV-6 | M | Rename list missing classes | step 3.4–step 3.6 |
| INV-7 | M | kaiser.h deletion more than an include swap | 3.1 |
| INV-8 | M | Existing breakage invisible | step P.1 |
| INV-9 | M | Unaccounted files | 4.1 |
| INV-10 | m | 3.4 conclusion wrong | 3.4 |
| INV-11 | m | 3.5 table errors | 3.5 |
| INV-12 | m | C ABI counts exact; version encodings | 3.6; D13 |
| INV-13 | m | DspTap list 10 of 25; non-goal conflict; STYLE via taphouse | 3.7; 7; step 5 |
| INV-14 | m | RatioTap URLs dangle | 3.7; step 3.8 |
| INV-15 | n | 3.2 / R4 stale | 3.2; R4 |
| INV-16 | m | Outside consumers unchecked | 3.7 (confirmed by user) |
