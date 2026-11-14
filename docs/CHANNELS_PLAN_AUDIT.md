# Audit of the channel-count plan (A13)

Status: **run 2026-11-13 against [`CHANNELS_PLAN.md`](CHANNELS_PLAN.md) at 45a22a0;
answered by the plan's v2 (2026-11-14), which cites these ids where it changes.** Four
independent reviewers each attacked the plan from one angle: its facts and inventory,
the family's settled decisions and rules, its engineering design, and its evidence,
gates and sequencing. Each reviewer's findings then went to a separate skeptic, who
re-checked the evidence in the trees and tried to refute every one. The reviewers read
the trees and re-ran the plan's host measurements; nothing was edited.

| Reviewer | Findings | Confirmed | Partly right | Refuted |
|---|---|---|---|---|
| Facts and inventory | 15 | 11 | 4 | 0 |
| Settled decisions and rules | 13 | 3 | 9 | 1 |
| Engineering design | 11 | 8 | 3 | 0 |
| Evidence, gates and sequencing | 14 | 7 | 7 | 0 |
| **Total** | **53** | **29** | **23** | **1** |

Many findings are the same problem found independently from different angles; they are
grouped below by problem, with the finding ids in brackets. Severities are the skeptics'.
Section 2's first question was answered on 2026-11-14 (one converter type can appear at
more than one count in an image); the plan's v2 adds the shared base (its K12), with a
host prototype measured against these numbers.

## 1. The premise needs qualifying

**The code-size gain holds only for an image that uses one channel count per converter
type** [design-2, major]. Today one instantiation serves every count. With the count in
the type, each count is a whole instantiation: the constructor, the table building,
`validate()` and the accounting are duplicated along with the hot path. Reproduced on the
host (whole-object `.text`, one `up_2` and one `down_2` stage, the count added as a
template parameter with the dispatch pinned by `if constexpr`):

| Counts in one image | Today, float | Count in type, float | Today, Q15 | Count in type, Q15 |
|---|---|---|---|---|
| one, `-O3` | 19,979 B | 15,084 B | 25,421 B | 18,282 B |
| two, `-O3` | 21,251 B | 22,413 B | 27,176 B | 27,650 B |
| three (1, 2, 6), `-O3` | 21,786 B | 30,268 B | 26,733 B | 38,072 B |
| three (1, 2, 6), `-Os` | 9,144 B | 14,533 B | 9,849 B | 15,590 B |

One count shrinks by 14 to 28 %; two grow by 2 to 7 % at `-O3` and `-O2` and by about
20 % at `-Os`; three grow by 39 to 59 %. The
growth is construction code, not the hot path (`design_stage<R>` does not depend on the
count and stays single). The usual remedy is to hoist the count-independent state and
code (rows, spans, design, `validate()`, accounting) into a base that does not depend on
the count, which the plan does not consider. Gate 0's probes are single-count, so as
written they would record only the favourable case.

**Section 1 measures a different thing from the ceilings** [evid-2, facts-12, rules-9,
design-9]. Its "hot-path code" counts only `process()` and what it inlines; the
ceilings' engine `.text` is the whole probe translation unit. On that measure the host
cut for `rational` float is about 30 % (15,052 → 10,527 B), not two thirds. A12's
recorded engine `.text` growth was +19 … +57 %, not the +25 … +50 % the plan and the
roadmap quote. A12's record attributes the M33 stack growth to the out-of-line frame,
which A13 keeps, so the plan's expectation that the stack falls rests on the host frame
measurements alone.

