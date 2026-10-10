# Roadmap after the comparison plan: production readiness

Status: **drafted 2026-10-10, adversarially audited the same day (two independent
reviews: one against the trees' facts and settled decisions, one against engineering
judgement); section 7 lists the decisions still open.** Execution starts once section 7
is answered. This document sequences the open items the family's plans and ledgers
carry — `PLAN.md`, `async/PLAN.md` §4, `async/docs/PERFORMANCE.md` "Known debt",
`async/docs/HARDWARE_TESTING.md`, `bridge/PLAN.md` §9, `docs/AUDIT_2026-10_PLAN.md` — into
twelve weeks in three blocks, and says what "production-ready" means so the verdict can be
checked rather than believed. The house rules apply unchanged: substrate changes land in
DspTap first, renames are clean with no aliases (D7), one version in three headers and the
root `project()` (D13), the ratchet stays within ±3 % or is re-recorded with a reason, and
no engine is ever routed to by rate (D12).

## 1. Where the family stands (verified against the tree at v0.6.0, c120bc9)

**Evidence in hand.** Scipy golden vectors for `bridge` and `rational`; exhaustive phase
sweeps; `bridge`'s cross-validation against `async` (R4) and `rational`'s two independent
legs (scipy and DspTap's `decimate.h` as a second golden; `rational/PLAN.md` §5 says why
the async cross-validation is not repeated there); typed batteries over float/Q15/Q31;
pinned C ABIs with their symbol sets gated; ASan+UBSan over every engine and TSan over
`async` (host, and weekly on arm64 silicon); four host compilers (Linux GCC and Clang, macOS
AppleClang, Windows MSVC); three QEMU targets here (Cortex-M33, Cortex-M55, Hexagon) and
DspTap's own four Cortex-M legs; a two-sided ±3 % instruction ratchet per engine; executed,
hash-pinned notebooks; the October 2026 audit closed except F25 (a TapHouse PR, referred).

