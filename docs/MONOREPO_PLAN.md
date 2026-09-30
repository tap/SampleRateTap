# Monorepo plan: the `tap::sr` sample-rate family

Status: **DRAFT v3.1. Two adversarial audit rounds folded in; step P executed, steps 0–5 not started.**

| Version | Commit | What changed |
|---|---|---|
| v1 | `11f2a94` | First draft |
| v2 | `4884b01` | Round 1: 5 reviewers, 78 findings (Appendix A) |
| v2.1 | `c78ab93` | User decisions D13–D15; outside-repository facts confirmed |
| v3 | `b34681c` | Round 2: 4 reviewers, including an end-to-end dry run of steps 1a–1c and gate prototypes; 78 findings (Appendix B). The user reconfirmed D14 and chose banners everywhere |
| v3.1 | this commit | Engine names (D5, user decision 2026-09-27): RatioTap's engine becomes **`bridge`** and the future 2^a·3^b engine **`rational`**, replacing `ratio` and `integer`. The Python ctypes modules are now called **bindings**, leaving "bridge" to the engine. Step 0's G2 parser note |

When this plan is approved, it becomes the family-level `PLAN.md`. The
audit's draft workflow and CMake files were replaced at step 1c by the real
ones (root `CMakeLists.txt`, `.github/workflows/`), and the gates they
sketched are implemented in `docs/migration/gates.py`.

How to read it:

- **Section 1:** decisions.
- **Section 2:** family, charters and coverage rules.
- **Section 3:** measured inventory.
- **Section 4:** layout, file disposition and dependency enforcement.
- **Section 5:** gates.
- **Section 6:** the migration: pre-work step P, then steps 0–5, then what
  comes next.
- **Sections 7–9:** non-goals, risks and open questions.
- **Appendices A and B:** audit disposition.

---

## 1. Decisions

| # | Decision | Rationale / supersession |
|---|---|---|
| D1 | **Merge SampleRateTap and RatioTap into one repository.** Each engine keeps its own charter, CMake target, CI coverage and ratchet baselines | The engines are built to be composed and cross-checked. **This supersedes** HANDOFF.md preamble item 1 ("separate repo") and RatioTap PLAN.md §2 (SampleRateTap as a test-only dependency). Those decisions predate a five-engine roadmap: every added engine multiplies pins, harness copies and cross-repository test dependencies. Nothing outside these two repositories consumes them (confirmed, 3.7) |
| D2 | **SampleRateTap is the host repository** and keeps its name | "Sample rate" names the family once it is namespaced. The Pages book keeps its URL. It also has the larger history: 136 commits on `main` in a full clone, against RatioTap's 29 at the time of writing (step 0 re-counts both) |
| D3 | **RatioTap's history is preserved.** `git filter-repo` rewrites the paths, and `.gitmodules` throughout history, on `main` only (step 1b). The rewritten history is joined by an unrelated-histories merge. **The final PR is merged with a merge commit, never squash or rebase** | `--follow`, `blame` and submodule checkout keep working for every imported commit. A squash would erase all imported commits (GIT-1). Merge commits are allowed on SampleRateTap (confirmed) |
| D4 | **Namespace `tap::sr::<engine>`**, include path `include/tap/sr/<engine>/` | The path mirrors the namespace, as in DspTap. **This supersedes** RatioTap PLAN.md's agreed `include/tap/samplerate/`. The two-level form is **accepted** (user, 2026-09-26). The convention note goes to taphouse's `STYLE.md` (step 5) |
| D5 | **Engines:** `async` (was SampleRateTap), `bridge` (was RatioTap); future `rational`, `pdm`, `varispeed` | `async` is the industry term and the family's clock-topology word. It is the only async engine, and async at other ratios comes from composition. The sync engines are named for what they convert: **`rational`** converts *within* a rate family (small-factor L/M, L, M ∈ {2^a·3^b}); **`bridge`** crosses *between* the 44.1 and 48 kHz families, the one large-factor ratio (147/160) and hand-optimized for it. Both are rational in the mathematical sense; the charters (section 2), not the names, draw the line. **User decision, 2026-09-27**, replacing v3's `ratio` (which read too close to `rational`) and `integer` (which read as the integer-sample Q15/Q31 profiles). `async` sits next to `std::async` only under namespace-wide `using`, which the house style avoids. The `bluetooth_bridge` example keeps its name: it composes `async` and `bridge` |
| D6 | **One top-level directory per engine**, each with `include/ tests/ bench/ examples/ capi/ notebooks/ README.md PLAN.md` | Keeps each charter's boundary physical |
| D7 | **Clean renames, no aliases.** Retired user-facing override macros get an `#error` **tripwire**. Retired **CMake options** get a `FATAL_ERROR` tripwire (step 3.4) | Aliases would be permanent debt. A tripwire turns a stale `-DSRT_CP_MIN_CHANNELS=…` or `-DSRT_WERROR=ON` into a loud failure instead of a silent no-op. An unknown `-D` otherwise only warns and drops a gate (R2-CI-6) |
| D8 | **C ABI prefix `tap_sr_<engine>_*`, one shared library per engine** (`tap_sr_async_capi`, `tap_sr_bridge_capi`), and one Python binding module per engine | No shipped artifact links two engines. The version function follows the rule as well: `tap_sr_async_version()` and `tap_sr_bridge_version()`, which return the same family value (D13) |
| D9 | **CMake options `TAP_SR_*`** after step 3.4: `BUILD_TESTS`, `BUILD_EXAMPLES`, `BUILD_CAPI` (builds both engine libraries), `BUILD_ICOUNT_BENCH`, `BUILD_BENCHMARKS` and `BUILD_COMPARE_BENCH` (async-only), plus per-engine `TAP_SR_<ENGINE>_WERROR`. **Every configure builds both engines. CI picks an engine only when running tests**, through ctest names and labels (step P.2) | Per-engine WERROR keeps bridge's MSVC `/WX` alongside async's untriaged `/W4`. The warning flags sit on separate INTERFACE targets, and the dry run showed they coexist. There are no per-engine enables, which could silently drop the cross-validation (DEC-12). Selecting tests at ctest time settles the D9 versus per-engine-job conflict (R2-CI-3, R2-COH-11) |
| D10 | **DspTap stays separate**, pinned once at `submodules/dsptap` | It has consumers outside the family: TapTools, and MuTap through the `LogMel`/`Decimator` C ABI |
| D11 | **Engine directory vs. DspTap:** a capability gets an engine directory when it has its own charter, campaign and ratchet. Building blocks go into DspTap. **Engine-owned datapaths stay with their engine.** `fractional_resampler`, the polyphase bank and the blend stratum belong to `async` | Consistent with RatioTap PLAN.md Appendix A. It also keeps `bridge`'s cross-validation oracle out of `bridge`'s reach |
| D12 | **No routing by rate, including through composition.** `chain<>` is a caller-named, compile-time chain of **synchronous** stages. There is no `(in_hz, out_hz)` lookup, `async` is never chained, and the coverage matrix documents chains without dispatching them | Keeps HANDOFF preamble item 4 |
| D13 | **One family version, 0.4.0**, with tags `vX.Y.Z` and bit-packed encoding `(M<<16)\|(m<<8)\|p`. **Mechanics (step 3):** root `project(SampleRateTap VERSION 0.4.0)`; engine subprojects renamed `tap_sr_async` / `tap_sr_bridge` with no VERSION; macros `TAP_SR_VERSION_{MAJOR,MINOR,PATCH}` defined **token-identically** in each engine's umbrella header, checked by a static_assert test (no shared header, so 4.2 check 1 holds); each C ABI library exports its own `tap_sr_<engine>_version()`; a new C ABI test `CApi.VersionIsBitPacked` pins the encoding, **which nothing pins today** (`test_skeleton.cpp` checks only MAJOR = 0) | User decision. 0.4.0 is above both current versions. The encoding is RatioTap's (`ratio_capi.cpp:97`) |
| D14 | **One copyright line family-wide:** `Copyright (c) 2026 Timothy Place and the SampleRateTap contributors` in the root `LICENSE`, and the same holder in a **banner on every C/C++/Python source file** | User decision, **reconfirmed**: the user holds SampleRateTap's copyright. SampleRateTap's notice today names only "SampleRateTap contributors"; RatioTap's names the user. So this **adds the author's name** to SampleRateTap's notice, which is accurate on the user's word, and restates RatioTap's. There are 35 banner lines today; about 25 of SampleRateTap's C/C++ files have none. Banners are added everywhere in step 3.8. `STYLE.md`'s banner template ("Copyright 2025-2026 Timothy Place.") is reconciled through taphouse (step 5) |
| D15 | **`async` renames its converter family to match `bridge`'s `basic_converter` family:** `basic_async_sample_rate_converter<S>` → `basic_converter<S>`, `async_sample_rate_converter` → `converter`, `…_q15`/`…_q31` → `converter_q15`/`converter_q31`, exception prefixes `"async_sample_rate_converter: "` → `"tap::sr::async::converter: "`, and `asrc.h` → `converter.h` | User decision. v2.1's rationale cited a `tap::sr::ratio::converter` that does not exist. The real parallel is RatioTap's (now `bridge`'s) `basic_converter<S, D>` with its `converter_to_48k` family. Scale: about 80 hits in 22 files, plus the book's naming-decision prose (R2-COH-19) |
| D16 | **Tests carry an engine prefix:** `gtest_discover_tests(… TEST_PREFIX "async." / "bridge.")`, plus a `LABELS` value of `async` / `bridge` on every test, including the bare-metal `*_tests_emulated` entries. Lands in step P.2, so the snapshot already has it, **with RatioTap's pre-rename spelling `ratio.` / `ratio`**; step 3.4 renames it to `bridge.` / `bridge`, and G1/G2 compare through `rename.py`'s name map | CTest applies a duplicate name's properties to both tests. `FixedPoint.FullScaleSineDoesNotWrapQ15` exists in both engines, so labels alias and `ctest -L ratio` selects async's copy (reproduced, R2-GATE-4). Unique names fix G1 and engine selection |

---

## 2. The family after the merge

