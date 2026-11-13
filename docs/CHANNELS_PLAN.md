# Channel counts at compile time (A13): plan

Status: **draft for the maintainer's review, 2026-11-13. Nothing here is executed.
Adversarially audited the same day ([`CHANNELS_PLAN_AUDIT.md`](CHANNELS_PLAN_AUDIT.md)):
52 of 53 findings survived, including that the code-size gain holds only for one count
per converter type in an image; the revision waits on the maintainer's answers to the
audit's section 2.** The
maintainer decided on 2026-11-13 that every channel count is known at compile time and
never at run time. This plan turns that into work: what the decision buys (measured on the
host, section 1), the decisions that shape the API (section 2, three of them the
maintainer's), a measurement on the real targets that gates everything after it
(section 3), four pull requests in a fixed order (section 4), and what "done" means
(section 5).

The house rules apply unchanged: substrate changes land in DspTap first, renames are clean
with no aliases (D7), one version bumped in three headers and the root `project()`
together (D13), the DspTap floor is raised with its tag (A9), the ratchet stays within
±3 % or is re-recorded with a reason, both configurations count the same (A12), and no
engine is ever routed to by rate (D12). No output changes: every pinned vector, matrix row,
output hash and ratchet checksum stays as it is, and those are the gate that proves it.

## 1. Why, and what it buys

Today every engine takes its channel count as a constructor argument. `bridge` stamps out
three channel shapes (one, two, any) for each of its five compile-time tap lengths and
picks one per call; since A12 (#97) `rational` does the same for its three entry points.
That dispatch is what A12's ceilings recorded: `rational`'s engine `.text` +25 … +50 % and
its M33 call stacks 184–208 B against 88–128 B before. `async` chooses between its two
history layouts at run time (`TAP_SR_ASYNC_CP_MIN_CHANNELS`: frame-major from four
channels, where the sample format takes the channel-parallel path).

With the count in the type, each converter compiles exactly one shape. Measured on the
host by pinning the dispatch to a constant in scratch copies of the headers at 2ed199a
(x86-64, GCC 13, `-O3 -DNDEBUG`); proportions only, the Arm targets are section 3's job:

| Host, size probe (one up and one down converter, stereo) | All shapes, today | One shape |
|---|---|---|
| `rational` hot-path code, float | 6,509 B | 1,984 B |
| `rational` hot-path code, Q15 | 9,676 B | 2,934 B |
| `bridge` hot-path code, float | 30,960 B | 10,772 B |
| `bridge` hot-path code, Q15 | 54,973 B | 23,006 B |
| `rational` `process()` frame, float (`-fstack-usage`) | 304–368 B | 208–256 B |
| `rational` `process()` frame, Q15 | 304–448 B | 224–288 B |

"Hot-path code" is `process()` with the walks and dot kernels it inlines; design and
construction code is not counted. `bridge`'s frames are not comparable on the host
(GCC keeps some of its walks out of line there).

| Host, instructions (callgrind, `scripts/icount.py`'s pinned environment) | Runtime count | Compile-time count |
|---|---|---|
| `rational` ↑2, 2 channels | 54,413,191 | 53,539,417 (−1.61 %) |
| `rational` ↓2, 2 channels | 28,551,355 | 28,541,653 (−0.03 %) |
| `rational` ↑2, 12 channels | 282,912,211 | 277,328,581 (−1.97 %) |
| `rational` ↓2, 12 channels | 161,390,269 | 157,470,937 (−2.43 %) |
| `bridge` 44.1 → 48, 2 channels | 79,169,263 | 79,215,186 (+0.06 %) |
| `bridge` 48 → 44.1, 2 channels | 99,512,817 | 99,550,166 (+0.04 %) |
| `bridge` 44.1 → 48, 12 channels | 470,747,117 | 464,012,693 (−1.43 %) |
| `bridge` 48 → 44.1, 12 channels | 593,269,419 | 586,773,702 (−1.10 %) |

The `rational` rows reproduce the recorded `host` baselines exactly; the `bridge` rows sit
within 0.2 % of theirs (a hand build, not CI's targets), so compare within the table.
Every checksum was unchanged.

So the decision buys **code size and stack**, with a small speed gain at counts above two
and none to speak of at two. It also simplifies the engines (the dispatch and the generic
loops go), turns zero channels into a compile error, and lets `async` pick its layout at
compile time. Not yet measured: the M33, M55 and Hexagon (section 3), and `async` (its
layout choice needs code, measured in A13d).

## 2. Decisions proposed

Each states the recommendation, the alternative, and what hangs on it. **Maintainer**
marks the three whose cost falls outside the code.

- **K1. Where the count lives.** A `std::size_t Ch` template parameter, second on every
  engine type, after the sample format, so `(S, Ch)` reads as the frame:
  `rational::basic_stage<S, Ch, R>`, `basic_chain<S, Ch, Rs...>`,
  `basic_converter<S, Ch, R>`; `bridge::basic_converter<S, Ch, D, K = 0>`;
  `async::basic_converter<S, Ch>`. The aliases drop `S` and keep `Ch` first:
  `rational::converter<Ch, R>`, `converter_q15<Ch, R>`, `chain<Ch, Rs...>`, the named
  chains `up_2_up_2<S, Ch>`; `bridge::converter_to_48k<Ch>` and its siblings;
  `async::converter<Ch>`, `converter_q15<Ch>`, `converter_q31<Ch>`. *Alternative:* the
  count last, which a chain's ratio pack cannot take, or a `frame<S, Ch>` type in place
  of `S`, which re-keys every sample trait. *Hangs on it:* every construction site in
  the family: about 110 constructor calls with a channel argument in `bridge`'s and
  `rational`'s trees and 60 `config.channels` assignments in `async`'s, plus the book.
- **K2. No default count.** *Alternative:* `Ch = 2` on the aliases, fewer edits but a
  choice the family wants declared left implicit (D12's spirit: the caller declares the
  topology by choosing a type). **Maintainer.**
- **K3. The rule and the accessor.** `Ch >= 1` by `static_assert` with a message, pinned by
  a compile-fail project (as `rational`'s charter is, `rational/tests/compile_fail/`); no
  upper bound (`async` serves 16-channel AVB bundles and HOA counts). `channels()` stays,
  as `static constexpr`, so its call sites do not change and DspTap can read it from the
  type (K7). No second name for the same fact (D7).
- **K4. What construction takes.** The channel argument leaves every constructor and
  `validate()`: `basic_stage(const profile& p = profile::economy())`,
  `validate(const profile&)`; `async::config` loses `channels`. Zero channels becomes a
  compile error, so `Stage.ZeroChannelsThrows`, `Chain.ZeroChannelsThrows` and `async`'s
  zero-channel hardening case are retired for compile-fail checks. The C ABIs keep their
  runtime rejection of zero (K8), and their tests keep theirs.
- **K5. One shape per instantiation.** `rational`: `process_block<Ch>` and its siblings
  become the only body; the three `switch (m_channels)` go, and the cursor loses its
  `channels` field. `bridge`: the tap-length dispatch stays (the profile is a runtime
  value); the channel branch goes; the hand-written one- and two-channel bodies stay under
  `if constexpr` and every other count takes the constant-count loop. `async`: the layout
  choice becomes `if constexpr` on `Ch` against `TAP_SR_ASYNC_CP_MIN_CHANNELS`, which
  stays as the A/B knob it is. A12's out-of-line entries and `rational`'s cursor stay.
- **K6. Allocation layout: only the channel-length containers change.** `bridge`'s outer
  vector of histories and `async`'s become `std::array<…, Ch>`, one allocation fewer
  each, with the footprint tests updated. `pull()`'s 16-frame scratch and the chain's
  scratch stay on the heap: moving them into the object trades an allocation for object
  size, a separate lever to measure on its own. *Alternative:* move them now, and mix a
  footprint change into a shape change.
- **K7. DspTap's `chain` takes the count from its stages.** `sync_stage` gains an optional
  static `channels()`; a stage without one counts as one channel (DspTap's decimators are
  mono); the chain `static_assert`s that its stages agree and sizes its scratch by the
  constant; the `chain(std::size_t channels, …)` constructor goes. Only `rational` uses
  `tap::dsp::chain` (checked across this tree, TapTools and PythonTap), so the break is
  contained. DspTap 0.2.0, tagged `v0.2.0`; this tree pins it and raises
  `TAP_SR_DSPTAP_MINIMUM` to 0.2.0 with the README's tested pairs (A9). *Alternative:*
  keep the runtime constructor beside the new path, two ways to say one thing.
- **K8. The C ABIs keep their signatures and serve a stated set of counts.** C has no
  templates, so `tap_sr_<engine>_create…(…, unsigned channels)` stays, and the symbol sets
  stay pinned (`rational`'s 23). How each serves a count:
  - **`bridge` and `rational`:** one and two channels natively; any other count as that
    many mono converters behind one handle, de- and re-interleaved through a scratch
    buffer. Exact, because these engines' channels are independent by construction
    (`TwoChannelsAreIndependent…`, extended to every count the C ABI tests use).
  - **`async`:** mono converters would be wrong (each would run its own servo and lock on
    its own estimate), so it serves a native menu and returns NULL for any other count.
    Proposed menu: {1, 2, 4, 6, 8, 12, 16}: mono, stereo, quad, 5.1, 7.1, 7.1.4 and a
    16-channel AVB bundle.
  - *Measured cost of a native menu:* host `.text` today (GCC 13, Release, 2ed199a) is
    924,147 B for `rational`'s C ABI, 167,918 B for `bridge`'s and 68,041 B for
    `async`'s. A native menu multiplies the hot-path share of each by its length; the
    mono fallback keeps the synchronous engines near today's size. No consumer outside
    these trees links a C ABI (Q2), so the fallback's extra copy costs only the
    verification layer.
  - *Alternative:* a native menu for all three (simplest to state, largest `rational`
    library), or one and two channels only (smallest, and C ABI tests at six channels
    change). **Maintainer: the menu.**
- **K9. Programs that learn the count at run time switch over a menu themselves.**
  `async/examples/alsa_bridge.cpp`'s `--channels` dispatches over `async`'s C ABI menu
  and says so in its usage text; the benches, probes and Pico examples already use
  constants. The switch lives in the program, never in an engine (D12's spirit).
- **K10. Sequencing and version.** Four PRs (section 4), as A11 was three. This tree's
  version moves once, to 0.7.0, in the last (D13), with no tag in between; until then the
  root README says the family is mid-change and which engines have moved. `PLAN.md`
  records the decision as D18, each engine's plan its part. *Alternative:* the three
  engines in one PR, one review and a #97-sized diff. **Maintainer: the split.**
- **K11. Hardware confirmation.** Before the 0.7.0 tag, `pico2_cyccnt` times `bridge` and
  `rational` on the RP2350 (Cortex-M33) at `main` before A13b and at A13c's head, both
  with and without exceptions, if the bench is available; otherwise the README states the
  gain is measured in instructions only. **Maintainer**, with Block B's bench.

## 3. Gate 0: measure on the targets first

No API changes until this passes; nothing from it is committed to the engines.

1. **Toolchains as CI installs them:** `gcc-arm-none-eabi` (13.2.1) and `qemu-system-arm`
   (8.2.2) from Ubuntu 24.04, the CodeLinaro Hexagon toolchain (clang 19.1.5) with its
   `qemu-hexagon`, the counting plugin (`tools/qemu_insn_plugin`) and the call-max plugin.
2. **Variants:** scratch copies of `bridge`'s and `rational`'s headers with the channel
   dispatch pinned to one and to two channels, as on the host.
3. **Measured, both configurations (`TAP_SR_NO_EXCEPTIONS_RTTI` off and on):** every
   ratchet row on the M33, M55 and Hexagon, the host rows at 2 and 12 channels, and the
   ceilings (engine `.text` and `.rodata`, image, and the M33 per-call stack, static and
   painted).
4. **Go when:** every streaming row's count is equal or lower than its baseline on every
   target in both configurations, with any rise above 0.5 % traced and either removed or
   brought to the maintainer; the engine `.text` falls for the declared shape on every
   target; and the M33 per-call stack falls. **No-go** returns to the maintainer with the
   numbers.
5. **Recorded:** the results as a table in this section, replacing section 1's host-only
   evidence.

`async` is not in gate 0: its layout choice needs code. A13d measures it against the same
criteria.

## 4. The pull requests, in order

Each PR runs the whole gate before it opens: every battery in both configurations on the
host, the nine QEMU legs, clang-tidy, clang-format and `check_retired_words`, every output
pin unchanged, and the ratchet and ceilings re-recorded with the reason where they move.
Ceilings are re-recorded downward when a figure falls, so the gain is gated and not just
observed.

- **A13a (DspTap): the chain takes its count from its stages** (K7). `chain.h`, its test
  (a two-channel test stage, and a compile-fail check that disagreeing stages are
  rejected), README and CLAUDE.md, the version to 0.2.0, tagged `v0.2.0` after merge. Gate:
  DspTap's battery on its hosted, M33, M55 and no-exceptions legs; the srdif fingerprints
  unchanged.
- **A13b: `rational`, with the pin bump.** The pin to DspTap `v0.2.0` and the floor to
  0.2.0 (A9); `stage.h`, `chain.h`, `converter.h`, `rational.h` and the named chains (K1 to
  K6); the C ABI (K8); tests typed over the count where the contract involves channels,
  at one or two elsewhere, so the QEMU legs' test images do not multiply; the zero-channel
  tests to the compile-fail project; every bench workload, probe and example; the README,
  CLAUDE.md, `PLAN.md` (a decision and the section 6 record), the book's Part VII and
  the quick starts. Gate adds: the scipy vectors, the 182-row matrix, the `decimate.h`
  cross-check, `CApi.*` bit for bit at every count the C ABI serves.
- **A13c: `bridge`.** The same pattern (K1 to K6, K8); `examples/bluetooth_bridge.cpp`,
  which composes with `async` while `async` still takes a runtime count. Gate adds: the
  scipy leg green and the cross-validation against `async` with its tolerances and
  `bridge/tests/reference/` untouched (R4).
- **A13d: `async`, and the family closes.** `basic_converter<S, Ch>`, `config` without
  `channels`, the resampler's layout at compile time (K5) and its two history containers
  (K6); the C ABI menu (K8); `alsa_bridge`'s dispatch (K9); the thirteen test files
  that set a count, with `MultiChannel` at 12 and 16 channels as constants; the book's Parts I and III where they
  describe the runtime layout switch; then the family: 0.7.0 in the three umbrella
  headers and the root `project()` (D13), `PLAN.md` D18, the root README's call shapes and
  every engine README (A4), the roadmap's A13 marked landed. Gate adds: `async`'s output
  hashes, and gate 0's criteria for `async` against its own baselines.

## 5. Done means

- Every engine's public types carry the count, and nothing in an engine reads a runtime
  channel count.
- Every output pin is unchanged: scipy vectors, matrix rows, output hashes, the C ABIs'
  bit-for-bit tests, every ratchet checksum.
- Every ratchet row is equal or lower than its pre-A13 baseline on every target in both
  configurations, re-recorded; the size and stack ceilings re-recorded at their lower
  figures.
- The two configurations count the same on every row (A12).
- The C ABIs' signatures and symbol sets are unchanged; each header states the counts it
  serves, and a count outside them returns NULL.
- The README's tested pairs name DspTap `v0.2.0`; SampleRateTap is tagged `v0.7.0`.

## 6. Risks, and where each is watched

- **The QEMU legs' test images.** Typing tests over `(S, Ch)` multiplies instantiations
  in images that must fit the MPS2 and MPS3 boards. Watched by typing over the count only
  where the contract is about channels, and by the legs' own image sizes in each PR.
- **Large constant counts.** With the count a constant, the compiler may unroll channel
  loops fully at 12, 16 or HOA counts, growing code and register pressure. The host shows
  a gain at 12; A13d measures `async` at 16; if a count regresses, its loop is kept rolled
  above a threshold, with the measurement beside it.
- **Every consumer's construction sites change,** with no aliases (D7). The current
  `-fno-exceptions` project moves at its 0.7.0 pin.
- **A mixed family between A13b and A13d.** Stated in the root README while it lasts.
- **The C ABI's mono fallback is slower** than a native shape. Accepted for the
  verification layer (Q2); a native count can be added to the menu if a caller needs it.

## 7. Out of scope

Moving the remaining scratch buffers into the object (K6's follow-up lever). Any change to
the header-only model. Fixing the profile at compile time (`bridge`'s tap-length dispatch
stays). Anything that changes an output.