**What is not in hand.** Nothing has run on real silicon: every embedded number is a
deterministic instruction count under QEMU, so cycles, real `%core@48k` and real boot-time
construction cost are unmeasured for every engine. The three hardware harnesses exist (the
ALSA bridge, `pico2_cyccnt`, `pico2_dualcore`); the two Pico firmwares build in CI, the
ALSA bridge builds only where ALSA is found and is not CI-proven. `async`'s ppm estimate
wobbles under block-quantized transfer (the documented sensor ceiling; timestamps are the
documented path past it). No engine builds with `-fno-exceptions`, the default of the Pico
SDK, Zephyr and most Cortex-M firmware trees: every constructor validates by throwing, and
the Pico firmware has to force exceptions on. TSan has only ever run the SPSC ring with two
threads; `push()`/`pull()`/`status()` concurrency on the converter has not been under it.
No per-instance memory footprint or per-call stack figure is stated anywhere. The ratchet
gates mean instructions per frame, not the worst single call. There is no host-side
performance gate (the channel-parallel host path sits outside every ratchet). There is no
fuzzing, no long-horizon soak, no tail-latency benchmark, no changelog, no family-level
API-stability statement (a one-line pre-1.0 disclaimer in `async/README.md` only), no
`install()` rules (by DspTap's decision, unstated here), and DspTap is 0.1.0, untagged,
pinned by SHA, with no version macros a consumer could assert against. `main` is
unprotected.

## 2. What "production-ready" means here

An engine is production-ready when every row below is green. The verdict in section 6
is this checklist, not an opinion.

| # | Row | bridge | rational | async |
|---|---|---|---|---|
| P1 | Golden reference and independent leg, pinned in CI | ✓ | ✓ | ✓ (sim) |
| P2 | RT contract (`noexcept`, allocation-free, lock-free) stated and tested | ✓ | ✓ | ✓ |
| P3 | Thread contract stated and under TSan with the real call pattern | n/a | n/a | **open** (A4) |
| P4 | Error model usable without exceptions; Hexagon throw documented | **open** (A3) | **open** (A3) | **open** (A3) |
| P5 | Memory footprint per instance and stack high-water per call, stated and gated | **open** (A5) | **open** (A5) | **open** (A5) |
| P6 | Worst single-call cost gated (QEMU per-call max) | **open** (A6) | **open** (A6) | **open** (A6) |
| P7 | Measured on silicon: cycles, `%core@48k`, construction time | **open** (B2) | **open** (B2) | **open** (B1, B2) |
| P8 | Clock recovery proven on independent crystals, hours, zero underruns | n/a | n/a | **open** (B1) |
| P9 | Non-finite input behaviour pinned | **open** (A7) | **open** (A7) | **open** (A7) |
| P10 | Host hot path gated | **open** (A8) | **open** (A8) | **open** (A8) |
| P11 | C ABI: one error model, extension rule, compat test, version probe | **open** (C3) | **open** (C3) | **open** (C3) |
| P12 | Versioned substrate pin asserted at compile time | **open** (A9) | **open** (A9) | **open** (A9) |
| P13 | Changelog, semver and deprecation statement reconciled with D7 | **open** (C3) | **open** (C3) | **open** (C3) |

## 3. Block A (weeks 1–2): contracts and hygiene

Each item is one PR. Order the bench parts in week 1 (section 7, Q1) so Block B is not
gated on shipping.

- **A1. Re-record `rational`'s thirteen streaming baselines, with the reason.** The rows
  are the 2026-10-02 record; the construct row was re-recorded 2026-10-10. CI and the local
  harness (which reproduces `bridge`'s and `async`'s M55 rows exactly) now read, per row:
  M55 +76…+196, the by-4 chain +926, `ratio23_q15_eco` +355,117 (+1.93 %); M33 +158…+545,
  the chain +1,102, `ratio23_q15_eco` +7,346; Hexagon +52…+132, `ratio23_q15_eco` −224. The
  +1.9 % is ab8fea3 (2026-10-03, the Q15 mixed ratios going down at a power-of-two row
  gain), which stated the move and did not re-record; the sub-0.02 % residue coincides with
  d4fff5c, 391145d and the DspTap #54–#57 bumps. Precedent for an in-band re-record with a
  stated reason: `bridge/PLAN.md` M7e and `async/docs/PERFORMANCE.md` "Hexagon harness
  isolation". Also reconcile the ratchet rule's wording: root `CLAUDE.md` says "beyond
  tolerance", `bridge/CLAUDE.md` says "a change that moves a count"; the root's is the
  intended rule unless Q3 says otherwise. Decide Q3 (whether to take the `run<S>()`
  out-of-line pin at the same time) before this PR.
- **A2. Hygiene from `async/PLAN.md` §4.** Bring `async/bench/icount` under clang-tidy
  (drop the `style.yml` path exclusion); triage async's MSVC `/W4` output and set
  `TAP_SR_ASYNC_WERROR=ON` on the Windows leg. Hours, not days. The Hexagon unwinder
  (`-unwindlib=libunwind`) is time-boxed to one day: land it if a constructor throw
  propagates under `qemu-hexagon`, else record the attempt; A3 makes the throw unreachable
  in embedded deployments either way.
- **A3. An exception-free error model.** Per engine, a `noexcept` precheck
  (`config::validate(cfg)` returning a `const char*` reason, or `std::optional<error>`),
  the constructor's throw kept for hosts and documented as a contract violation when
  exceptions are off, a `__cpp_exceptions` guard so the headers compile under
  `-fno-exceptions`, and a CI leg that builds the Pico firmware and the three C ABIs with
  exceptions off. The C ABIs already return NULL on invalid config, so they route through
  the precheck. The shared error vocabulary, if any, lands in DspTap first.
- **A4. The converter under TSan with its real call pattern.** `async.Threads.*`: a
  producer thread pushing, a consumer thread pulling, a third thread polling `status()`,
  seconds of simulated drift including a stage promotion and a hard resync; run in the
  TSan job and in `ci-arm64.yml`. Today the only two-thread test is the ring's.
- **A5. Footprint and stack.** A footprint formula per engine in each README (`async`:
  `(L+1)·T·sizeof(coeff) + bit_ceil(fifo·channels)·sizeof(S) + scratch`; `bridge` and
  `rational` from their tables), a measured table per profile and format, and
  `-fstack-usage` high-water for `push`/`pull`/`process` on the M33 leg, pinned as
  ceilings beside the instruction counts, with per-engine, per-profile `.text` and `.rodata`
  ceilings on the DspTap pattern (`bench.yml` `text_ceiling_*`).
- **A6. Worst single call.** A per-call maximum-instructions scenario in the QEMU ratchet
  (the plugin counts between two guest markers per `pull()`/`process()`; the block that
  carries a whole-sample slip, a ring wrap, a servo promotion, the fade, a hard resync),
  gated two-sided. The host p99/max `pull(128)` row of the metrics table is measured on the
  Setup 1 Pi under `SCHED_FIFO` in Block B and recorded as a trend, never gated on CI.
- **A7. Non-finite and denormal input.** One test per engine that a NaN/Inf burst clears
  after one window and does not poison the servo (it reads counts only), two sentences per
  README on denormals (float accumulates in double; output may carry denormals onward).
- **A8. A host ratchet.** Deterministic `Ir` from callgrind (or `perf stat -e
  instructions:u` with pinned flags) over the multichannel float pipelines, ±3 %, the same
  `icount.py` mechanics; Q6 decides whether it is a hard gate.
- **A9. DspTap versioning.** Tag DspTap v0.1.0 at the tree this family pins (d7ebe4b or
  its successor), add one `tap/dsp/version.h` (one tree, one header; the D13 triplication
  exists only because the engines share no header), a `static_assert` in each engine
  umbrella header against the minimum DspTap it was tested with, a "tested pairs" table in
  the root README, and have each consumer's bump commit name the tag (a submodule gitlink
  is a SHA; TapTools' and MuTap's pins are theirs to move).