| Engine | Namespace | Origin | Charter |
|---|---|---|---|
| `async` | `tap::sr::async` | SampleRateTap v0.1.0 | Asynchronous, near-unity (±`max_deviation_ppm`, default 1000 ppm): absorbs the clock |
| `bridge` | `tap::sr::bridge` | RatioTap v0.3.0 | Synchronous 160/147 pair, 44.1 ↔ 48 kHz: converts the number. **After the 2.2 follow-up:** 44.1·2^k ↔ 48·2^k, k ≤ 2 |
| `rational` | `tap::sr::rational` | new, separate plan | Synchronous rational L/M with L, M ∈ {2^a·3^b}. Nyquist (L-th band) stages. Does not absorb DspTap's `decimate.h` |
| `pdm` | `tap::sr::pdm` | new, when a consumer asks | 1-bit sigma-delta → PCM: CIC → compensation FIR → `rational` stages |
| `varispeed` | `tap::sr::varispeed` | new, when a consumer asks | Time-varying ratio (Smith, CCRMA bandlimited interpolation) |

Not engines:

- **Timestamp clock recovery** is an `async` feature.
- **Minimum-phase and IIR tiers** are profiles.
- **An offline FFT tier** is built only on request.
- **`decimate.h`** stays in DspTap, where MuTap consumes it.

### 2.1 Coverage rule (rewritten in v3)

v2's rules 1–3 contradicted v2's own 176.4 → 48 row, and had no stopband or
image condition (R2-COH-7). The replacement is stated per **chain**. The
chain declares a passband `f_pass`, a stopband attenuation `A` and a
passband ripple `δ`, and these are pinned per rate pair by the coverage
matrix test. For **every rate-changing stage** whose lower rate is `r`:

- **(a) Aliases and images stay out of the passband.** The stage's stopband
  edge is ≤ `r − f_pass`, so every alias (decimating) or image
  (interpolating) of passband content lands above `f_pass`.
- **(b) Attenuation.** The stage's stopband attenuation is ≥ `A`.
- **(c) Ripple.** The chain's passband ripple is the **sum** of its stages'
  ripples, and must be ≤ `δ`. Droop compounds, so this is stated as ripple,
  not as a design-parameter edge.
- **(d) What gets pinned.** The declared `(f_pass, A, δ)` belongs to the
  chain, not to any one engine's profile. The matrix test measures and pins
  it for each pair, next to MACs per output and latency.

Checks against the rows below:

- **176.4 → 48** (`rational` ↓4 → `bridge` ↑): `bridge`'s stopband edge,
  24 kHz, is ≤ 44.1 − `f_pass` for `f_pass` ≤ 20.1 kHz. The ↓4 stage needs a
  stopband ≤ 24.1 kHz at 20 kHz passband.
- **16 → 44.1** through ↑3 (lower rate 16): needs stopband ≤ 16 − `f_pass`
  at attenuation `A`. That rules out an L-th-band ↑3 stage whose transition
  band leaves images at 8–9 kHz, which v2's rules let through.

**Supported rates:** exactly these 14: 8, 11.025, 12, 16, 22.05, 24, 32,
44.1, 48, 88.2, 96, 176.4, 192 and 384 kHz.

**Excluded, with reasons:**

- **352.8 kHz (DXD):** `bridge`'s k stops at 2 and no consumer has asked.
- **37.8 and 50.4 kHz:** ratios of 7.
- **1000/1001 pull-down rates:** these are synchronous ratios that happen to
  sit inside `async`'s ±1000 ppm. They must never be served by `async`, since
  that would be routing by rate.

The **full 14 × 14 matrix is generated** in `rational`'s plan. Illustrative
rows:

| From → To | Chain |
|---|---|
| 48 ↔ 44.1 | `bridge` |
| 96 ↔ 88.2, 192 ↔ 176.4 | `bridge` at k = 1, 2 (after 2.2) |
| 96 → 44.1 | `rational` ↓2 → `bridge` |
| 176.4 → 48 | `rational` ↓4 → `bridge` |
| 48 → 88.2 | `rational` ↑2 → `bridge` k = 1 |
| 44.1 → 16 | `bridge` → `rational` ↓3 |
| 48 → 32 | `rational` 2/3 (one stage) |

### 2.2 `bridge` at 2× and 4× rates (a follow-up after the migration)

**What the Hz values reach.** `bridge`'s profile Hz values feed:

- the normalized cutoff (`design.h:138`);
- the validation `p.passband_hz >= traits::k_stopband_edge_hz`
  (`design.h:133-134`).

`ratio_traits` hard-codes `k_input_rate_hz` and `k_stopband_edge_hz`
(`design.h:37-55`). So a 2×-rate design cannot even be *expressed* today:
a 36 kHz passband throws "bad profile".

**The follow-up:**

- Add a rate-scale parameter `k` to `ratio_traits`. This is an API change.
- Document the profile edges as fractions of the rate.
- Pin with a test that `design_prototype<D, k=1>` is bit-identical to
  `<D, k=0>`.

**The claim holds exactly.** Power-of-two scaling is exact in IEEE double,
and `kaiser_beta` depends only on dB (R2-COH-9).

**At 2× rates:**

- 88.2 → 96 (up) places images at ≥ 44.1 kHz.
- 96 → 88.2 (down) folds aliases above 40.2 kHz.

`bridge/PLAN.md` keeps "no other ratios" until then. The follow-up is
sequenced before `rational`.

---

## 3. Inventory (measured 2026-09-26/27; corrected in v3)

**Clones.** Use fresh, full GitHub clones only. The session checkouts are
shallow (SampleRateTap) or have a stale local `main`.

**Remote state:**

- **SampleRateTap `main`:** `2b4dff1` (136 commits). The migration branch
  carries the plan commits on top.
- **RatioTap `main`:** `349ab7b` (29 commits, no renames, no tags). Its PRs
  were **rebase-merged**, not squashed: #16 has 6 commits, and #2, #3, #6 and
  #10 have several each. Its root commit has no PR. Commit messages carry no
  `#NN`.
- **Leftover RatioTap branches:** `claude/sample-rate-solutions-comparison-mqc190`
  (merged PR #17) and `claude/sample-rate-expansion-strategies-ezqzu6`.
  Neither is imported (`--refs main`); both are deleted at step 5.
- **Visibility:** all three repositories are **public**
  (`"visibility": "public"`, 0 billable CI minutes). v2's "private
  repository" came from a stale comment in `book-pages.yml:4`. RatioTap has
  no Pages site.

### 3.1 Shipped headers

| Today | After |
|---|---|
| `include/srt/{asrc,pi_servo,polyphase_filter,sample_traits,spsc_ring,srt}.h` (1 525 lines) | `async/include/tap/sr/async/…`, with `asrc.h` → `converter.h` (D15) and `srt.h` → `async.h` (umbrella) |
| `include/srt/detail/kaiser.h` (a 26-line re-export into `tap::samplerate::detail`) | **Deleted in step 3.1.** `polyphase_filter.h:152,153,157` requalify as `tap::dsp::`. `tests/test_kaiser.cpp` (9 tests) repoints to `tap::dsp` with its test names unchanged. Path citations in the book, the bibliography, `book_figures.py:7` and `asrc_rbj_analysis.ipynb` are rewritten |
| `include/tap/ratio/{converter,design,phase_table,ratio,schedule}.h` (820 lines) | `bridge/include/tap/sr/bridge/…` |

### 3.2 Pins

- Both repositories record DspTap at `0eb09fa`.
- RatioTap pins `submodules/sampleratetap` at `2b4dff1`.
- **Step P changes SampleRateTap `main`**, so step P.4 re-pins RatioTap's
  copy to post-P `main`. That way step 0's cross-validation lines (G6) are
  measured against the same async tree that step 1c compiles
  (R2-COH-1).
- Every gate runs `git submodule update --init --recursive` first.

### 3.3 Duplicated infrastructure

**Three harness copies exist:** SampleRateTap, RatioTap and DspTap. The
merge removes one. Adopting DspTap's copy is out of scope, because it has
diverged and would change codegen inputs.

SampleRateTap vs RatioTap:

| File | Class | Detail |
|---|---|---|
| `.clang-*`, `STYLE.md`, `.pre-commit-config.yaml`, `.claude/**`, `scripts/tidy.sh`, `.github/pull_request_template.md` | identical | Verified with `cmp` / `diff -r` in the dry run |
| `platform/armv8m_startup.c`, `platform/*/**.ld` | cosmetic | Comments, copyright line and book `ANCHOR`s only |
| `cmake/arm-cortex-m33-mps2.cmake`, `…-m55-mps3.cmake` | **functional (one variable)** | `set(SRT_BARE_METAL ON)` vs `set(TAP_RATIO_BARE_METAL ON)`. `hexagon-linux-musl.cmake` sets **no** such variable (corrected, R2-RUN-17) |
| `tools/qemu_insn_plugin/insn_count.c` | functional (host-side marker) | `SRT_INSN_COUNT` vs `RATIO_INSN_COUNT`. Never affects the guest count |
| `scripts/icount.py` | functional (prefix, marker) | |
| `tests/bare_metal_main.cpp` | per-engine, never deduplicated | Filters and floors. RatioTap's M33 selection is **59** tests against a floor of 25 |
| `.github/workflows/style.yml` | functional | RatioTap's body wins |
| `scripts/fetch_hexagon_toolchain.sh` (RatioTap only) | functional | RatioTap's copy wins, and every Hexagon cache writer uses it, **including `compare.yml`** |

Present in only one repository:

- **SampleRateTap:** `cmake/r8brain.cmake`, `tools/compare_shim`,
  `bench/compare`, `compare.yml`, `ci-arm64.yml`, `book-pages.yml`, `book/`,
  `docs/`, `scripts/update_*_docs.py`, `scripts/book_figures*`,
  `examples/pico2_*`, `.git-blame-ignore-revs`.
- **RatioTap:** `scripts/fetch_hexagon_toolchain.sh`, `tools/reference/`,
  `tests/reference/`, `HANDOFF.md`, `CLAUDE.md`, `notebooks/requirements.txt`,
  and the CI dedup scheme.

GoogleTest is the same pin in both (`f8d7d77`, v1.14.0).

### 3.4 The book

There are 84 `{{#include}}` matches:

| Target | Count |
|---|---|
| `include/srt` | 42 |
| `submodules/dsptap` | 21 |
| `tools/capi` | 6 |
| `tests` | 4 |
| `platform/` | 7 |
| `cmake/` | 2 |
| `tools/qemu_insn_plugin` | 1 |
| escaped example in prose (`bibliography.md:104`) | 1 |

- **Moves at step 1:** 52 of the 84 break when step 1 moves their targets.
  The dry run rewrote them all; mdbook 0.4.40 then reported 0 warnings, and
  the rendered book was byte-identical to the baseline.
- **Prose:** 23 book files carry retired identifiers that mdbook cannot
  check, plus 5 links to RatioTap as a live repository.

### 3.5 CI (measured)

| Job | async (SampleRateTap) | bridge (RatioTap) |
|---|---|---|
| Host matrix | GCC, Clang, AppleClang, MSVC (MSVC `werror: OFF`) | GCC, AppleClang, MSVC with `WERROR=ON` |
| Sanitizers | ASan+UBSan, TSan | ASan+UBSan only, WERROR ON |
| arm64 native + TSan | `ci-arm64.yml` (scheduled/dispatch). **Currently tests nothing:** `-R 'SpscRing'` vs suite `spsc_ring` | — |
| Hexagon correctness | 22.5 min, serial, `-E` list of 8 | 11.0 min at `-j 4`, `-E` list of 2 |
| M33 correctness | 22.4 min (30-min timeout) | 0.4 min |
| `icount-ratchet` | 7 workloads × 3 targets | 10 × 3 |
| bench-smoke, compare-smoke, clang-format, book | yes | no |
| Triggers | push (all branches) + PR, cancel-in-progress by ref. **Every push to a PR branch runs twice** | push to `main` + PR + dispatch |
| Actions | SHA-pinned | tag-pinned |

Timings are from runs 36253867157 and 36256431538.

`main` is unprotected in both repositories.

### 3.6 C ABI

| Engine | Functions | Opaque type | Library | Version encoding |
|---|---|---|---|---|
| async | 8: `srt_{version,create,destroy,push,pull,status,designed_latency_seconds,reset_from_consumer}` | `SrtHandle` | `libsrt_capi.so` | decimal `srt_version()` |
| bridge (RatioTap) | 11 | `ratio_converter` | `libratio_capi.so` | bit-packed `ratio_version()`. Pinned **by nothing** except the ctypes binding |

The r8brain shim exports `srt_r8b_oneshot` and `srt_r8b_latency_frames`.

Bindings build **only when the library is missing**, so they can measure a
stale library. They also print CMake logs into notebook outputs.

### 3.7 Outside references

- **DspTap:** 25 files, all of them comments or docs.
  - Path citations change through a DspTap PR (step 5).
  - `STYLE.md` changes only through taphouse.
  - Close DspTap's `TAP_DSP_CP_MIN_CHANNELS` item (audit doc lines 126,
    392, 458 and 668) when `SRT_CP_MIN_CHANNELS` is retired.
