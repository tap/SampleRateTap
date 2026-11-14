# Channel counts at compile time (A13): plan

Status: **v2, revised 2026-11-14 for the maintainer's review. Nothing here is executed.**
v1 (2026-11-13, 45a22a0) was adversarially audited the same day
([`CHANNELS_PLAN_AUDIT.md`](CHANNELS_PLAN_AUDIT.md): four independent reviewers, a
skeptic per reviewer, 52 of 53 findings surviving). This version answers every surviving
finding; the audit's ids appear in brackets where a change answers one. The maintainer
has answered the audit's first question: one converter type can appear at more than one
channel count in a firmware image. That makes K12, the shared base, part of the plan.
Two questions remain the maintainer's (K1, K8), with K2, K10 and K11.

The maintainer decided on 2026-11-13 that every channel count is known at compile time
and never at run time. This plan turns that into work: what the decision buys, measured
on the host (section 1); the decisions that shape the API (section 2); a measurement on
the real targets that gates everything after it (section 3); four pull requests in a
fixed order (section 4); and what "done" means (section 5).

The house rules apply unchanged: substrate changes land in DspTap first, renames are clean
with no aliases (D7), one version bumped in three headers and the root `project()`
together (D13), the DspTap floor is raised with its tag (A9), a call-shape change updates
the root README and every engine README in the same PR (A4), the ratchet stays within
±3 % or is re-recorded with a reason, both configurations count the same (A12), and no
engine is ever routed to by rate (D12). No output changes.

## 1. Why, and what it buys