- **A10. Stale prose sweep.** `PLAN.md:328` and `bridge/PLAN.md` §9 still call the book's
  bridge chapters follow-up work (Part VI landed in #68, 2026-10-08); `async/PLAN.md` §3
  says v0.4.0 and §4 lists the construction scenario as open (done 2026-10-09);
  `docs/AUDIT_2026-10_PLAN.md` still says "execution in progress"; the comparison records'
  competitor pins are frozen by policy and each `COMPARISON.md` should say so with the
  date.
- **A11. Harness prerequisites for Block B** (written now, run then): the ALSA bridge
  gains per-device cumulative frame counters sampled with `clock_gettime` in the CSV,
  separate ALSA xrun counters, a refusal of non-`hw:` devices (the `default` device on a Pi
  routes through PipeWire and its own resampler), an `--rt` flag (`SCHED_FIFO`,
  `mlockall`), and `snd_pcm_status` hardware timestamps where the driver has them; the
  `--csv` plot script does the regression slope and the ppm wobble/breathing statistics;
  the host CI job installs `libasound2-dev` so the bridge is CI-proven;
  `pico2_cyccnt` gains `bridge` (147/160 Q15 economy) and `rational` (↑2, ↓2, 2/3 Q15)
  cases and a `copy_to_ram` option matched to the ratchet's flags.

## 4. Block B (weeks 3–8): the hardware campaign

Needs a bench: a Pi, two USB audio devices, a Pico 2. The maintainer runs; the assistant
writes the tooling and reads the results.

- **B0. Verify the clocks are independent.** Many cheap UAC1 dongles are adaptive or
  synchronous endpoints PLL'd to the host's USB frame clock: two of them share one
  effective clock and a hair dryer does nothing. Confirm `/proc/asound/cardN/stream0`
  reports an asynchronous endpoint for the device under test, or use an I2S HAT with its
  own oscillator as one side. Record device models and modes in the run report.
- **B1. Setup 1, with the right targets.** With zero under/overruns, frame conservation
  forces the long-run mean ppm to the true ratio, so "converges to < 1 ppm" tests the
  counting, not the servo. The production numbers are: the long-run mean within the
  regression's own uncertainty; the per-second ppm standard deviation and max excursion,
  stated per ALSA period size (this is the documented wobble, measured); latency breathing;
  zero library underruns with zero ALSA xruns over hours, under `chrt -f`; thermal tracking
  and the ±50 ppm step demoting and re-promoting the servo stage. Compare once against
  zita-ajbridge on the same dongles: that comparison is the headline an integrator wants.
- **B2. Setup 2 (Pico 2), direct numbers.** `%core@48k` with per-block p99/max,
  construction milliseconds at boot through the RP2350 DCP, SRAM used, for `async` Q15,
  `bridge` Q15 and `rational` Q15 — the rows P7 needs for every engine. A cycles-per-
  instruction ratio is stated only as a sanity figure measured with `copy_to_ram` and the
  ratchet's flags; it is not a calibration constant for future numbers.
- **B3. The timestamp servo on paper, and if Q1 allows, on the same bench.** The two-sided
  shape shipping converters use (zita-ajbridge, the JACK/PipeWire DLL): timestamps on both
  sides in one monotonic timebase, `push(frames, t_in)` and `pull(frames, t_out)` (or an
  `observe_*_clock(frame_index, t)` pair), a DLL estimating the ratio, FIFO fill kept for
  the latency setpoint only, `max_deviation_ppm` kept as the hard clamp so D12 holds. It
  rides the ring as an SPSC side-channel so the one-producer/one-consumer contract and
  `noexcept` are preserved, which A4's tests must prove. A new noise model for the
  simulator (timestamp jitter, scheduler spikes). This is a servo extension, four to five
  weeks, not an overload. Designing it during Block B lets a second bench pass measure both
  servos on the same clocks; if the bench is available once, the count-only servo is what
  Block B proves and timestamps ship in 1.1 after their own pass.
- **B4. Setup 3 (UDP sender and receiver)** only if time allows; it proves deployment
  shape, not clock lock.

## 5. Block C (weeks 9–12): the 1.0 shape

- **C1. Timestamp clock recovery lands** (B3's design) if the bench ran twice; otherwise
  it is the first 1.1 item and the 1.0 freeze excludes it.
- **C2. A sequence fuzzer, not a byte fuzzer.** The one real memory bug the audit found
  (F01) was a legal but unusual call sequence. A `FuzzedDataProvider`-driven harness:
  random config in the charter set × random sequence of push/pull/process/flush/reset
  sizes; invariants: no sanitizer report, `outputs_for` accounting holds, returns ≤
  requested, chunking invariance against a reference run. Short in CI, long weekly.
  Host-only (Hexagon's musl terminates on throw). A weekly 24-hour simulated run with a
  temperature-like random-walk offset (about four minutes of host time) pins zero
  under/overruns, a bounded integrator and the counter and accumulator wraps.
- **C3. 1.0 release engineering.** Reconcile the three C ABIs' NULL semantics (`async`
  tolerates NULL handles; `bridge` and `rational` say UB); a stated extension rule (new
  symbols only; status through a size-tagged struct or `_v2`); `SOVERSION` on the three
  shared libraries; a compat test compiling a frozen copy of each 1.0 header and a C
  caller against HEAD; the statement that MAJOR is the ABI or a separate
  `tap_sr_<engine>_abi_version()` (Q2); a `CHANGELOG.md` back-filled from v0.4.0; a
  semver and deprecation statement reconciled with D7 (at 1.x every rename is a MAJOR,
  written down); the consumption model stated (`add_subdirectory`/FetchContent as the
  supported model, or `install()` + `SampleRateTapConfig.cmake` with DspTap first, Q2);
  `main` protected; a manual workflow that re-executes the notebooks and diffs the figure
  digests as part of the release checklist. The 1.0 freeze covers `bridge`, `rational` and
  the existing `async` surface; the `bridge` C ABI stays at K = 0 and may grow additively
  as already promised.

## 6. Verdict rule

`bridge` and `rational` are production-ready when P1–P7 and P9–P13 are green: Block A
closes P3–P6, P9, P10, P12; B2 closes P7; C3 closes P11 and P13. `async` additionally needs
P8 from B1. On this sequencing, `bridge` and `rational` reach the bar at the end of Block C
(not Block B: P11 and P13 are release engineering), and `async` reaches it at the same
point with the count-only servo, provided B1's numbers are inside the README's claims; the
timestamp servo is 1.1 unless the bench ran twice.

## 7. Decisions the maintainer takes before week 1

- **Q1. The bench.** Which USB devices, and are they asynchronous-mode (feedback
  endpoint), or is an I2S HAT with its own oscillator available? Is the bench available
  once (count-only servo) or twice (count-only, then timestamp)? The answer decides
  whether timestamps are 1.0 or 1.1.
- **Q2. What 1.0 promises.** Does any consumer outside these trees link a C ABI today (the
  audit's A5 was answered "none named")? Is MAJOR the ABI, or a separate ABI version? Is
  `add_subdirectory`/FetchContent the only supported consumption model at 1.0, or do you
  want `install()`/`find_package`? Recommendation: MAJOR is the ABI; FetchContent only,
  stated.
- **Q3. A1's scope.** Re-record `rational` alone (recommended; `bridge` and `async` are
  exact), or take the `run<S>()` out-of-line pin (`async/docs/PERFORMANCE.md`, "left for a
  deliberate re-record") now and re-record all three engines' Arm baselines once? And is
  the root's "beyond tolerance" the intended ratchet wording, so `bridge/CLAUDE.md` is
  aligned to it?
- **Q4. DspTap's release coupling.** Tagging DspTap and asserting a minimum in the engines
  is contained; releasing DspTap and SampleRateTap together couples TapTools' and MuTap's
  cadence to this tree's. Recommendation: tag independently, assert the minimum, no
  coupled cadence.
- **Q5. `-fno-exceptions`.** Does any consumer build with it today? A3 is recommended
  regardless; the answer sets its priority inside Block A.
- **Q6. The host ratchet.** A hard gate on callgrind `Ir` (deterministic enough on the
  runners), or an unguarded trend with the channel-parallel path documented as ungated?
  Recommendation: hard gate, same ±3 %.

## 8. Stays deferred, on purpose

Multistage decomposition, minimum-phase economy, the IIR pre-filter and the FFT offline
path change the output contract and wait for a consumer (`bridge/PLAN.md` §7). Coefficient
baking does not change the output but would move the M33/Hexagon fixed-point counts
without touching the audio path, and stays un-pulled. The `bridge` C ABI above K = 0 waits
for a consumer. Embedded channel-parallel dots wait for a DSP budget that demands them.
The HOA channel-count validation was deferred to the embedded campaign and is a Block B
item if a 16-channel fixture is on the bench, else it stays deferred. Hypothesis 5 (4-way
double accumulation) is bit-changing and superseded for N ≥ 4. `pdm` and `varispeed`
engines, a 4th-band stage and 352.8 kHz wait for a consumer (`PLAN.md`, `rational/PLAN.md`
R3). The HiFi ISS Xtensa lane stays commented out until a self-hosted runner exists. The
F25 banner template is a TapHouse PR.