- **SampleRateTap:**
  - RatioTap URLs: `README.md:407,432`, `book/src/part0/two-crystals.md:173`,
    `part5/scaling.md:194,352-355` and `docs/COMPARISON.md:234`.
  - Stale cross-validation figures in the README.
- **Outside this session** (confirmed by the user):
  - No other Tap repository consumes either one.
  - **taphouse's `sync.sh` includes RatioTap.** Drop it before archiving.
  - Merge commits are allowed.
  - Open RatioTap issues and PRs are checked at step 5.

---

## 4. Target layout

```
SampleRateTap/
├── CMakeLists.txt          root: project(SampleRateTap VERSION 0.4.0 from step 3), enable_testing(),
│                           option() defaults, dsptap once, add_subdirectory(async|bridge)
├── PLAN.md  CLAUDE.md  README.md   family files (step 4; PLAN.md is this document, moved)
├── LICENSE                 D14 line
├── requirements.lock       notebook environment (step P.3)
├── STYLE.md .clang-* .pre-commit-config.yaml .claude/ .github/ .gitmodules .gitignore
├── .git-blame-ignore-revs  repaired at P.1; extended after step 4
├── submodules/dsptap
├── cmake/  platform/  tools/qemu_insn_plugin/
├── scripts/                icount.py, tidy.sh, fetch_hexagon_toolchain.sh,
│                           update_icount_docs.py, update_perf_docs.py, book_figures*
├── book/                   (bridge chapters are follow-up work)
├── docs/                   Doxyfile (both engines); migration/ (removed at step 4)
├── async/
│   ├── CMakeLists.txt  README.md (+ icount table)  PLAN.md
│   ├── include/tap/sr/async/   tests/   bench/ (icount, compare)   examples/
│   ├── capi/   notebooks/   docs/ (PERFORMANCE, COMPARISON, HARDWARE_TESTING)
│   └── tools/compare_shim/   cmake/r8brain.cmake
└── bridge/
    ├── CMakeLists.txt  README.md (+ icount table)  PLAN.md  HANDOFF.md  CLAUDE.md
    ├── include/tap/sr/bridge/   tests/ (+reference/)   bench/   examples/
    └── capi/   notebooks/   tools/reference/   docs/HISTORY.md
```

### 4.1 File disposition

| Path today | Destination | Step |
|---|---|---|
| SRT `CMakeLists.txt`, `README.md` | `async/…` (pure move; the new root files come later) | 1a |
| SRT `include/ tests/ bench/ examples/ notebooks/` | `async/…` | 1a |
| SRT `tools/capi/`, `tools/compare_shim/`, `cmake/r8brain.cmake` | `async/capi/`, `async/tools/compare_shim/`, `async/cmake/r8brain.cmake` | 1a |
| SRT `docs/{PERFORMANCE,COMPARISON,HARDWARE_TESTING}.md` | `async/docs/` | 1a |
| SRT `docs/Doxyfile`, `docs/MONOREPO_PLAN.md`, `docs/migration/` | stay in root `docs/` (the plan moves to root `PLAN.md` at step 4) | —, 4 |
| SRT `book/`, `scripts/`, `cmake/{arm,hexagon}-*`, `platform/`, `tools/qemu_insn_plugin/`, dotfiles, `LICENSE`, `STYLE.md`, `.github/` | stay at root | — |
| RatioTap `main` | `bridge/…` through filter-repo | 1b |
| `bridge/.gitmodules`, `bridge/submodules/*` | rewritten into root `.gitmodules` throughout history; gitlinks removed at the merge | 1b |
| `bridge/{.clang-format,.clang-tidy,STYLE.md,.pre-commit-config.yaml,.claude,scripts/tidy.sh,.github/pull_request_template.md}` | deleted (byte-identical to root) | 1b |
| `bridge/.github/workflows/{ci,style}.yml` | ported into root workflows, then deleted | 1c |
| `bridge/scripts/fetch_hexagon_toolchain.sh` | root `scripts/` | 1c |
| `bridge/.gitignore` | deleted (root `build*/` already covers `build_capi/`) | 1c |
| `bridge/LICENSE` | deleted in the commit that writes D14's line into root `LICENSE` | 1c |
| `bridge/requirements.lock`, `bridge/notebooks/requirements.txt` | deleted (identical root lockfile from P.3) | 1c |
| `bridge/cmake/`, `bridge/platform/`, `bridge/tools/qemu_insn_plugin/`, `bridge/scripts/icount.py` | deleted (root copies) | 2 |
| `bridge/tools/capi/` | `bridge/capi/` | 3.1 |
| `bridge/CLAUDE.md` | build commands fixed at 1c; reduced to the charter at 4 | 1c, 4 |
| `docs/migration/` | created at 0; deleted at 4 (`runs.md` optionally kept) | 0, 4 |

### 4.2 Dependency rule and its enforcement (scheduled: step 3.4)

- `async` and `bridge` each depend on `tap::dsp` only.
- An engine may depend on a sibling only in `tests/` and `examples/`: bridge's
  cross-validation, and `bluetooth_bridge`.
- `capi/` is per-engine.

Enforcement:

1. **Link interface.** A configure-time assertion that each engine target's
   `INTERFACE_LINK_LIBRARIES` is exactly `tap::dsp`.
2. **Header isolation.** Each public header compiles in its own TU with only
   `tap::dsp` and its own engine's include directory on the path. This
   replaces v2's install-tree test, which needed install rules the repository
   does not have (R2-COH-14).
3. **Include grep.** `*/include/**` rejects `srt/`, `tap/sr/<other>/`, `../`
   and a sibling `__has_include`.
4. **Header count.** The header glob count is pinned.

---

## 5. Gates

Round 2 showed that one uniform "same-job A/B for everything" design breaks
two ways: it pushes the M33 and Hexagon jobs past their timeouts
(R2-GATE-2), and several gates had no input data or were red on a no-op.
v3 splits the gates into two classes:

- **A/B gates** are the toolchain-sensitive ones. They run in the dedicated
  **`migration-gates`** workflow (`.github/workflows/migration-gates.yml`, running `docs/migration/gates.py`),
  on a pinned `ubuntu-24.04` with `cancel-in-progress: false`. Each job checks
  out the gated SHA **and** the two step-0 tips: `tap/SampleRateTap@S0` and
  `tap/RatioTap@R0`, both public, so no token is needed. It builds all three
  with one toolchain and compares them. The jobs are cheap: under 3 minutes
  per engine per target for icount.
- **Snapshot gates** compare against the step-0 snapshot committed in
  `docs/migration/`. They do not depend on the toolchain.

Each run records in `docs/migration/runs.md`: the run IDs,
`ImageVersion`, and `dpkg-query -W gcc-arm-none-eabi qemu-system-arm`.