**The 12-channel host rows were not "pinned"** [gate-2, evid-1]: that variant adds a
compile-time `case 12` beside the runtime dispatch. With clang 18, the stereo rows do
not move at all (`rational` ↑2: −0.003 % against GCC's −1.61 %); the 12-channel gains
are similar.

## 2. Decisions the maintainer now needs to take

- **Whether the count in the type is still wanted, and with what remedy for several
  counts in one image** [design-2]. The options are: accept the growth for multi-count
  images; hoist the count-independent code into a base, measured on a multi-count probe;
  or narrow the decision to the engines' hot paths.
- **What the C ABIs do** [design-1, facts-3, capi-1, rules-4]. The C ABIs are the one
  place a count stays a run-time value, and the plan asked only about a menu, not whether
  that is acceptable. Its cost figures were wrong. Rebuilt on the host:
  - Native one and two channels with a mono fallback, the plan's proposal: `rational`'s
    C ABI grows 76 % (924,120 → 1,629,191 B of `.text`) and takes about 1.7 times as
    long to compile.
  - Native mono only, every other count as mono converters behind one handle: about
    11 % smaller (818,703 B) before the fallback's wrapper.
  - No C ABI test, notebook or binding uses any count but one or two, so the plan's
    "C ABI tests at six channels change" describes tests that do not exist.
  - A third option the plan missed: per-count create entry points.
  - For `async`, a native menu of seven counts multiplies its per-format code about
    five times.
- **K1 is a maintainer decision** [rules-6]. It sets the public type shape the 1.0
  freeze will hold, and the family has kept public names with the maintainer (D4, D5,
  D15). The plan also says "three" maintainer decisions and marks four. K2's alternative
  should say that today's defaults are one channel for `bridge` and `rational` and two
  for `async`, so `Ch = 2` would change two engines' defaults.

## 3. Gate 0 cannot be run as written

- **The go rule contradicts itself** [facts-1, rules-10, design-3, gate-3, major]. "Equal
  or lower" allows no rise; "any rise above 0.5 % traced" implies smaller rises pass. The
  plan's own host data has `bridge`'s stereo rows up 0.04 and 0.06 %. One numeric rule
  is needed, used by gate 0, every PR and section 5.
- **`bridge`'s committed baselines are stale** [gate-1, major]. A12 changed `bridge` and
  left its baselines within ±0.7 % without re-recording them; the host rows are 0.15 and
  0.23 % off, which CI's own targets reproduce (not "a hand build"). That gap exceeds the
  0.5 % threshold. The reference must be `2ed199a` measured in the same job as each
  variant (`scripts/icount.py --json-out`), committed with gate 0's table.
- **A variant pinned to one channel silently runs mono code on stereo workloads**
  [gate-2, major]. Every `bridge` and `rational` workload is stereo; the pinned-mono
  build prints `ok=1` with a wrong checksum and about half the count, and `icount.py`
  reports "IMPROVED beyond tolerance". No baselines file holds a checksum, so "every
  ratchet checksum unchanged" has no committed reference. Record the reference's
  checksums and diff them at every step; drop the mono variant unless mono workloads are
  added.
- **The two configurations' identity is not gated** [gate-5]. CI counts each against the
  shared baselines at ±3 %, so they can drift several percent apart and pass. Compare them
  directly (`--compare-json` on the no-exceptions build against the exceptions build's
  file), in gate 0, every PR and ideally CI.
- **`async` can and should be in gate 0** [design-4, gate-4, major]. On the M33, M55 and
  Hexagon its channel-parallel layout is already compiled out (`TAP_DSP_CHANNEL_PARALLEL`
  is 0 there), so a pinned-count copy measures it now; as written its no-go could first
  appear in the last PR, after three breaking changes have shipped.
- **The gate measures a model, and the PRs do not re-apply its rule** [seq-1]. The
  variants leave out K4, K6 and K7 and do not touch `pull()` or `flush()`. A13b's and
  A13c's gates should check their rows against the reference under gate 0's rule.
  `pull()` and `flush()` are pinned for outputs only; no workload measures their cost
  [gate-6].
- **The toolchain step would not produce Hexagon counts** [facts-11, tool-1]. The
  CodeLinaro `qemu-hexagon` has no plugin support; CI builds QEMU 8.2.2 from source as
  linux-user with plugins. There is no separate call-max plugin: `TAP_SR_CALL_MAX` is a
  line the counting plugin prints.

## 4. Gaps in the inventory

- **`async`'s precheck needs the count** [facts-2, rules-2, design-5, major].
  `config::validate(cfg)` is a static member of an untemplated struct. It checks zero
  channels, multiplies by the count in its overflow checks, and divides a power-of-two
  FIFO capacity by it. With `channels` gone from `config`, D17's precheck has to move,
  for example to `basic_converter<S, Ch>::validate(const config&)`. It is used in 18
  places across 11 files, including the README, the C ABI, the Pico examples and the
  book.
- **`fractional_resampler<S>` is built directly outside the converter** [facts-4,
  rules-5, design-10], including by `bridge`'s cross-validation test, which R4 guards.
  A13d changes it, so A13d's gate needs R4's terms (tolerances and
  `bridge/tests/reference/` untouched). Its other direct users: `async`'s output-hash
  and footprint tests, `bench_compare.cpp`, `cmp_main.cpp`, and the two-clock test
  harness.
- **Cross-engine edits the PR scopes miss** [facts-7, design-10]. `rational`'s
  `test_matrix.cpp` uses the chain constructor A13a removes, and its `bridge_stage.h`
  adapter wraps `bridge`; both change in A13b and the adapter again in A13c.
- **The Pico examples take the count at run time** [facts-6]. `pico2_cyccnt` passes a
  runtime count to all three engines, so A13b, A13c and A13d each edit it.
  `pico2_dualcore` runs a 2- and a 12-channel phase in one binary through one pointer
  shared with core 1, which is a restructure. CI's `pico2-build` job belongs in each
  gate.
- **The documentation inventory misses chapters and tests** [facts-5, facts-10,
  rules-12]. Part V's channels-and-scaling chapter ("`config::channels` is a run-time
  count with no architectural limit") is the chapter about this, and Part II's
  multichannel chapter and Part VI's converter chapter describe the stamped shapes.
  Zero-channel checks also sit inside both engines' `ValidateAgreesWithTheConstructor`
  tests and at `test_chain.cpp:528`.
- **The call-site counts are low** [facts-8]: 139 constructor calls with a channel
  argument in `bridge`'s and `rational`'s trees and 31 `validate(channels, …)` calls,
  against the plan's "about 110".

## 5. Rules and sequencing

- **A4 needs the READMEs in the same PR** [rules-1, facts-5, design-11, seq-2]. The root
  README spells out `validate(channels, profile)` for `bridge` and `rational`, which A13b
  and A13c make false, and `bridge`'s README quick start is compiled as a test
  (`bridge.Family.ReadmeQuickStartCompiles`), so A13c's own gate fails if its README
  waits for A13d.
- **The DspTap floor touches all three umbrella headers** [rules-8, facts-9].
  `TAP_SR_DSPTAP_MINIMUM` is token-identical in each, so A13b edits `async.h` and
  `bridge`'s header too. The tested-pairs table is keyed by version: keep the v0.6.0 row
  and add a "main (0.7.0 in progress)" row until A13d.
- **Record D18 first** [rules-7]. The plan records the decision in A13d, after two
  engines have implemented it; meanwhile `bridge/PLAN.md`'s "Channels: runtime count"
  row and its siblings are contradicted on `main`.
- **K7's mono default is an implicit count** [design-6]. Once the runtime constructor is
  gone, a chain of stages that do not declare a count is sized as mono and would overflow
  if they ran several channels. Require `channels()` in `sync_stage`; it is one line in
  DspTap's `basic_decimator`.
- **K7's evidence** [rules-3]: MuTap, a DspTap consumer named in D10, was not checked (it
  reaches DspTap through the C ABI, which does not expose `chain`); PythonTap does not
  use DspTap at all.