Today every engine takes its channel count as a constructor argument. `bridge` stamps out
three channel shapes (one, two, any) for each of its five walks (four compile-time tap
lengths and a runtime-length walk for custom profiles) and picks one per call [facts-13];
since A12 (#97) `rational` does the same for its three entry points, to which A12's
record attributes the growth in `rational`'s engine `.text`, +19 … +57 % across the M33,
M55 and Hexagon [facts-12]. A12's M33 call stacks, 176–208 B against 88–128 B before, were
attributed to the out-of-line frame, which A13 keeps [rules-9, design-9]. `async`
already compiles its channel-parallel layout out on the M33, M55 and Hexagon
(`TAP_DSP_CHANNEL_PARALLEL` is 0 there) and chooses it at run time only on the host; its
runtime count costs a per-frame branch, its channel loops and divisions by the count
[design-4].

Every figure below is from the host (x86-64, GCC 13), measured with header copies; the
Arm targets are gate 0's (section 3). The prototype behind the third column is committed
as [`channels/stage_state_prototype.patch`](channels/stage_state_prototype.patch); the
probe and commands are in the appendix.

**Code size, whole object** (one `rational` ↑2 and one ↓2 stage, run at each of the
counts in one image; `.text` of the object, so construction code counts) [design-2,
evid-2]. "Count in type" adds the count as a template parameter; "with K12" also moves
the count-independent state and cold code into a base built once per format and ratio:

| Build | Counts in the image | Today | Count in type | With K12 |
|---|---|---|---|---|
| `-O3`, float | 1 | 19,979 B | 15,084 B | 16,609 B (−17 %) |
| `-O3`, float | 1, 2 | 21,251 B | 22,413 B | 19,069 B (−10 %) |
| `-O3`, float | 1, 2, 6 | 21,786 B | 30,268 B | 22,045 B (+1 %) |
| `-O3`, Q15 | 1 | 25,421 B | 18,282 B | 19,167 B (−25 %) |
| `-O3`, Q15 | 1, 2 | 27,176 B | 27,650 B | 22,559 B (−17 %) |
| `-O3`, Q15 | 1, 2, 6 | 26,733 B | 38,072 B | 26,457 B (−1 %) |
| `-Os`, float | 1 | 8,289 B | 7,009 B | 7,740 B (−7 %) |
| `-Os`, float | 1, 2 | 8,828 B | 10,493 B | 9,507 B (+8 %) |
| `-Os`, float | 1, 2, 6 | 9,144 B | 14,533 B | 11,795 B (+29 %) |
| `-Os`, Q15 | 1 | 8,994 B | 7,697 B | 8,428 B (−6 %) |
| `-Os`, Q15 | 1, 2 | 9,533 B | 11,692 B | 10,468 B (+10 %) |
| `-Os`, Q15 | 1, 2, 6 | 9,849 B | 15,590 B | 12,402 B (+26 %) |

`-O2` reads like `-O3` (with K12: one count −21 %, two −14 %, three +0.3 to +1.3 %).
Without K12 every count is a whole instantiation and an image with three counts grows by
39 to 59 %. With it, the cold code exists once (2,128 B of `stage_state` against today's
2,088 B of `basic_stage` constructors at `-Os`), and what remains is the hot path, one
copy per count. At `-Os`, today's three shapes share their out-of-line helpers, so three
constant-count copies cost more: that is the residual +26 to +29 %, and the price of the
decision for a size-optimized image that runs one converter type at three counts.

**Instructions** (callgrind in `scripts/icount.py`'s pinned environment, `-O3`). For
`rational`, the host workload with the count really in the type; for `bridge`, the
dispatch pinned to two channels, and a compile-time 12-channel case added beside the
runtime dispatch [gate-2, evid-1]:

| Host workload | Today | Count in type |
|---|---|---|
| `rational` ↑2, 2 channels | 54,413,191 | 53,539,417 (−1.61 %) |
| `rational` ↓2, 2 channels | 28,551,355 | 28,541,653 (−0.03 %) |
| `rational` ↑2, 12 channels | 282,912,211 | 277,310,677 (−1.98 %) |
| `rational` ↓2, 12 channels | 161,390,269 | 158,035,783 (−2.08 %) |
| `bridge` 44.1 → 48, 2 channels | 79,169,263 | 79,215,186 (+0.06 %) |
| `bridge` 48 → 44.1, 2 channels | 99,512,817 | 99,550,166 (+0.04 %) |
| `bridge` 44.1 → 48, 12 channels | 470,747,117 | 464,012,693 (−1.43 %) |
| `bridge` 48 → 44.1, 12 channels | 593,269,419 | 586,773,702 (−1.10 %) |

For `rational`, K12 changes none of these: the prototype's counts equal the
count-in-type column to the instruction (K12 is prototyped for `rational` only; gate 0
builds it for all three engines). Every checksum is unchanged, and every coefficient of the 14 ratios in all
three formats hashes the same. With clang 18 the stereo rows do not move (`rational` ↑2:
−0.003 %) and the 12-channel rows gain 0.4 to 1.8 % [evid-1]. The "Today" column is the
code at 2ed199a: `rational`'s and `async`'s host baselines equal it, `bridge`'s do not
(0.15 and 0.23 % off on the host, up to ±0.7 % on the targets), because A12 changed
`bridge` within tolerance and did not re-record it [gate-1].

**Stack.** `rational`'s `process()` frame on the host (`-fstack-usage`) is 304–368 B for
float and 304–448 B for Q15 today, 208–256 B and 224–288 B with one shape, because one
frame no longer holds three inlined shapes. Whether the M33 frame falls is gate 0's to
show.

So the decision buys code size for an image that runs each converter type at one count,
holds it even at two or three counts in `-O2` and `-O3` builds once K12 is in, costs
+8 to +29 % for that converter at two or three counts in a `-Os` build, gains up to 2 % in
speed above two channels and nothing to speak of at two, turns zero channels into a
compile error, and removes the dispatch and the generic loops.

## 2. Decisions

Each states the recommendation, the alternative, and what hangs on it. **Maintainer**
marks the five whose cost falls outside the code: K1, K2, K8, K10 and K11 [rules-6].

- **K1. Where the count lives.** **Maintainer**: this is the public type shape the 1.0
  freeze will hold, and the family keeps public names with the maintainer (D4, D5, D15)
  [rules-6]. Recommended: a `std::size_t Ch` template parameter, second on every engine
  type, after the sample format, so `(S, Ch)` reads as the frame:
  `rational::basic_stage<S, Ch, R>`, `basic_chain<S, Ch, Rs...>`,
  `basic_converter<S, Ch, R>`; `bridge::basic_converter<S, Ch, D, K = 0>`;
  `async::basic_converter<S, Ch>` and `async::fractional_resampler<S, Ch>` [facts-4]. The
  aliases drop `S` and keep `Ch` first: `rational::converter<Ch, R>`,
  `converter_q15<Ch, R>`, `chain<Ch, Rs...>`, the named chains `up_2_up_2<S, Ch>`;
  `bridge::converter_to_48k<Ch>` and its siblings; `async::converter<Ch>`,
  `converter_q15<Ch>`, `converter_q31<Ch>`. *Alternative:* the count last, which a
  chain's ratio pack cannot take, or a `frame<S, Ch>` type in place of `S`, which re-keys
  every sample trait. *Hangs on it:* 139 constructor calls and 31 `validate(channels, …)`
  calls in `bridge`'s and `rational`'s trees, 60 `config.channels` assignments in
  `async`'s, and the book [facts-8].
- **K2. No default count.** **Maintainer.** Today's defaults are one channel for
  `bridge` and `rational` and two for `async`, so `Ch = 2` on the aliases would change
  two engines' defaults, not only save edits [rules-6]. *Alternative:* a default; the
  family's habit is that the caller declares the topology by choosing a type (D12's
  spirit).
- **K3. The rule and the accessor.** `Ch >= 1` by `static_assert` with a message, pinned
  by a compile-fail project (as `rational`'s charter is, `rational/tests/compile_fail/`);
  no upper bound (`async` serves any count today; its tests run 16). `channels()` stays,
  as `static constexpr`; call sites that read it through an instance keep working
  [facts-15]. No second name for the same fact (D7).
- **K4. What construction takes.** The channel argument leaves every constructor and
  `validate()`: `basic_stage(const profile& p = profile::economy())`,
  `validate(const profile&)`. For `async`, `config` loses `channels`, and D17's precheck
  moves to `basic_converter<S, Ch>::validate(const config&)`, because the checks need the
  count (zero channels, the overflow products, the power-of-two FIFO capacity divided by
  the count); `config::validate` is retired with no alias (D7), and
  `detail::ring_capacity_elems` takes the count [facts-2, rules-2, design-5]. Its 18 uses
  in 11 files (the README, the C ABI, the Pico examples, `test_hardening.cpp`, the book's
  Hexagon chapter) move in A13d. Zero channels becomes a compile error, so these leave the
  batteries for compile-fail checks: `Stage.ZeroChannelsThrows`,
  `Chain.ZeroChannelsThrows`, `async`'s zero-channel hardening case, the zero-channel
  halves of both engines' `ValidateAgreesWithTheConstructor`, and `test_chain.cpp:528`
  [facts-10]. The C ABIs keep rejecting zero at run time.
- **K5. One shape per instantiation.** `rational`: `process_block<Ch>` and its siblings
  become the only bodies; the three `switch (m_channels)` go, and the cursor loses its
  `channels` field. `bridge`: the tap-length dispatch stays (the profile is a runtime
  value); the channel branch goes; the hand-written one- and two-channel bodies stay under
  `if constexpr`, and every other count takes the constant-count loop. `async`: the
  per-frame mono branch becomes `if constexpr`, the divisions by the count become
  constant, and on the host the layout choice becomes `if constexpr` on `Ch` against
  `TAP_SR_ASYNC_CP_MIN_CHANNELS`, which stays the A/B knob it is and must then be uniform
  across a program, as the layout is no longer a runtime member [design-8]. A12's
  out-of-line entries and `rational`'s cursor stay.
- **K6. Allocation layout: only the channel-length containers change.** `bridge`'s outer
  vector of histories becomes `std::array<std::vector<S>, Ch>`; `async`'s becomes
  `std::array<…, frame_major ? 1 : Ch>`, one interleaved line in frame-major mode
  [facts-15]. One allocation fewer each, with the footprint tests updated. `pull()`'s
  16-frame scratch and the chain's scratch stay on the heap; moving them into the object
  trades an allocation for object size, a separate lever. *Alternative:* move them now,
  and mix a footprint change into a shape change.
- **K7. DspTap's `chain` takes the count from its stages.** `sync_stage` requires a
  `static constexpr std::size_t channels()`, checked as a constant expression; DspTap's
  `basic_decimator` gains it (one line beside `k_up`); the chain `static_assert`s that its
  stages agree and sizes its scratch by the constant; the
  `chain(std::size_t channels, …)` constructor goes. Requiring the declaration, rather
  than defaulting an undeclared stage to one channel, keeps the substrate from holding an
  implicit count that would size scratch wrongly for a caller's multichannel adapter
  [design-6]. Only `rational` uses `tap::dsp::chain` in this tree and TapTools; MuTap was
  not checked here and reaches DspTap through its C ABI, which does not expose `chain`
  [rules-3]. DspTap 0.2.0, tagged `v0.2.0` after A13a merges.
- **K8. What the C ABIs do.** **Maintainer.** C has no templates, so the C ABIs are the
  one place a count stays a run-time value; the question is whether that is acceptable
  and in what form [rules-4]. Nothing calls them with more than two channels: every C ABI
  test, notebook and binding uses one or two [facts-3, capi-1]. Measured on the host
  before K12 [design-1]: `rational`'s C ABI is 924,120 B of `.text` today; serving one and
  two channels natively makes it 1,629,191 B (+76 %) and compiles about 1.7 times slower;
  serving mono natively makes it 818,703 B (−11 %) before a fan-out wrapper. K12 moves
  the cold code out of the per-count instantiations, so gate 0 re-measures every option.
  The options:
  - **(a) Mono native, other counts fanned out** (recommended for `bridge` and
    `rational`): every count served by that many mono converters behind one handle,
    de- and re-interleaved through a scratch buffer. Exact because these engines' channels
    are independent; pinned by a finite set, 3, 6 and 12 channels in float, Q15 and Q31,
    against the native `basic_converter<S, N, …>` and `basic_chain<S, N, …>`, through
    `process`, `flush`, `outputs_for`, `frames_needed`, latency and `reset`, with one call
    longer than the scratch [capi-1].
  - **(b) One and two native, the rest fanned out:** the deployment shapes measured
    natively, at the size above.
  - **(c) One create entry point per count:** the count in the symbol, as the decision
    reads, with the symbol sets growing.
  - **For `async`:** fanning out would be wrong (each mono converter would run its own
    clock servo), so it serves a native menu and returns NULL otherwise. Recommended
    menu: one and two channels, the counts its callers use, growing when a caller needs
    more; a seven-count menu would multiply its per-format code about five times.
  The signatures and symbol sets stay (`rational`'s 23 pinned) under (a), (b) and the
  `async` menu.
- **K9. Programs that learn the count at run time switch over it themselves.** The
  switch lives in the program, never in an engine [facts-6]:
  `async/examples/alsa_bridge.cpp`'s `--channels` dispatches over a stated list and says
  so in its usage text; `pico2_dualcore` runs its 2- and 12-channel phases as two
  instantiations, with core 1's pointer typed per phase; `pico2_cyccnt` passes its counts
  as template arguments for all three engines, so A13b, A13c and A13d each edit it; the
  `async` benches' runtime-channel helpers (`bench_converter.cpp`, `bench_compare.cpp`)
  move in A13d.
- **K10. Sequencing and version.** **Maintainer: the split.** Four PRs (section 4), as A11
  was three. `PLAN.md` records the decision as D18 in A13b, the first PR in this tree, and
  each engine's plan updates its "runtime count" row in the PR that changes that engine
  [rules-7]. Each PR updates the root README and every engine README that states the call
  shape it changes (A4) [rules-1, facts-5, design-11, seq-2]. A13b raises
  `TAP_SR_DSPTAP_MINIMUM` to 0.2.0 in all three umbrella headers (they are
  token-identical) and adds a "main (0.7.0 in progress) | 0.2.0" row to the tested pairs
  beside the unchanged v0.6.0 row [rules-8, facts-9]. The version moves once, to 0.7.0,
  in A13d, where the in-progress row becomes the 0.7.0 row (D13). *Alternative:* the three
  engines in one PR, one review and a #97-sized diff.
- **K11. Hardware confirmation.** **Maintainer**, with Block B's bench. Before the 0.7.0
  tag, at A13d's head against 2ed199a, `pico2_cyccnt` times every case it runs on the
  RP2350 (Cortex-M33), `async` at 2 and 12 channels included, with and without
  exceptions. Pass: cycles per output frame no higher in any case, within the run-to-run
  spread measured by repeating each case at both commits; an exception goes to the
  maintainer [k11-1]. B2's P7
  figures for `bridge` and `rational` are taken at or after 0.7.0 [rules-11]. Without the
  bench, the README states the gain is measured in instructions only.
- **K12. Count-independent code is built once per format and rate.** Each engine splits
  into a base, templated on the format and the rate parameters only, that holds the
  state and the cold code (the table or bank and its building, the design, `validate()`,
  the constructor's allocations with the count as a value, `reset()`, the accessors and
  the latency accounting), and the per-count type, which adds `process`, `pull`,
  `flush` and their hot helpers with the count as a constant. The base's constructor and
  `reset()` stay out of line, so the per-count types do not each inline a copy. The host
  prototype for `rational` (the patch above) moves construction and `reset()` into
  `stage_state<S, R>`; its numbers are section 1's. *Alternative:* the count on the whole
  class, which grows an image with three counts by 39 to 59 % [design-2].

## 3. Gate 0: measure on the targets first

No API changes until this passes. The variants are scratch copies; nothing from them is
committed to the engines.

1. **Toolchains as CI installs them** [facts-11, tool-1]: `gcc-arm-none-eabi` (13.2.1)
   and `qemu-system-arm` (8.2.2) from Ubuntu 24.04; for Hexagon, the CodeLinaro
   toolchain (clang 19.1.5) and QEMU 8.2.2 built from source as linux-user with
   `--enable-plugins`, as the ratchet job does, because the toolchain's own `qemu-hexagon`
   has no plugin support; the counting plugin from `tools/qemu_insn_plugin`, which also
   prints the `TAP_SR_CALL_MAX` line the call-max workloads use.
2. **The reference: 2ed199a, measured in the same job as every variant** [gate-1]. Per
   engine, target and configuration, `scripts/icount.py --json-out` (counts and
   checksums) and `scripts/ceilings.py measure`; committed with gate 0's record. Every
   later comparison is against this reference, not the committed baselines, which are
   stale for `bridge`.
3. **The variants, all three engines** [design-4, gate-4]: the count in the type with
   K12, pinned to two channels, which is what every ratchet and probe workload runs. No
   variant pinned to one channel: a mono build runs stereo workloads with a wrong
   checksum and about half the count, which `icount.py` reports as an improvement
   [gate-2]. The host 12-channel rows use the workloads with the count as a template
   argument. A multi-count probe, one ↑2 and one ↓2 stage at {1, 2} and at {1, 2, 6},
   built Release and MinSizeRel per target.
4. **The rule, the same in gate 0, every PR and section 5** [facts-1, rules-10,
   design-3, gate-3]:
   - every workload's checksum equals the reference's;
   - no streaming row's count rises more than 0.5 % against the reference, on any target,
     in either configuration; every rise above 0.1 % is traced and explained in the
     record; each engine's summed streaming count does not rise;
   - the no-exceptions build's counts and checksums equal the exceptions build's exactly
     (`icount.py --compare-json` against the exceptions build's file) [gate-5];
   - every single-count probe's engine `.text` and image fall on every target, and no M33
     per-call stack rises;
   - the multi-count probe's figures are recorded at Release and MinSizeRel and brought
     to the maintainer with the go decision: `-Os` growth at two and three counts is
     expected (section 1), and its size on the targets is the maintainer's to accept.
   **No-go** on any other point returns to the maintainer with the numbers.
5. **The record:** a table in this section with two size measures stated by name, the
   ceilings' engine `.text` (the probe translation unit's whole `.text`) and a hot-path
   split from the probe image's symbols under one stated rule [evid-2]; the variant
   patches and scripts committed beside it under `docs/channels/`.

The gate measures a model of the change; each PR re-applies the rule to the real code
(section 4) [seq-1]. `pull()` and `flush()` are pinned for outputs only: no workload
measures their cost or size, today or after [gate-6].

## 4. The pull requests, in order

Every PR runs, before it opens: every battery in both configurations on the host, the
nine QEMU legs, CI's `pico2-build` job, clang-tidy, clang-format and
`check_retired_words`; gate 0's rule against the 2ed199a reference, with the checksum
diff and the configuration comparison; and the multi-count probe. The ratchet and
ceilings are re-recorded with the reason where they move, and ceilings downward when a
figure falls, so a gain is gated and not just observed.

- **A13a (DspTap): the chain takes its count from its stages** (K7). `chain.h`;
  `basic_decimator::channels()`; tests with a two-channel test stage and compile-fail
  cases for an undeclared stage and for disagreeing stages; README and CLAUDE.md; the
  version to 0.2.0, tagged `v0.2.0` after merge. Gate: DspTap's battery on its hosted,
  M33, M55 and no-exceptions legs; the srdif fingerprints unchanged.
- **A13b: `rational`, with the pin bump and D18.** The pin to DspTap `v0.2.0`; the floor
  to 0.2.0 in all three umbrella headers and the tested-pairs row (K10); D18 in
  `PLAN.md`; `stage.h` with K12's base, `chain.h`, `converter.h`, `rational.h` and the
  named chains (K1 to K6); the C ABI (K8); `tests/test_matrix.cpp`, which uses the chain
  constructor A13a removes, and `tests/support/bridge_stage.h`, which declares its count
  [facts-7, design-10]; tests typed over the count where the contract involves channels,
  at one or two elsewhere, so the QEMU legs' test images do not multiply; the zero-channel
  tests to the compile-fail project (K4); every bench workload, probe and example, and
  `pico2_cyccnt`'s `rational` cases; the root README and every engine README that states
  `rational`'s call shape; the book's Part VII and the quick starts; `rational/PLAN.md`
  (its decisions and section 6's record) and CLAUDE.md. Gate adds: the scipy vectors, the
  182-row matrix, the `decimate.h` cross-check, and `CApi.*` bit for bit at the K8 test
  set.
- **A13c: `bridge`.** The same pattern (K1 to K6, K8, K12); `bridge_stage.h` forwarding
  `bridge`'s count; `pico2_cyccnt`'s `bridge` cases; `examples/bluetooth_bridge.cpp`,
  which composes with `async` while `async` still takes a runtime count; `bridge`'s
  README, whose quick start compiles as `bridge.Family.ReadmeQuickStartCompiles`, and the
  root README's `validate` shape [facts-5]; the book's Part VI converter chapter and
  Part II's multichannel chapter [rules-12]; `bridge/PLAN.md`'s channels row. Gate adds:
  the scipy leg green and the cross-validation against `async` with its tolerances and
  `bridge/tests/reference/` untouched (R4).
- **A13d: `async`, and the family closes.** `basic_converter<S, Ch>`,
  `fractional_resampler<S, Ch>`, `config` without `channels` and the precheck moved
  (K4); the resampler's branch, divisions and host layout at compile time (K5) and its
  history container (K6); K12's base; the C ABI menu (K8); `alsa_bridge`,
  `pico2_dualcore`, `pico2_cyccnt`'s `async` cases and the benches (K9); the resampler's
  direct users, `bridge/tests/test_cross_validation.cpp`, `async`'s `test_output_hash`
  and `test_footprint`, `bench_compare.cpp`, `cmp_main.cpp` and
  `tests/support/two_clock_sim.h` [facts-4, rules-5]; the thirteen test files that set a
  count, with `MultiChannel` at 12 and 16 channels as constants; the book's Parts I and
  III where they describe the runtime layout switch and Part V's channels chapter
  [rules-12]; then 0.7.0 in the three umbrella headers and the root `project()` (D13),
  the tested-pairs row, the roadmap's A13 marked landed. Gate adds: `async`'s output
  hashes, and R4's terms for the cross-validation file this PR edits: tolerances and
  `bridge/tests/reference/` untouched, the scipy leg green [rules-5].

## 5. Done means

- Every engine's public types carry the count, and nothing in an engine's headers reads a
  runtime channel count; the C ABIs serve counts as K8 decides [rules-4].
- Every output pin is unchanged: scipy vectors, matrix rows, output hashes, the C ABIs'
  bit-for-bit tests, and every workload's checksum against the 2ed199a reference.
- Gate 0's rule holds at A13d's head against the reference, on every target, in both
  configurations, and the two configurations are identical; the ratchet's baselines and
  the size and stack ceilings re-recorded.
- The C ABIs' signatures and symbol sets follow K8; each header states the counts it
  serves.
- The README's tested pairs name DspTap `v0.2.0`; SampleRateTap is tagged `v0.7.0`;
  K11's measurement is recorded, or the README says there is none.

## 6. Risks, and where each is watched

- **Several counts of one converter type in a `-Os` image.** +8 to +29 % for that
  converter on the host even with K12 (section 1). Measured on the targets by gate 0's
  multi-count probe and put to the maintainer there; every PR re-runs the probe.
- **The QEMU legs' test images.** Typing tests over `(S, Ch)` multiplies instantiations
  in images that must fit the MPS2 and MPS3 boards. Watched by typing over the count only
  where the contract is about channels, and by the legs' own image sizes in each PR.
- **Large constant counts.** At 12, 16 or HOA counts the compiler may unroll channel
  loops fully. The host gains at 12; A13d measures `async` at 16; a count that regresses
  keeps its loop rolled above a threshold, with the measurement beside it.
- **Every consumer's construction sites change,** with no aliases (D7). The current
  `-fno-exceptions` project moves at its 0.7.0 pin.
- **A mixed family between A13b and A13d.** The READMEs say which engines have moved
  (K10), and the tested pairs carry the in-progress row.
- **`TAP_SR_ASYNC_CP_MIN_CHANNELS` set differently in two translation units** would link
  mismatched layouts once the choice is compile-time; host-only, stated in K5 [design-8].

## 7. Out of scope

Moving the remaining scratch buffers into the object (K6's follow-up lever). Any change to
the header-only model. Fixing the profile at compile time (`bridge`'s tap-length dispatch
stays). Anything that changes an output.

## Appendix: the host measurements

Section 1's size table: the probe below, with today's headers (`VARIANT=0`), the count in
the type with the dispatch pinned by `if constexpr` (`VARIANT=1` on the audit's header
copy), and K12's prototype (`VARIANT=1` on the patch applied), compiled
`g++-13 -std=c++20 <opt> -DNDEBUG -ffunction-sections -DPT=<float|std::int16_t>
-DVARIANT=<0|1> -DNCOUNTS='<+ run<2>() + run<6>()>'`, and the object's `.text*` sections
summed from `size -A`.

```cpp
#include <cstddef>
#include <cstdint>
#include "tap/sr/rational/stage.h"
using sample = PT;
namespace rat = tap::sr::rational;
#if VARIANT
template <std::size_t C> using up_t = rat::basic_stage<sample, rat::up_2, C>;
template <std::size_t C> using dn_t = rat::basic_stage<sample, rat::down_2, C>;
#else
template <std::size_t C> using up_t = rat::basic_stage<sample, rat::up_2>;
template <std::size_t C> using dn_t = rat::basic_stage<sample, rat::down_2>;
#endif
sample g_in[64 * 2], g_out[200 * 2];
template <std::size_t C> [[gnu::noinline]] std::size_t run() {
    up_t<C> up(C); dn_t<C> dn(C); std::size_t m = 0;
    for (int b = 0; b < 100; ++b) { m += up.process(g_in, 32, g_out); m += dn.process(g_in, 32, g_out); }
    return m;
}
int main() { return static_cast<int>((run<1>() NCOUNTS) & 1U); }
```

Section 1's instruction table: `rational/bench/icount/host_main.cpp` with its `converter`
alias replaced by `basic_stage<float, R, TAP_SR_RATIONAL_HC_CH>` on the prototype
headers, and `bridge/bench/icount/host_main.cpp` on header copies with `process_taps`'s
channel branch pinned to two, or with a `process_walk<12, T>` case added; built
`g++-13 -std=c++20 -O3 -DNDEBUG`, counted under callgrind with `--toggle-collect=
tap_sr_host_counted` and `icount.py`'s `LD_BIND_NOW` and `GLIBC_TUNABLES`.