| ID | Class | Gate | Catches |
|---|---|---|---|
| G1 | snapshot | **Test multiset per (job, engine).** `ctest --show-only=json-v1`; names are unique through D16's prefixes. The collector asserts that the list is non-empty itself, because `--no-tests=error` is ignored under `-N` and `--show-only`. New rows need an entry in `docs/migration/allow.txt`. `bridge` newly runs under Linux Clang, where it passes clang `-Werror` 78/78; TSan runs `-L async` only | Dropped tests; label aliasing |
| G2 | snapshot | **On-target test multiset.** `[ RUN ]` lines from each QEMU leg's `Testing/Temporary/LastTest.log`, which every QEMU job uploads. `--output-on-failure` prints nothing on success, so the CI log alone is not enough. Keyed by (target, engine) | Losses hidden by the floors (up to 34 for bridge on M33); Hexagon exclusion drift |
| G3 | A/B | **Exact icount.** `icount.py --compare-json`: the gated SHA's measured counts equal step 0's counts **measured in the same job**, exactly. The measured workload **set** equals the baseline key set. Committed `baselines.json` files must be **byte-unchanged**. Everyday CI keeps ±3 % against the committed files, since unpinned apt toolchains drift (R2-CI-10). **Allowance (3.2):** `docs/migration/allow-g3.txt` may name, per (target, engine, workload), one symbol and the exact delta it accounts for; the gate then requires that exact delta, and re-proves the row with `fncount.c` (per-function counts: every other symbol identical as a multiset, the named one at exactly the delta). No row may name `tap::` code | Codegen and harness changes. Verified deterministic: RatioTap's M33 counts match the baselines to the instruction, and the namespace rename leaves all 10 unchanged |
| G4 | A/B | **Codegen identity of icount and C ABI binaries only.** Split the disassembly at `STT_FUNC` bounds and skip non-function bytes. Key functions by demangled name after the rename map, and compare them as a sorted multiset. Strip addresses and RIP displacements. Symbolize literal-pool words **through relocations** (a gate-only link with `-Wl,--emit-relocs`, or per-TU `objdump -dr`), and resolve string-literal pointers to their text after the name and path maps. Normalizer prototype: `audit2-gates/norm3.py`. **Test binaries are excluded:** a pure namespace rename changes their codegen (stack-slot swaps in `check_cross_validation`), and gtest embeds `__FILE__` | Real codegen changes in shipped code |
| G5 | A/B | **Output identity.** P.2's per-engine tests print `[ measured ] hash <workload> <fnv64>` for every direction × format × profile. The gate compares these lines between A and B **per host, in one job**, and compares the QEMU `checksum=` lines, which `icount.py` now prints. **Hashes are never pinned as constants:** async's float `interpolate()` hashes differently when FMA is available, so a pinned hash would fail on arm64 and macOS (R2-GATE-3) | Coefficient, table and datapath changes that leave icount unchanged |
| G6 | snapshot | **Cross-validation lines**, including the tolerance arguments. P.2 makes the test print its limit | A loosened tolerance. Also covered by G14 |
| G7 | A/B | **Compile and link flags.** Every gated configure sets `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. Each entry is keyed by source path under the 4.1 map and tokenized with shlex. `-o/-c/-MD/-MT/-MF` and their arguments are dropped. Paths become `<SRC>`/`<BLD>`, then the 4.1 path map and the macro map apply; FetchContent's `_deps` relocation is mapped away. The **ordered** token lists are compared. The same normalization applies to each target's `link.txt`, which holds the startup file, `-T`, specs and `--gc-sections`. `$CXX --version` is recorded. Linux and cross builds only | `-std`/`-ffp-contract` flips, leaked `-D`s, link changes |
| G8 | snapshot | **Book and API docs.** `mdbook build` clean, the image check, and `doxygen docs/Doxyfile` producing non-empty output, all in the `ci.yml` book job. `book-pages` itself is never dispatched from the branch, because it deploys | Broken anchors; an empty API reference |
| G9 | snapshot | **Retired identifiers.** Zero hits for `srt/`, `tap::samplerate`, `tap/ratio`, `tap::ratio`, `SRT_`, `TAP_RATIO_`, `srt_`, `ratio_capi`, `async_sample_rate_converter`, `basic_async_sample_rate_converter`, and retired `-D` option names in `.github/`. Exceptions are an explicit **(file, pattern) allowlist**: the guest icount markers `SRT_ICOUNT_DONE` and `RATIO_ICOUNT_DONE` (kept on purpose), each D7 tripwire line, `STYLE.md` until step 5, `bridge/HANDOFF.md` and `bridge/docs/HISTORY.md` (history). `bridge/PLAN.md` is **rewritten**, not allowlisted. The retired test prefix and label (`"ratio."`, `LABELS ratio`, `-L ratio`) are checked in CMake files and workflows. Applies from step 3.7 | Stale prose, code and CI flags |
| G10 | snapshot | **C ABI symbols.** `nm -D --defined-only` on Linux equals the snapshot under the name map, plus the D13 version functions | ABI drift |
| G11 | A/B | **Notebooks.** Pinned environment (`pip install --require-hashes`, Python version from `setup-python`). Bindings always rebuild **quietly**, printing the CMake log only on failure. Every figure cell also prints an FNV hash or `%.6g` summary of its plotted arrays. A and B are executed in one CI job. The normalizer applies the name and version map and drops timing lines and PNGs | Changed numbers and curves; stale libraries |
| G12 | snapshot | **History.** `--follow` and `blame` on a fixed file list. Blame of a moved file is not attributed wholesale to a migration commit. The new root `CMakeLists.txt` is checked with `git log --`, since its history starts at 1c | Lost blame |
| G13 | snapshot | **Every named workflow ran on each gated SHA.** `ci.yml`, `style.yml` and `migration-gates.yml` run automatically. `ci-arm64` and `compare` are dispatched on the SHA. `book-pages` is replaced by G8. Gated SHAs are 1c, 2 and 3.1–3.8 (1a and 1b are not buildable on their own) | Skipped or cancelled evidence |
| G14 | snapshot | **Rename-only residual.** Apply the committed mechanical rename script (`docs/migration/rename.py`: paths, namespaces, macros, targets, banners) to the step-0 trees, then `git diff --no-index` against HEAD. Every residual hunk must appear in a reviewed allowlist (`docs/migration/residual/<step>.txt`) | Anything the other gates miss: **a loosened `EXPECT_NEAR`, MSVC-only paths, docs, CI.** Round 2 showed that a tolerance change passes G1–G13 (R2-GATE-8) |

A step's gate lists the IDs it requires.

---

## 6. Migration

**Branch and PR.**

- After step P, the migration branch `claude/sample-rate-expansion-strategies-ezqzu6`
  is **rebased onto post-P SampleRateTap `main`**, keeping the plan commits.
  The plan is part of the PR, and becomes `PLAN.md` at step 4.
- A **draft PR** is open from the first migration push.
- Apart from step P's PRs, nothing reaches either `main` until step 4.
- **As executed (v3.1):** the user merged PR #49 after step 1, with a merge
  commit (`976f117`, 2026-09-28), so steps 0 and 1 are on `main`. Steps 2–4
  continue on the same branch name restarted from `main`, in a new PR. The
  gates are unaffected: A/B gates always rebuild S0 and R0, and snapshot
  gates compare against `docs/migration/snapshot/`. Step 4's post-merge
  checks already hold: `main` has 186 commits (≥ 140 + 33 + 13), and
  `--follow` on `bridge/include/tap/ratio/converter.h` reaches RatioTap M3
  (`a83d6d8`, now `b8bad2e`). Step 4's own merge only needs to keep a merge
  commit if the new PR carries further merges; otherwise any method keeps
  the imported history, which is already on `main`.
- RatioTap is written to only by step P's PRs and by the archive at step 5.

**Push discipline** (R2-CI-1, R2-GATE-9, R2-RUN-15):

- 1a, 1b and 1c are pushed **together**, because 1a and 1b are not buildable
  alone. The gated SHA is 1c.
- After that, **one gated commit per push**. The next push waits until every
  workflow for the previous SHA has concluded and its run IDs are in
  `runs.md`.
- Concurrency is set in step P so that the migration PR never cancels
  in-flight runs:

  ```yaml
  cancel-in-progress: ${{ github.event_name == 'pull_request' && github.head_ref != 'claude/sample-rate-expansion-strategies-ezqzu6' }}
  ```

  v2's `github.ref != 'refs/heads/main'` was backwards: on a PR the ref is
  `refs/pull/N/merge`, so it cancelled on the migration PR and never on
  `main`. On this branch, run 36269857609 was already cancelled by a later
  push.
- That makes roughly 12 gated pushes, at 25–50 minutes each.

### Step P — Pre-work (normal PRs in both repositories, before step 0)

The snapshot must not record existing breakage as green, and the harness
must produce the data the gates read.

**P.1 Repair existing breakage:**

- `ci-arm64.yml`: `SpscRing` → `spsc_ring`.
- Pico 2 examples: add the dsptap include path, and build them in CI once.
- `scripts/book_figures_trace.cpp`: move to the current API, or commit its
  "before" panel as an image.
- `.git-blame-ignore-revs`: `34bb89e…` → `b84020e738f771c7689ffc1e592f8448b9ce063f` and `e2f5a48`.
- README cross-validation figures updated to the current floors.

**P.2 Harness hardening (both repositories, identical where shared):**

- **`icount.py`:**
  - `--exact`;
  - `--compare-json A.json`;
  - workload-set equality;
  - a missing baselines file is fatal;
  - print the guest's `checksum=` line.
- **Hexagon runs:**
  - Resolve `shutil.which("qemu-hexagon")` **before** clearing the
    environment. `env -i` empties PATH, and the run then fails or silently
    picks apt's plugin-less qemu (R2-GATE-11).
  - Copy each workload to one fixed path.
  - Run `[abs_qemu, "-0", "w", "-d", "plugin", "-plugin", …]` with
    `env={}`. `-0 argv0` is supported in QEMU 8.2.2.
  - **Re-record the Hexagon baselines once**, with each repository's
    documentation duties:
    - SampleRateTap: the README icount table and a `docs/PERFORMANCE.md`
      ledger row;
    - RatioTap: a PLAN.md §7 ledger row.
- **Tests:**
  - D16's test prefixes and `LABELS`, including on the `*_tests_emulated`
    entries.
  - `--no-tests=error` on every executing ctest call.
  - `-V --output-log` plus artifact upload on the QEMU legs (the G2 input).
  - G5 hash tests that print `[ measured ] hash …`. On async M33 they are
    budgeted through `MAIN_FILTER`, and the M33 timeout rises to 40 minutes.
  - The cross-validation test prints its tolerance (G6).
- **CI hygiene:**
  - Adopt RatioTap's triggers (push `main` + PR + dispatch) in
    SampleRateTap, which ends the double runs, with the concurrency
    expression above.
  - `permissions: contents: read` on `ci.yml` and `style.yml`.
    `ci-arm64` keeps `issues: write` in its own workflow.
  - SHA-pin every action.
  - Pin `runs-on: ubuntu-24.04` for QEMU and ratchet jobs.
  - Add `ImageOS` to the `qemu-hexagon-plugins` cache key.
  - Build the plugin per job, not as an artifact: it takes under 1 s, and a
    shared artifact would couple jobs across images.

**P.3 Notebook environment.**

- Commit an identical `requirements.lock` at both repository roots: numpy,
  scipy, matplotlib, jupyter, samplerate, soxr, hash-pinned.
- Bindings always rebuild, quietly.
- Every figure cell gets an array-hash cell.
- Re-execute all notebooks in that environment. Confirm that their text
  outputs equal the committed ones, apart from the added hash cells.
- Mark `asrc_rbj_analysis` cell 18 (wall-clock timings) as excluded.

**P.4 Re-pin RatioTap's `submodules/sampleratetap`** to post-P SampleRateTap
`main` (R2-COH-1).

### Step 0 — Freeze and snapshot

- Record the two tips, `S0` (SampleRateTap `main`) and `R0` (RatioTap
  `main`), in `docs/migration/tips.txt`. Record their commit counts: every
  later count check uses these, not the literals 136 or 29.
- Assert that the full clones are not shallow.
- Commit the snapshot-class baselines (G1, G2, G6, G8–G10, G12) under
  `docs/migration/`.
- Commit `docs/migration/rename.py` (the G14 map) and its first residual
  allowlist. The map also serves G1/G2 as the test-name map
  (`ratio.` → `bridge.`, D16).
- The G2 collector strips the test-number prefix that ctest's verbose log
  puts on each output line (`1: [ RUN      ] …`) before it compares.
- **Done (v3.1):** S0 `5e2057f` (140 commits) and R0 `8f19e8b` (33), from
  CI runs 36348916668 and 36356088654 (`docs/migration/runs.md`).
  `rename.py` applied through 3.8 builds with `-Werror`, passes 77/77 and
  82/82, reproduces both tips' output hashes and cross-validation lines,
  and flags a loosened tolerance as residual. The map fixed three things the
  plan left implicit: the `SRT_RESTRICT`/`…_Q15_SMLALD`/`…_CHANNEL_PARALLEL`
  alias `#define`s are deleted rather than renamed; the kaiser re-export's
  two users requalify to `tap::dsp` at 3.1; and `SrtHandle` becomes
  `tap_sr_async_converter`, matching `tap_sr_bridge_converter`.
