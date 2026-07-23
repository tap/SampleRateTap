# RatioTap — Plan

This is the authoritative plan for RatioTap v0.1. It supersedes the design
brief in [HANDOFF.md](HANDOFF.md) where the two disagree (the deltas are
listed in that file's status preamble). The DSP reference material in the
handoff doc (§3–§6) remains current.

---

## 1. Charter

**RatioTap converts between 44.1 kHz and 48 kHz, synchronously, as fast as
possible.** One rational ratio pair (160/147 up, 147/160 down), one clock,
and the entire optimization budget spent on exactly that.

The scope is deliberately boxed in, and the boundaries are identity, not
policy:

- **No other ratios.** Not 2:1, not 96→44.1, not arbitrary L/M. Generalized
  rational machinery may exist as *internal scaffolding* where it costs
  nothing, but the public surface is 44.1↔48 and the optimization work
  (superblock codegen, baked tables, multistage) is allowed to hard-commit
  to L ∈ {147, 160}.
- **No asynchronous conversion.** If the two ends of your signal chain run
  on different crystals — even at nominally 44.1-vs-48 — that is
  [SampleRateTap](https://github.com/tap/SampleRateTap)'s near-unity ASRC
  problem, reached by *composition* (see §5, `bluetooth_bridge`). Which
  engine applies is a property of the clock topology, not the number.
  RatioTap's API makes the caller state this by choosing a type; nothing is
  ever inferred from a float ratio.
- **Speed-first.** Where quality-vs-speed trades exist, the default profile
  takes the speed side of any trade that is inaudible (see §4, `economy`),
  and the direction is a compile-time parameter so the hot loop can
  specialize completely.

## 2. Position in the Tap family

```
                    ┌────────────────────────────┐
                    │           DspTap           │  shared substrate (submodule)
                    │  kaiser design · sample    │
                    │  traits (float/Q15/Q31) ·  │
                    │  FIR dot kernels · row-sum │
                    │  quantization · analysis   │
                    └──────┬──────────────┬──────┘
                           │              │
              ┌────────────┴───┐   ┌──────┴─────────┐
              │ SampleRateTap  │   │    RatioTap    │
              │ async, near-   │   │ sync, 44.1↔48, │
              │ unity, servo   │   │ speed-first    │
              └────────────┬───┘   └──────┬─────────┘
                           │              │
                           └──── test-only│dependency:
                                cross-validation (§6)
```

- **Production dependency: DspTap only**, pinned as `submodules/dsptap` and
  linked as the `tap::dsp` INTERFACE target — the same pattern as TapTools
  and MuTap. Changes to shared code land in DspTap first; RatioTap bumps its
  pin (DspTap's documented release flow).
- **SampleRateTap is a test-only dependency** (FetchContent in the test
  tree), used solely for the golden cross-validation in §6. It never appears
  in the shipped headers.
- **TapHouse** provides style/tooling (`.clang-format`, `.clang-tidy`,
  `pre-commit`, drift checks, SessionStart hook) from day one.

## 3. Architecture decisions (settled)

| Decision | Choice |
|---|---|
| Namespace | `tap::ratio` |
| Include path | `include/tap/ratio/…` (the DspTap-style convention: path mirrors namespace) |
| Direction | **Compile-time** template parameter; two concrete instantiations. Working names: `basic_converter<Sample, direction>` with `direction::up_to_48k` / `direction::down_to_44k1` and aliases per sample type (bikeshed open, see §9) |
| Sample types | `float`, Q15 (`int16_t`), Q31 (`int32_t`) via `tap::dsp::sample_traits`. **Q15 is the flagship embedded profile** (Bluetooth-adjacent M33/M55 deployments) |
| Coefficient tables | Phase-major (each output = one contiguous dot product), exact L = 147/160 phases, no inter-phase interpolation, no extra wrap row |
| Schedule | Precomputed L-entry `(phase, input_advance)` table; advance ∈ {0,1} up, {1,2} down |
| Prototype design | Two independent prototypes (directions are asymmetric; do not transpose one). Designed **at construction time** in double via `tap::dsp` kaiser math, per the family's runtime-design philosophy. Row-sum-preserving quantization for fixed-point tables via the shared DspTap utility |
| Hot loop | `tap::dsp` dot kernels: `dot_row` (planar / SMLALD path) and `dot_rows_frame_major` (channel-parallel) — inherited, not rebuilt |
| Channels | Runtime count, one converter instance per stream; per-frame coefficient row shared across channels |
| RT contract | Constructor allocates and designs (may throw); processing is `noexcept`, lock-free, allocation-free |
| Repeatability | Bit-exact: same input → same output, per sample type, on every platform (integer paths exactly; float path via fixed accumulation order) |

### API surface (v0.1)

Both call shapes, because the Bluetooth composition (§5) needs one on each
side of the ASRC:

- **Push-transform**: `process(const S* in, size_t in_frames, S* out)` →
  frames produced. For producer-side placement and file processing.
- **Pull-with-callback**: `pull(S* out, size_t out_frames, PopFn&&)` —
  produce exactly N output frames, drawing input as needed (the
  `fractional_resampler` PopFn pattern). For consumer-side placement.
- **`frames_needed(size_t out_frames)`** — exact input requirement from the
  current schedule position. Deterministic; a capability the async engine
  cannot offer and the sync engine gets for free.
- **`flush(S* out)`** — end-of-stream: drain the filter tail (group-delay
  padding with zeros), return frames produced. Needed by the file-domain
  path; a live stream never calls it.
- **`latency_frames()`** — constant, exact (linear-phase group delay), in
  input frames.
- `reset()` — return to initial schedule position and cleared history.

## 4. Profiles

Two quality tiers behind one design path, named in the family vocabulary:

| Profile | Stopband | Passband edge | Taps/phase (=MACs/out) down / up | Storage f32 down / up | Role |
|---|---|---|---|---|---|
| `economy()` — **default** | 70 dB | 19 kHz | **78 / 44** | 44.8 / 27.5 KiB | The speed-first default. All alias products land above 20 kHz at ≤ −71 dBFS — arithmetically confined to the ultrasonic band (see HANDOFF §4) |
| `transparent()` | 120 dB | 20 kHz | **184 / 96** | 105.7 / 60.0 KiB | Pristine/offline tier |

`economy` as default is a deliberate positioning choice consistent with the
speed-first charter; the README must state the reasoning (the §4 argument:
nothing *can* fold below 20.1 kHz going down; images land ≥ 22.05 kHz going
up) rather than just the number, and the program-weighted measurement style
from SampleRateTap's `economy` preset applies here too.

Numbers pinned by the M2 design spike (`notebooks/design_spike.ipynb`,
executed and committed; enforced in CI by `test_design.cpp`): taps are the
minimal even counts meeting the stopband with ≥ 1 dB margin. Measured
worst-case stopband on the shipping designs: economy −72.1 dB (down) /
−72.8 dB (up); transparent −121.7 dB (both). Passband ripple ±0.003 dB
(economy) / ±0.00001 dB (transparent). Q15 tables halve the storage. The
designs additionally normalize every polyphase branch's DC sum to exactly
1.0 (kills fs_out/L-harmonic spurs from DC/LF energy; lets fixed-point
row-sum quantization land on format unity exactly).

## 5. The async composition (`bluetooth_bridge`)

The documented answer to "I need 44.1↔48 across independent clocks"
(Bluetooth chip on its own crystal being the motivating case):

```
receive:  BT codec (44.1 @ BT clock) → RatioTap 44.1→48 → ASRC push │ pull @ local 48k
send:     local 48k → ASRC push │ pull @ BT pace → RatioTap 48→44.1 → BT codec
```

RatioTap is clock-agnostic (a pure sample-count transformer), so the ASRC
sees nominal-48k-vs-48k with the BT crystal's ppm offset passed through
unchanged (ppm is dimensionless) — exactly its designed near-unity regime.
`examples/bluetooth_bridge.cpp` ships both directions and is the reason the
API carries both call shapes. Neither package grows scope: the capability
lives at the seam.

## 6. Test strategy — three independent legs

1. **Contract/unit tests** (GoogleTest, typed over sample types, per family
   convention): schedule correctness with **exhaustive coverage of all 147
   and 160 phases**, `frames_needed` exactness, flush/latency contracts,
   bit-exact repeatability, channel independence, alias/image measurements
   via the shared `tap::dsp` analysis headers. Acceptance numbers from §8.
2. **Independent golden reference**: committed reference vectors generated
   offline (scipy `resample_poly` with the same prototype, plus a
   soxr/libsamplerate sanity comparison in the notebook). This leg exists so
   correctness never rests solely on agreement between two things we built
   ourselves — the shared kaiser code would otherwise be a common-mode
   failure.
3. **Cross-validation against SampleRateTap** (test-only dependency): drive
   `fractional_resampler` with **pinned eps = L/M − 1** (no servo — the
   near-unity servo cannot and need not acquire an 8% offset), identical
   input, exhaustive over all phases; assert agreement within the ASRC's
   phase-table interpolation floor (its documented ≈ −12 dB per doubling of
   L inter-phase residual). This is the handoff doc's golden-reference idea,
   relocated one layer down where it actually works.

Verification layer per family convention: `tools/capi/` C ABI +
`notebooks/` ctypes bridge, with the design-spike notebook committed
executed (it measures the shipping C++, not a Python re-implementation).

## 7. Milestones

- **M0 — substrate extraction** (two PRs in other repos; outlines in
  Appendices A and B). DspTap gains the shared FIR substrate; SampleRateTap
  adopts it via submodule with re-export shims. Gate: both repos' CI green,
  SampleRateTap icount baselines unchanged (proving the move is free).
- **M1 — skeleton.** CMake (`tap::ratio` INTERFACE target), TapHouse
  adoption, DspTap submodule, host CI (Linux/macOS/Windows + ASan/UBSan),
  README carrying the §1 charter, LICENSE (MIT).
- **M2 — design spike + tables.** The notebook that designs both prototypes
  at both profiles and *pins the numbers* (taps/phase, storage, worst-case
  alias level, passband ripple); then the coefficient-table and schedule
  classes with contract tests. Acceptance numbers in §8 get their final
  values here.
- **M3 — float engine correct.** Both directions, both call shapes,
  exhaustive-phase tests, reference-vector leg (§6.2) passing, alias
  acceptance met on `economy`.
- **M4 — fixed point.** Q15/Q31 datapaths via the shared traits;
  cross-precision agreement pinned (Q31 at format limit vs float; Q15
  format-limited).
- **M5 — cross-validation.** The §6.3 leg wired with SampleRateTap as
  test-only dependency.
- **M6 — composition + verification layer.** `bluetooth_bridge` example,
  flush/file path, C ABI + notebook committed executed.
- **M7+ — optimization campaign**, strictly measured, one lever per change,
  in the revised order: superblock codegen (elevated by the speed-first
  charter) → baked/committed tables if codegen wants them → polyphase
  symmetry storage halving → multistage decomposition (storage lever for
  embedded) → minimum-phase `economy` variant (latency) → IIR pre-filter →
  FFT offline path. Embedded CI matrix (M33/M55/Hexagon under QEMU) and
  icount gating land at the top of this campaign, before the first lever, so
  every optimization is measured the family way. Levers already banked:
  spec relaxation (the `economy` default) and channel vectorization
  (inherited kernels).

v0.1 ships at M6. Nothing in M7+ blocks it.

## 8. Acceptance criteria (v0.1)

All numbers pinned (M2 design spike, 2026-07-23).

- `economy`, both directions: every spurious product ≥ **71 dB below the
  source content** (design floors: −72.1 dB down, −72.8 dB up). Two species,
  measured separately (M3, `test_converter.cpp`): decimation *aliases* of
  signal content are additionally **confined above 20 kHz by arithmetic**;
  upsampling *image leakage* folds in-band but is bounded by the stopband
  (worst measured in-band product: −85 dBFS for a −6 dBFS stopband-adjacent
  tone; a 997 Hz tone measures ~89 dB SNR against its imaging floor). The
  original "nothing measurable below 20 kHz" phrasing overstated economy —
  that claim holds at the *transparent* tier; economy's honest in-band bound
  is the stopband. Deepening exactly these in-band images for low-frequency
  program energy is the k·fs image-zeros lever (M7, SampleRateTap's
  `design_prototype_compensated`).
- `transparent`, both directions: alias/image products ≤ **−121 dB**
  (design floors −121.7 dB); passband flat to 20 kHz within ±0.00001 dB.
- Exhaustive phase coverage in tests — all 147 and all 160 phases, not
  statistical sampling (began in M2: `test_phase_table.cpp` holds the
  row-sum and DC guarantees for every phase of all four tables).
- Cross-validation agreement within the ASRC interpolation floor (§6.3).
- Bit-exact repeatability per §3; Q15/Q31 parity bounds pinned.
- RT contract: processing paths `noexcept`, allocation-free (verified under
  sanitizers); constructor-only design confirmed < 10 ms class.
- Latency (`latency_frames()`, linear-phase group delay in input samples):
  economy **39** down (0.81 ms) / **22** up (0.50 ms); transparent **92**
  down (1.92 ms) / **48** up (1.09 ms) — inside the plan's 45–90-sample
  budget at the transparent tier, well under it at economy.

## 9. Open items

- **Naming bikeshed** (§3): final alias names for the four
  direction × common-type instantiations. Decide before M3 makes them
  public.
- **SampleRateTap include-path rename** (`include/srt/` →
  `include/tap/samplerate/`): agreed direction, separate PR in that repo,
  same era as Appendix B (shared anchor-repointing work), not a RatioTap
  blocker.
- **Book/white-paper chapter** ("the degenerate case"): explicitly deferred
  past v0.1. Code is written anchor-friendly (`ANCHOR:` comments on the
  load-bearing excerpts) from day one so the chapter can be added without
  touching the code.
- **AmbiTap/HOA channel-count validation** (the HVX 16-channel story):
  deferred to the M7+ embedded campaign.

---

## Appendix A — M0 PR outline: DspTap "shared FIR substrate"

*Lands first. Follows DspTap's "Adding a primitive" checklist for each
asset. Everything moves from SampleRateTap `include/srt/` /
`tests/`; provenance noted per file (the DspTap origin-story pattern).*

**New headers under `include/tap/dsp/`:**

1. `kaiser.h` — `bessel_i0`, `kaiser_beta`, `estimate_taps`, `sinc`,
   `design_prototype`, `design_prototype_compensated` (from
   `srt/detail/kaiser.h`, namespace → `tap::dsp`, keeping the
   runtime-design design-note docstring). Contract tests ported from
   `test_kaiser.cpp`.
2. `sample_traits.h` — the **format-core stratum only**: `coeff`/`accum`
   types, Q-format ladder (Q1.14/Q29→Q15, Q1.30/Q45→Q31 with the pre-shift
   rationale comments), `make_coeff`, `k_coeff_scale`, `mac`, `finalize`,
   `round_sat`/`clamp_sat`, `silence`, and a concept covering exactly what
   the dot kernels require. **The blend stratum does not move** (it is
   mu-interpolation machinery, coupled to the ASRC's Q0.64 phase
   accumulator; it stays in SampleRateTap as a refinement). Contract tests
   ported from `test_fixed_point.cpp`, restated as pinned numeric contracts
   (Q formats, rounding mode, saturation, cross-precision bounds).
3. `fir_kernels.h` — `dot_row` (with the SMLALD Q15 path and its
   `__ARM_FEATURE_DSP`/MVE gating), `dot_tile_frame_major`,
   `dot_rows_frame_major`, the restrict macro. Macro prefix `SRT_` →
   `TAP_DSP_` (incl. `SRT_CP_MIN_CHANNELS` → `TAP_DSP_CP_MIN_CHANNELS`).
   Bit-exactness comments travel with the code. Kernel parity tests (planar
   vs channel-parallel bit-exact per type) extracted from the SampleRateTap
   suite.
4. `quantize.h` — row-sum-preserving quantization (largest-remainder),
   refactored out of `polyphase_filter_bank`'s constructor into a
   free function over (double row, `sample_traits<S>`) with the
   RBJ-attribution comment. New focused tests (every row sums to
   `llround(exact × scale)`).
5. `analysis/sine_analysis.h`, `analysis/multitone_analysis.h` — the
   measurement instruments from `tests/support/`, generalized out of the
   `tap::samplerate` test namespace.

**Documentation:**

- README: one section per asset (bump the primitive count); the traits
  section states the **fixed-point roadmap**: Q15/Q31 are first-class
  embedded profiles, expected deployments include M33/M55-class eurorack
  and pedal targets (TapTools) and the Bluetooth-adjacent RatioTap path;
  per-primitive fixed-point adoption is opt-in and each adoption is its own
  documented Q-format design (no wrapper classes — raw sample types +
  traits is the family contract, and the rationale goes in the header
  docstring).
- CLAUDE.md discipline clause extended: "double is the golden model;
  float32 is the embedded profile; **Q15/Q31 are format-limited embedded
  profiles** with contracts pinned like everything else."

**Housekeeping in the same PR:** delete the stray committed `a.out` at the
repo root.

**Non-goals:** no fixed-point variants of the existing four primitives; no
blend/interpolation machinery; no polyphase bank (engine-specific, stays
put).

## Appendix B — M0 PR outline: SampleRateTap adopts DspTap

*Lands second, pinned at Appendix A's merged tree. The MuTap `fft.h`
re-export shim is the template throughout. Behavior change: none — proven
by the gates below.*

1. **Submodule**: add `submodules/dsptap`, link `tap::dsp` into the
   `SampleRateTap::SampleRateTap` INTERFACE target. (The TapHouse
   SessionStart hook already runs submodule init, so web sessions keep
   working unchanged.)
2. **Shims / refactors**:
   - `srt/detail/kaiser.h` → re-export shim (`tap::samplerate::detail`
     using-declarations for the design functions; historical include path
     keeps compiling).
   - `srt/sample_traits.h` → keeps its name and full interface, now
     implemented as a refinement of `tap::dsp::sample_traits` (core stratum
     inherited/aliased; blend stratum defined here; the `sample_type`
     concept refines the DspTap core concept).
   - `srt/polyphase_filter.h` → dot kernels consumed from
     `tap::dsp` via using-declarations; the bank constructor calls the
     shared `quantize.h` utility; the mu-blend functions and the bank stay.
   - `tests/support/` analysis headers → thin includes of the
     `tap::dsp::analysis` versions (or direct test-side migration).
3. **Book anchor repointing**: `kai_*`, the moved `st_*` subset, and the
   kernel `rs_dot_*`/`opt_*` anchors now include from
   `submodules/dsptap/include/tap/dsp/…` paths; blend-stratum and
   engine anchors unchanged. The book CI's stale-anchor gate verifies
   completeness.
4. **Gates proving the move is free**: full test suite green unchanged
   (bit-exact outputs); **icount baselines unchanged within the existing
   ±3% CI gate on all three embedded targets** — the strongest available
   proof that relocation cost nothing on the hot path; book builds clean.

**Follow-up PR, same era, not part of M0**: `include/srt/` →
`include/tap/samplerate/` rename with forwarding headers at the old paths
(deprecation window), completing the `srt` → `tap::samplerate` namespace
migration. Kept separate so the M0 diff stays reviewable.

---

*License: MIT, consistent with the family. Docs venue:
timothy.place/RatioTap once there is something to document.*