- **`async`'s layout macro becomes an ODR hazard** [design-8]. Chosen by `if constexpr`
  on `TAP_SR_ASYNC_CP_MIN_CHANNELS`, two translation units built with different values
  could link one's constructor with the other's `process()`. Host-only, but state that
  the macro must be uniform, or derive the layout from a template parameter.
- **Block B and K11** [rules-11, k11-1]. B2's P7 figures for `bridge` and `rational`
  should be taken at or after 0.7.0. K11 should measure at A13d's head, `async`
  included (the firmware times its 2- and 12-channel pipelines), against `2ed199a`, with
  a pass criterion: cycles per frame no higher per case, within run-to-run noise.

## 6. Small factual corrections

`bridge` has four compile-time tap lengths per direction plus a runtime-length walk, not
five tap lengths [facts-13]. The C ABI sizes the plan quotes are `size`'s text column,
which includes read-only data; `.text` alone is 728,282, 150,333 and 57,286 B [facts-14].
In async's frame-major mode the history is one interleaved line, so its container is
`std::array<…, frame_major ? 1 : Ch>`, and some of `channels()`'s call sites read it
through an instance [facts-15].

## 7. Refuted

[rules-13] That K3's "no upper bound" claims more than the family has shown, because HOA
validation is deferred and the C ABI menu omits 9 and 25. Refuted: K3 is about the type,
`async` serves any count today and its tests run 16; the deferred item is on-target HOA
validation.