- If `main` must move, it moves in SampleRateTap only, and step 1 is re-cut.
  filter-repo is deterministic (tip `daceb8d` on two fresh clones, with
  the dry run's `ratio/` prefix).

### Step 1 — Import (1a, 1b and 1c are pushed together)

**1a — Pure move.** One commit. `git show -M --stat` shows **0 insertions
and 0 deletions**; the dry run moved 59 files.

- Use `git mv` for exactly the 1a rows of 4.1.

**1b — Import RatioTap.** One merge commit. Verified in the dry run, with
the two corrections that round found. The dry run used the prefix
`ratio/`; v3.1's `bridge/` changes only the prefix string, so the
rewritten tip differs from the dry run's `daceb8d` and is re-verified on
two fresh clones at 1b:

```sh
git clone https://github.com/tap/RatioTap rt && cd rt
test "$(git rev-parse main)" = "$R0"
git filter-repo --refs main \
  --path-rename :bridge/ --path-rename bridge/.gitmodules:.gitmodules \
  --blob-callback '
if blob.data.startswith(b"[submodule \"submodules/"):
    blob.data = (blob.data
        .replace(b"path = submodules/", b"path = bridge/submodules/")
        .replace(b"[submodule \"submodules/", b"[submodule \"bridge/submodules/"))'
cd ../SampleRateTap
git fetch ../rt main:ratio-import
git merge --allow-unrelated-histories --no-commit ratio-import
git checkout --ours .gitmodules && git add .gitmodules     # the one add/add conflict
git rm -r --cached bridge/submodules
git rm -rf bridge/.clang-format bridge/.clang-tidy bridge/STYLE.md \
  bridge/.pre-commit-config.yaml bridge/.claude bridge/scripts/tidy.sh \
  bridge/.github/pull_request_template.md
git commit        # message records R0 and the rewritten tip
git branch -D ratio-import                                  # never pushed
```

- `--refs main` keeps the leftover branches out, so the commit map has
  exactly as many entries as `main`.
- `git submodule update --init --recursive` works on the merge commit and on
  the rewritten RatioTap commits. The dry run checked the root commit, M1,
  M7c (which recurses into `bridge/submodules/sampleratetap/submodules/dsptap`)
  and the tip.
- The rewritten reformat commit `c0894cf` becomes `89c7eba` (`4c3562c` in the
  dry run, under the old prefix). It is recorded
  for `.git-blame-ignore-revs`.

**1c — Build glue and path fix-ups.** No change reaches codegen: G4 and G7
prove it.

**Done (v3.1).** 1b's rewritten tip is `654659e`, identical on two fresh
clones; the branch holds 182 commits (140 + 33 + 9). Measured locally before
the push, against S0 and R0 built in the same session (`gates.py`):
G7 exact for every host, M33 and M55 TU; G3 and G5 exact on M33 and M55 (the
new icount ELFs are byte-identical to S0's and R0's); G4 exact for the C ABI
libraries and all 17 icount binaries; G1, G6 and G10 equal to the snapshot;
G12 reaches every step-0 commit; G14 has no unlisted residual; the rendered
book is byte-identical to S0's. Hexagon A/B, the macOS/Windows legs and G11
run first in CI. Choices the plan left open:

- clang-tidy keeps each repository's old coverage: both engines' tests and
  examples, and bridge's icount workloads. async's workloads were never
  under the gate and fail it; bringing them in is follow-up work.
- ci-arm64 runs `-L '^async$'` (its old scope). TSan builds both engines and
  runs async only.
- The ratchet is one matrix job per engine, each with its own plugin marker
  and `icount.py`, until step 2.
- The book's anchor includes are a `rename.py` rule, not residual. **Gate 1:** G1, G2, G3, G4, G5, G6, G7, G8, G11, G12, G13 and
G14; every notebook binding and every standalone engine build configures and
builds.

- **Root `CMakeLists.txt`**:
  - `project(SampleRateTap LANGUAGES CXX)` and `enable_testing()`.
  - `option()` **defaults** ON for `SRT_BUILD_TESTS`, `SRT_BUILD_EXAMPLES`,
    `TAP_RATIO_BUILD_TESTS` and `TAP_RATIO_BUILD_EXAMPLES`, declared before
    the engines, **never FORCE**. CI's `-D…=OFF` must still win: every
    bare-metal job disables examples, because async's examples need Threads.
    Jobs that disable one engine's tests pass both engines' OFF flags.
  - `add_subdirectory(submodules/dsptap)`, then `async` and `bridge`.
  - Hoist the gtest settings (`INSTALL_GTEST OFF`, the Threads probe) to the
    root, so the result does not depend on the order of `add_subdirectory`.
- **Engine `CMakeLists.txt`:**
  - Guard with
    `if(NOT TARGET tap::dsp) add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../submodules/dsptap ${CMAKE_CURRENT_BINARY_DIR}/submodules/dsptap) endif()`.
    The dry run verified that this keeps `cmake -S async`, `cmake -S bridge`
    and the notebook bindings working. The plain relative guard in v2 broke
    all three (R2-RUN-3).
  - async: `add_subdirectory(tools/capi)` → `add_subdirectory(capi)`.
  - bridge: `srt_headers` becomes
    `cmake_path(… NORMALIZE)` of `${CMAKE_CURRENT_SOURCE_DIR}/../async/include`,
    kept `SYSTEM`.
  - Engines **keep their `project()`** until step 3.4, so
    `${PROJECT_SOURCE_DIR}/cmake/r8brain.cmake` resolves unchanged. No
    r8brain edit is needed.
- **Bare-metal:**
  - `arm-cortex-m33/m55` toolchain files set **both** `SRT_BARE_METAL` and
    `TAP_RATIO_BARE_METAL`.
  - `GTEST_HAS_*` stays **in each engine's `tests/CMakeLists.txt`**. Moving
    it to the root leaked it into the icount TUs and failed G7 in the dry
    run (R2-CI-5, R2-RUN-8). Setting both variables is what makes gtest and
    both test trees agree.
- **CI**:
  - Every job configures the root once and builds both engines.
  - Correctness jobs run **per (target, engine)** with
    `ctest -L '^<engine>$'`, each engine's own `-E` list and `-j`:
    - Hexagon: async serial, bridge `-j 4`.
    - M33: async gets a 40-minute timeout.
  - Host jobs: per-engine WERROR as in the draft (MSVC: async OFF, bridge ON).
  - Sanitizers: ASan for both engines; TSan with `-L async` only.
  - Ratchet: `icount-async` and `icount-bridge` jobs until step 2, each with
    its own baselines and README freshness check.
  - Add `migration-gates.yml` from the draft.
  - Replace root `style.yml` with RatioTap's body. It configures both
    engines with tests and icount ON, fails on an empty TU list, and is
    SHA-pinned.
  - Every Hexagon cache writer uses `fetch_hexagon_toolchain.sh`.
  - Delete `bridge/.github/workflows/`.
- **Paths:**
  - The 52 book includes.
  - `book-pages.yml` path filters.
  - Doxyfile: `INPUT = async/include bridge/include async/README.md` and
    `USE_MDFILE_AS_MAINPAGE = async/README.md` until step 4.
  - `bench-smoke` → `build/async/bench/srt_bench`.
  - `compare.yml` build paths.
  - `icount.py --baselines <engine>/bench/baselines.json`.
  - `update_icount_docs.py --engine` (async and bridge tables; README
    freshness per engine).
  - `update_perf_docs.py` default → `async/README.md`.
  - `book_figures.py`: `ROOT/"async"/"include"`.
  - Notebooks:
    - `asrc_demo` and `asrc_block_size_study`: `CAPI_DIR = REPO/"build"/"capi"`.
    - `asrc_comparison`: `TOOLS_DIR = REPO/"build"`.
    - `asrc_rbj_analysis`: `sys.path` → `"../../scripts"`.
  - README links, four in total: `async/README` `LICENSE`, and
    `bridge/README` `LICENSE` ×2 and `STYLE.md` → `../`.
  - `bridge/README`: build commands, and "eight workloads" → ten.
  - `bridge/CLAUDE.md`: build commands.
- **HISTORY.md:**
  - `bridge/docs/HISTORY.md` maps old SHA → new SHA → RatioTap PR for
    `git rev-list R0` (33 commits at R0, `tips.txt`; skipping the commit-map header).
  - PR numbers come from `GET /repos/tap/RatioTap/commits/<sha>/pulls`,
    queried once at cut time. The repository is public; the dry run built
    the map as `audit2-dryrun/history_map.tsv`.
  - PRs were rebase-merged, so several commits share one PR, and the root
    commit has none.
  - Optionally rewrite bare `DspTap #38` in messages at 1b with
    `--message-callback`.
- **LICENSE:** one commit writes D14's line into the root `LICENSE` and
  deletes `bridge/LICENSE`, so the notice is never absent.
- **Notebook environment:** delete `bridge/requirements.lock` and
  `bridge/notebooks/requirements.txt`; they are identical to the root lock.

### Step 2 — Shared infrastructure

**Changes:**

- Delete bridge's `cmake/`, `platform/`, `tools/qemu_insn_plugin/` and
  `scripts/icount.py`.
- `icount.py --engine async|bridge` sets its glob, baselines path and marker
  regex. **Guest-printed markers stay byte-identical**
  (`SRT_ICOUNT_DONE` / `RATIO_ICOUNT_DONE`, allowlisted in G9). Only the
  host-side plugin marker becomes `TAP_SR_INSN_COUNT`.
- **Ratchet:** one job **per target** measures both engines, with
  `fail-fast: false` and the plugin built in each job. The combined M33 run
  is about 45 s of QEMU, well within the timeout.
- Docs freshness runs as its own job, per engine.
- `compare.yml`: update it in the same commit, then dispatch it once.

**Gate 2:** G1–G7, G13 and G14, plus the `compare.yml` run.

**Done (v3.1).** `scripts/icount.py --engine async|bridge` (default
`async`, so the book's commands keep working) and the root plugin serve
both engines; bridge's duplicate `cmake/`, `platform/`, plugin and
`icount.py` are gone, so bare-metal bridge builds use the root toolchain
files (as CI has since 1c). Measured locally before the push: G3+G5 exact
on M33 and M55 for all 17 workloads against S0/R0 measured in the same
session, and exact (+0) against both committed baseline files; G1, G4, G5,
G6, G7, G10 and G14 pass. The rendered book changes only where it quotes
the plugin's marker line.

### Step 3 — Renames (one commit per class, each gated)

Every commit is formatted by the pre-commit hook as it is made, so the
**rename and its clang-format reflow are one commit**. Measured reflow:

- the namespace rename: 20 files, +101/−103;
- D15: 10 files, +61/−61.

Every step-3 commit's SHA goes into `.git-blame-ignore-revs` after step 4.
There is no separate reflow commit, since the hook would absorb it anyway
(R2-COH-6).

1. **Paths:**
   - `srt/…` → `tap/sr/async/…` (with `asrc.h` → `converter.h` and
     `srt.h` → `async.h`).
   - `tap/ratio/…` → `tap/sr/bridge/…`.
   - `bridge/tools/capi` → `bridge/capi`. Its standalone
     `add_subdirectory(../..)` becomes `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. bridge)`.
   - Delete `srt/detail/kaiser.h` per 3.1.
2. **Namespaces and names:**
   - `tap::samplerate` → `tap::sr::async`; `tap::ratio` → `tap::sr::bridge`.
   - The D15 mapping.
   - Test namespace `srt_test` → `async_test`. `ratio_ref` is unchanged; it
     is not a retired name.
   - **Done (v3.1), with one Hexagon allowance.** Codegen (G4), outputs
     (G5) and the M33/M55 counts are exact. On Hexagon 14 workloads count
     47–87 instructions fewer, all in musl's `memcpy`: the renamed
     exception-message literals change length, `.rodata` shifts, and the
     workload's final `*_ICOUNT_DONE` format string lands at an alignment
     whose `memcpy` path is shorter. Reproduced locally to the instruction
     with a per-function plugin (`docs/migration/fncount.c`): every symbol
     but `memcpy` executes an identical count in both trees. The 14 rows
     are in `docs/migration/allow-g3.txt`, and the gate re-proves them on
     every run (G3 row). The Arm legs print through semihosting from
     `main(0, NULL)` and have no such path. G4's Hexagon normalizer learned
     the PC-relative `add(pc,##imm)` form (the packet address is the base),
     the redundant `immext` value and the bare branch targets in the same
     commit; an absolute `##imm` is symbolized only into writable data,
     since this static musl link reaches read-only data PC-relatively.
3. **Macros:**
   - `SRT_VERSION_*` / `TAP_RATIO_VERSION_*` → `TAP_SR_VERSION_*` = 0.4.0 in
     both umbrella headers (D13).
   - `SRT_RESTRICT`, `SRT_Q15_SMLALD` and `SRT_CHANNEL_PARALLEL` → their
     `TAP_DSP_*` originals (they are pure aliases).
   - `TAP_RATIO_MIRRORED_DOT_ATTR` → `TAP_SR_BRIDGE_MIRRORED_DOT_ATTR`.
   - `SRT_CP_MIN_CHANNELS` → `TAP_SR_ASYNC_CP_MIN_CHANNELS`, with an
     `#error` tripwire.
   - `SRT_SC_*`, `RATIO_SC_*`, `SRT_CMP_*` → `TAP_SR_{ASYNC,BRIDGE}_SC_*` /
     `…_CMP_*`.
   - `*_TESTS_COMPLETE` → `TAP_SR_TESTS_COMPLETE`.
   - `*_BARE_METAL` → `TAP_SR_BARE_METAL`, in the toolchain files and both
     test trees.
   - `SRT_PICO2_*` → `TAP_SR_PICO2_*`.
   - Guest icount markers are unchanged.
   - **Done (v3.1).** `rename.py apply --step 3.3` (41 files) plus the hand
     edits G14 lists in `residual/3.3.txt`: `TAP_SR_VERSION_*` = 0.4.0 in
     both umbrella headers (`bluetooth_bridge.cpp` includes both, so the
     renamed macros must be token-identical in the same commit), the D7
     `#error` tripwire for `SRT_CP_MIN_CHANNELS`, the 1c toolchain lines
     retired (each file sets `TAP_SR_BARE_METAL` once), and
     `ratio_demo.ipynb` re-executed in the pinned environment for its
     version line (figures byte-identical; four timestamp hunks). The
     version value reaches two gates: the C ABI `srt_version` /
     `ratio_version` return the folded constant, allowed and printed by
     `allow-g4.txt` (100 → 400 decimal, 0x300 → 0x400 bit-packed, nothing
     else differs), and G11 maps the notebook's printed `RatioTap 0.3.0` to
     `0.4.0` by a 3.3 rule, since it executes both trees. `STYLE.md`'s macro
     example stays canonical (residual, G9-allowlisted until step 5) and the
     TapHouse-synced files joined `APPLY_SKIP`. G7 now compares `-D`
     definitions as the sorted set CMake emits them in, since the renamed
     `SRT_SC_*` moved past `TAP_DSP_FFT_CMSIS` in the Arm command lines
     with no change to the set. Measured locally before the push: G3+G5
     exact on hexagon (the 14 rows proved per function), m33 and m55 for all
     17 workloads; G4 17/17 per target; G7, G11 (all seven notebooks), G14
     (0 unlisted), the host gates but G12 (shallow clone); clang `-Werror`
     159/159 tests, clang-tidy clean, the book builds.
4. **CMake, dependency enforcement and workflows:**
   - Targets: `tap::sr::async`, `tap::sr::bridge`, umbrella `tap::sr`.
   - Internal targets renamed, including the ctest entries
     `srt_tests_emulated` / `tap_ratio_tests_emulated` →
     `tap_sr_{async,bridge}_tests_emulated`. They are listed in the G1 name
     map.
   - Engine `project()` → `tap_sr_async` / `tap_sr_bridge`, with no VERSION.
   - Root `project(SampleRateTap VERSION 0.4.0)`.
   - Options → `TAP_SR_*` per D9, with the D7 `FATAL_ERROR` tripwire for
     every retired option.
   - **All five workflows** (`ci`, `style`, `ci-arm64`, `compare`,
     `book-pages`) updated in the same commit.
   - The four 4.2 enforcement checks land here with their own tests.
   - **Done (v3.1).** `rename.py apply --step 3.4` (52 files, plus the
     `srt_headers` → `tap_sr_async_headers` and QEMU `label: ratio` →
     `bridge` rules) and the hand edits G14 lists in `residual/3.4.txt`:
     the root declares the D9 options once (the engines' own `option()`
     calls find the cache entry), `project(SampleRateTap VERSION 0.4.0)`,
     the engine subprojects `tap_sr_async` / `tap_sr_bridge` with no
     VERSION, async's INTERFACE target renamed `tap_sr_async` with one
     alias, the umbrella `tap::sr`, `cmake/retired_options.cmake` (the D7
     `FATAL_ERROR` tripwire for all 16 retired options, included by the
     root and by an engine configured on its own), and the doubled `-D`
     lines the unification left in the workflows removed. The 4.2 checks
     are the root `tests/`: per engine, `Family.LinkInterfaceIsDspOnly`
     (check 1), `Family.HeadersCompileInIsolation` (check 2: one generated
     TU per public header, only `tap::dsp` and the engine's include on the
     path, built by the test so a leaked include fails the test),
     `Family.IncludesStayInEngine` (check 3) and `Family.HeaderCountIsPinned`
     (check 4: 6 and 5), under the engine's label so every job that runs the
     engine's tests runs them; plus D13's `family.VersionMacrosAgree` under
     both labels (compile-only, so it runs on every leg). G1 puts a test in
     every engine bucket its labels name, and `allow.txt` rows may glob the
     job file. Measured locally before the push: G3+G5 exact on hexagon
     (the 14 rows proved per function), m33 and m55; G4 17/17 per target
     and the two allowed C ABI functions; G7, G11 (all seven notebooks,
     after `notebook_text` learned to join split stream chunks), G14 (0
     unlisted); host gates but G12; GCC and clang `-Werror` 168/168 tests
     (159 + the 9 family tests), clang-tidy clean, the book builds; the
     tripwire and each check script fail their negative case.
5. **C ABI:**
   - `srt_*` → `tap_sr_async_*` and `ratio_*` → `tap_sr_bridge_*`, including
     handle types, header names and library names.
   - The shim's exports → `tap_sr_async_r8b_*`.
   - The version functions per D13, and a new `CApi.VersionIsBitPacked`
     test, listed in the G1 name map.
   - Bindings renamed per D8.
   - **Done (v3.1).** `rename.py apply --step 3.5` (19 files: the C ABI
     functions, handle types, headers and libraries, the shim's exports
     and its source file, the `<target>_EXPORTS` defines, and the bridge
     binding `ratiotap_py.py` → `tap_sr_bridge_py.py`; async's notebooks
     keep their inline ctypes loader, since no module existed at S0) plus
     the hand edits G14 lists in `residual/3.5.txt`: `tap_sr_async_version`
     returns the family's bit-packed encoding (its decimal `100` becomes
     `0x400`, the value `tap_sr_bridge_version` returns), and
     `CApi.VersionIsBitPacked` per engine links the shipped shared library
     and pins the encoding and 0.4.0, declaring the probe itself so the C
     header stays out of the tidy gate; the test exists where
     `TAP_SR_BUILD_CAPI` builds the library (the host gate, Linux and
     macOS), which `allow.txt` lists per job file. G4's `allow-g4.txt` rows
     follow the renamed functions and print their diffs. Measured locally
     before the push: G3+G5 exact on hexagon (the 14 rows proved per
     function), m33 and m55; G4 17/17 per target and the two allowed C ABI
     functions; G7 (with the `_EXPORTS` rule), G10 (the renamed symbols
     plus D13's version functions), G11 (all seven notebooks; the step-0
     `libsrt_r8b_shim.so` line maps through a `lib…` rule like the C ABI
     libraries'), G14 (0 unlisted); the host gates but G12; GCC and clang
     `-Werror` 170/170 tests, clang-tidy clean, the book builds.
6. **Ratchet binaries:** prefix only, `tap_sr_<engine>_icount_*`. Workload
   names and baseline keys do not change.
   - **Done (v3.1).** `rename.py apply --step 3.6` (7 files: the two icount
     CMake target prefixes, `scripts/icount.py`'s engine prefixes, the
     comparison workloads' comments and the book's prose); no hand edits,
     and `residual/3.6.txt` carries step 3.5's list with the two
     `icount.py` hunks re-hashed. Guest markers, workload names and both
     `baselines.json` files are unchanged. Measured locally before the
     push: G3+G5 exact on hexagon (the 14 rows proved per function), m33
     and m55 with the binaries paired under their new prefix; G4 17/17 per
     target; G7, G14 (0 unlisted); the host gates but G12; clang `-Werror`
     170/170 tests with the ratchet workloads built, clang-tidy clean, the
     book builds.
7. **Docs and prose:**
   - The book: 23 files. This includes `part4/c-abi.md`'s decimal-encoding
     prose (lines 161-162 and 305-306) and the D15 naming-decision prose.
   - Non-book docs:
     - `async/docs/{PERFORMANCE,COMPARISON,HARDWARE_TESTING}.md`;
     - `examples/pico2_*/README.md`;
     - `bridge/{README,PLAN,CLAUDE}.md`;
     - notebook markdown (`asrc_comparison` 42 hits, `asrc_demo` 30,
       `asrc_block_size_study` 24).
   - RatioTap URLs; the `git clone …/RatioTap` instructions.
   - **G9 applies from this commit.**
   - **Done (v3.1).** No rename rule: the prose pass by hand, G14 listing
     every hunk in `residual/3.7.txt` (19 files). G9's own survey found 71
     hits, 69 of them structural and now allowlisted in `snapshot/g9.txt`
     with their reasons: the plan (48; it describes the migration and
     becomes `PLAN.md` at step 4), the D7 tripwire list (17, in
     `cmake/retired_options.cmake` since 3.4), the 4.2 include grep that
     names `srt/` on purpose (3) and the `#ifdef` half of the macro
     tripwire (1). The prose: `-L '^ratio$'` → `bridge` in bridge's
     README and CLAUDE.md; the C ABI chapter's probe is the bit-packed
     family version (D13) with the `tap_sr_async_*` prefix and the family
     build path; RatioTap's links, badge and clone instructions point at
     the `bridge` engine in this repository (book Part 0 and V, async
     README and COMPARISON.md, bridge README); the book's `asrc.h` is
     `converter.h` (D15; the chapter keeps its URL) and its `srt`
     namespace `tap::sr::async`; the `@file` lines the 3.1 moves left
     behind; and the example and bench paths under the family build tree.
     The mechanical renames of 3.2–3.6 had already reached the notebook
     markdown and the non-book docs, so nothing else remained there;
     `bridge/PLAN.md` is step 4's rewrite. Measured locally before the
     push: G9 0 hits; G3+G5 exact on hexagon (the 14 rows proved per
     function), m33 and m55; G4 17/17 per target; G7, G14 (0 unlisted);
     the host gates but G12; clang `-Werror` 170/170 tests, clang-tidy
     clean, the book builds.
8. **Banners (D14):**
   - Rewrite the 35 existing banner lines.
   - **Add** banners to every C/C++/Python source file that has none.
   - Update `bridge/tools/reference/make_reference_vectors.py`, and
     regenerate `tests/reference/reference_vectors.h`. That header's only
     residual must be its banner (G14).

**Gate for each commit:** G1 (the name map is empty except for the
renames listed in 3.4 and 3.5), G2, G3, G4, G5, G6, G7, G8, G10, G11,
G13 and G14, plus G9 from 3.7.

### Step 4 — Documentation and the PR

- `git mv docs/MONOREPO_PLAN.md PLAN.md`.
- Write the family `CLAUDE.md`. It records the dependency rule, D12,
  `git bisect start --first-parent`, and the cross-validation separation
  rule (R4).
- Write the family `README.md`.
- Per-engine `PLAN.md` and `README.md` (each with its icount table).
- `bridge/CLAUDE.md` reduced to the charter.
- Doxyfile main page → `README.md`.
- Delete `docs/migration/`, keeping `runs.md` if wanted.
- Mark the PR ready. **Merge with "Create a merge commit".**
- **Post-merge checks** (first run after PR #49, see section 6's preamble):
  - `git rev-list --count origin/main` ≥ count(S0) + count(R0) + N.
  - `git log --follow bridge/include/tap/sr/bridge/converter.h` reaches
    RatioTap M3 (`a83d6d8` before rewriting, `b8bad2e` after).
- **Follow-up commit:** append to `.git-blame-ignore-revs` the step-3
  commit SHAs and the rewritten RatioTap reformat commit `89c7eba`.
- Tag `v0.4.0`.

### Step 5 — Outside the repository

- A DspTap PR for the comment and doc references (3.7).
- A taphouse PR:
  - drop RatioTap from `sync.sh`, `drift-check.yml` and the catalog;
  - `STYLE.md`: the two-level namespace note, the macro example
    (`SRT_VERSION_MAJOR`), and the banner template (D14).
- The user checks open RatioTap issues and PRs.
- The user archives RatioTap, with a README pointer to the monorepo.
- Delete RatioTap's leftover branches (`…mqc190` and
  `claude/sample-rate-expansion-strategies-ezqzu6`).

### Next — `rational` (separate plan)

- The 2.2 follow-up comes first (`ratio_traits` k).
- Then `rational`'s own reviewed plan: L-th-band design math and `chain<>`
  (under D12) in DspTap, the engine, and the generated 14 × 14 matrix
  under 2.1's rule.

---

## 7. Non-goals

- **No algorithm, coefficient, design or process-loop change.**
  - Outputs are identical (G5), and codegen of shipped code is identical
    (G4).
  - Instruction counts are identical (G3), except P.2's one Hexagon
    re-record, which is a harness change made before the snapshot, and
    the 3.2 Hexagon allowance (`docs/migration/allow-g3.txt`): 47–87
    instructions per workload inside musl's `memcpy` in the final marker
    `printf`, from the renamed literals moving `.rodata`; every function
    of shipped code counts identically, and the gate proves it. The
    committed Hexagon baselines are therefore high by those amounts after
    the migration, inside the daily ±3 % gate; a post-migration re-record
    (and a marker print whose cost is layout-independent) is a follow-up,
    outside this plan's non-goals.
- **No new engine, profile or API function**, apart from D13's version
  function and its test.
- **DspTap code is untouched.** Its docs change through a DspTap PR, and
  `STYLE.md` through taphouse.
- **No install or package rules.** 4.2's check 2 does not need them.
- **The third harness copy (DspTap's) is not adopted.**

## 8. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Silently dropped tests or workloads | G1 and G2 multisets with unique names (D16); G3 workload-set equality; non-empty assertions |
| R2 | Infrastructure or build glue changes codegen | G3, G4 and G7 as same-job A/B; G14 |
| R3 | Docs rot | G8 from 1c; G9 from 3.7; G14 |
| R4 | The cross-validation loses its independence | Independence comes from the scipy leg and from the engines' structural difference, never from repository separation. G6 now prints tolerances, and G14 sees any tolerance edit. **Permanent family rule** (CLAUDE.md): a PR that changes cross-validation tolerances leaves `bridge/tests/reference/` untouched, keeps the scipy leg green, and does not also change async's datapath. `bridge` never includes async's bank or blend (D11) |
| R5 | Rewritten SHAs and PR numbers | `bridge/docs/HISTORY.md` built from the API (1c) |
| R6 | CI load | The repositories are public: no minutes cost. The Free concurrency caps (20 jobs, 5 macOS) mean queueing, not failure. About 20 `ci`+`style` jobs plus 4 gate jobs per push; at most 4 macOS jobs |
| R7 | `git bisect` across the merge | `--first-parent` is required and documented. Imported commits remain buildable in `bridge/` thanks to the `.gitmodules` rewrite |
| R8 | Charter erosion | 4.2 checks, D11, D12, per-engine PLAN.md files |
| R9 | Squash-merge of the final PR | D3; step 4; the post-merge history check |
| R10 | Toolchain drift blamed on a step | A/B gates in one job on a pinned image; image and package versions in `runs.md` |
| R11 | The migration outlives the snapshot's assumptions: many gated pushes over days, while `main` or the image moves | Re-cut rule (step 0); A/B gates always compare against S0/R0 built fresh in the same job |

## 9. Open questions

None. The decisions are recorded where they apply.

| Q | Resolution |
|---|---|
| Q1 | `HANDOFF.md` stays in `bridge/` (allowlisted as history) |
| Q2 | Rename to the `converter` family: D15 |
| Q3 | `bridge` at 2^k rates: 2.2 (follow-up) |
| Q4 | One family version 0.4.0, bit-packed: D13 |
| Q5 | Unified holder line; user reconfirmed ownership; banners everywhere: D14 |
| Q6 | Per-engine READMEs plus a family README |
| Q7 | Two-level namespace: D4 |
| Q8 | Icount tables in each engine's README: 1c |
| Q9 | Engine names `bridge` and `rational`: D5 (v3.1) |

---

## Appendix A — Round-1 audit disposition (v1 → v2)

Both appendices are the audit record. Where a finding's wording still
says `ratio` or `integer`, read `bridge` or `rational` (D5, v3.1); paths
and resolutions use the current names.

Citations in this table use **v2's** step numbering (v2 had a separate
reflow commit, so its step 3 sub-numbers differ from v3's). Rows that round
2 found only partly resolved are listed at the top of Appendix B and completed
there.

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
| DEC-5 | n | Table rows; 2/3 needs a rational stage | 2.1 rows; `rational` charter L/M |
| DEC-6 | M | `fractional_resampler` to DspTap contradicts settled M0 | D11 |
| DEC-7 | M | Moving `decimate.h` breaks MuTap / D10 | 2; the `rational` plan (section 6, Next) |
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

---

## Appendix B — Round-2 audit disposition (v2.1 → v3)

Reviewers: **RUN** (end-to-end dry run of steps 1a–1c on fresh full
clones: GCC/clang builds, M33 under QEMU, mdbook), **GATE** (gate
prototypes: disassembly normalizers, FMA hash experiments, CTest toy
projects), **CI** (workflow design against live Actions data), **COH**
(plan coherence; every Appendix A row re-checked).

Severity: B blocker, M major, m minor, n nit. **Rejected:** none.

**Round-2 corrections to Appendix A.** COH found these 19 round-1 rows only
partly resolved in v2:

- DEC-1, DEC-3, DEC-4, DEC-13;
- GIT-6, GIT-8, GIT-13;
- GATE-3, GATE-10, GATE-11, GATE-14, GATE-16;
- INF-2, INF-9, INF-11;
- INV-3, INV-6, INV-9, INV-12.

Each is completed in v3 under the COH, CI, GATE or RUN row that names it
below.

| ID | Sev | Finding (short) | Disposition in v3 |
|---|---|---|---|
| RUN-1 | M | `checkout --ours` leaves `.gitmodules` unmerged | 1b: `git add .gitmodules` |
| RUN-2 | M | `git rm -f` aborts on directory | 1b: explicit `git rm -rf` list |
| RUN-3 | M | `NOT TARGET` guard breaks standalone builds and bindings | 1c: `../submodules/dsptap` with a binary dir |
| RUN-4 | M | `add_subdirectory(tools/capi)` missed | 1c |
| RUN-5 | M | "Force ON" ignores CI's `-D…=OFF` | 1c: `option()` defaults, never FORCE |
| RUN-6 | M | Root `ctest` runs both engines under one `-E` | D9; 1c per-(target, engine) jobs with `-L` |
| RUN-7 | M | G4 unmeetable for gtest binaries | G4 scoped to icount and C ABI binaries |
| RUN-8 | M | Root-scope `GTEST_HAS_*` leaks into icount TUs | 1c: left in engine test trees |
| RUN-9 | m | HISTORY.md needs the API; PRs rebase-merged; 30 map entries | 3 (inventory); 1b `--refs main`; 1c HISTORY.md |
| RUN-10 | m | `--no-tests=error` ignored with `-N` / `--show-only` | G1 collector asserts non-empty |
| RUN-11 | m | Notebook and script paths under-specified | 1c: five paths listed |
| RUN-12 | m | G7 path shapes (`_deps`, `../async`) | G7 normalization; 1c `cmake_path` |
| RUN-13 | m | README link count wrong | 1c: four links |
| RUN-14 | m | Doxyfile `INPUT` and main page | 1c; step 4 |
| RUN-15 | m | 1a/1b red by construction under G13 | Push discipline; G13 gated SHAs |
| RUN-16 | n | r8brain edit unnecessary | 1c: engines keep `project()` until 3.4 |
| RUN-17 | n | Hexagon toolchain sets no BARE_METAL | 3.3 |
| RUN-18 | n | D14 wording; generator banner; unscheduled | D14; 3.8 |
| RUN-19 | n | New root `CMakeLists.txt` history check | G12 |
| RUN-20 | n | `bridge/.github/workflows` deletion timing | 4.1; 1c |
| GATE-1 (R2) | B | G4 red on a pure rename | G4 redefined (per-function, relocations, icount/C ABI only) |
| GATE-2 (R2) | B | A/B exceeds M33/Hexagon timeouts | Section 5: A/B only for G3/G4/G5/G7/G11 in `migration-gates` |
| GATE-3 (R2) | M | Pinned hashes fail with FMA | G5: per-host A/B, never pinned |
| GATE-4 (R2) | M | Duplicate-name label aliasing | D16 prefixes in P.2 |
| GATE-5 (R2) | M | G2 input not in logs; floor numbers | G2 `LastTest.log`; P.2 upload; 3.3 (59 tests) |
| GATE-6 (R2) | M | G7 inputs missing; undefined normalization | G7 fully specified, with `link.txt` |
| GATE-7 (R2) | M | G6 misses loosened tolerances | P.2 prints tolerances; G14 |
| GATE-8 (R2) | M | Regression passing all gates | **G14** (rename-only residual) |
| GATE-9 (R2) | M | G13 unmeetable (book-pages, dispatch-only, 1a/1b) | G13 names workflows and gated SHAs |
| GATE-10 (R2) | M | G11 leaks volatile text; compares little | G11 redefined; P.3 quiet bindings and array hashes |
| GATE-11 (R2) | M | `env -i` breaks qemu lookup | P.2: resolve absolute path first |
| GATE-12 (R2) | M | Forced options break cross/icount jobs | 1c `option()` defaults |
| GATE-13 (R2) | m | G9 vs kept markers | G9 (file, pattern) allowlist |
| GATE-14 (R2) | m | G10 per platform | G10 Linux-only |
| CI-1 | B | cancel-in-progress defeats per-SHA runs | Section 6 push discipline and concurrency expression (from P.2) |
| CI-2 | B | No A/B workflow; no G2 capture | `migration-gates` (draft); P.2 `--output-log` and upload |
| CI-3 | B | D9 vs per-engine jobs; Hexagon timeout | D9; 1c per-(target, engine) matrix |
| CI-4 | M | FORCE breaks bare-metal configures | 1c |
| CI-5 | M | Root `GTEST_HAS_*` fails G7 | 1c |
| CI-6 | M | Renamed options silently drop gates | D7 option tripwire; 3.4 updates all five workflows; G9 covers `.github/` |
| CI-7 | M | Merged jobs change coverage | G1 per (job, engine) plus `allow.txt`; TSan `-L async` |
| CI-8 | M | Repositories are public | 3; R6 |
| CI-9 | M | Double runs until step 2 | P.2 adopts triggers |
| CI-10 | M | `--exact` vs committed baselines turns daily CI red | G3: exact only in A/B; ±3 % stays for daily CI |
| CI-11 | m | Plugin artifact adds risk | P.2 per-job build; pinned image; `ImageOS` key |
| CI-12 | m | Docs freshness async-only | 1c/2 per engine |
| CI-13 | m | G4 on test binaries | G4 scope |
| CI-14 | m | Preamble contradicts step P | Section 6 preamble |
| CI-15 | m | Doxygen unchecked on PRs | G8 |
| CI-16 | m | Permissions and pinning; `compare.yml` cache writer | P.2; 3.3 |
| CI-17 | m | Ratio tests must see async as SYSTEM | 1c `srt_headers` kept `SYSTEM` |
| CI-18 | m | G5 tests in the tight M33 leg | P.2 `MAIN_FILTER` budget; 40-minute timeout |
| CI-19 | m | P.2 re-record documentation duties | P.2 |
| CI-20 | n | `INSTALL_GTEST` order dependence | 1c: gtest settings hoisted |
| COH-1 | M | Step P writes RatioTap; stale 29/136; pin | Section 6 preamble; step 0 counts; P.4 re-pin; 4.1 lockfile |
| COH-2 | M | Encoding pinned by nothing | D13 `CApi.VersionIsBitPacked`; 3.6 |
| COH-3 | M | D13 mechanics unspecified | D13 mechanics; 3.3–3.5 |
| COH-4 | M | G11 fails on rename commits | G11 name and version map |
| COH-5 | M | G9 fails by construction | G9 allowlist; 3.7 widened; `bridge/PLAN.md` rewritten |
| COH-6 | M | Rename/reflow split vs pre-commit | Step 3: rename and reflow in one commit, ignore-revs |
| COH-7 | M | Passband rule contradicts its example; no stopband rule | 2.1 rewritten (a)–(d); rows re-checked |
| COH-8 | m | Rate list circular; 352.8 missing | 2.1: exactly 14 rates; 352.8 excluded with reason |
| COH-9 | m | 2.2 facts; test needs API; direction sentence | 2.2 rewritten; section 2 table marks the follow-up |
| COH-10 | M | D14 false premise; banners unscheduled | D14 (user reconfirmed); 3.8 |
| COH-11 | M | Per-engine jobs run both engines | D9; 1c |
| COH-12 | m | Standalone builds; capi step | 1c guard; 3.1 capi fix; r8brain untouched |
| COH-13 | m | D9 option list incomplete | D9 |
| COH-14 | M | 4.2 enforcement unscheduled; install rules | 4.2 check 2 replaced; scheduled at 3.4 |
| COH-15 | M | G3 vs committed baselines; two tips; ratio A | G3; section 5 (S0/R0) |
| COH-16 | m | cancel-in-progress | Section 6 push discipline |
| COH-17 | m | Layout vs 4.1 (PLAN location, lockfile, scripts) | Section 4 tree; 4.1; step 4 `git mv` |
| COH-18 | m | 1c path list gaps | 1c paths |
| COH-19 | m | D15 scope and rationale | D15 full mapping; G9 |
| COH-20 | m | Rename-list gaps (test namespaces, emulated tests, `project()`, CP macro) | 3.2–3.4 |
| COH-21 | m | Ratio README table unguarded | 1c/2 freshness per engine |
| COH-22 | m | Plan tier vs concurrency | CI-8: public; R6 |
| COH-23 | m | Branch housekeeping | Section 6 preamble (rebase); 1b `ratio-import`; step 5 branches |
| COH-24 | n | Small inaccuracies (84 includes, line 152, audit line 126, HISTORY commit, `.gitignore`, reading guide) | 3.4; 3.1; 3.7; 1c; 4.1; reading guide |
