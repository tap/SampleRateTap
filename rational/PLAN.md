# `rational` — plan

The plan of the `tap::sr::rational` engine: synchronous small-factor L/M
conversion *within* a rate family, built the way `bridge` is built. The
family plan, [`../PLAN.md`](../PLAN.md), settles what is shared: the engine
names and charters (section 2, the `rational` row at line 61), the coverage
rule (2.1), the dependency rule (4.2) and the decisions D1–D16. This file is
the engine's charter, settled decisions, the generated 14 × 14 coverage
matrix, layout, test strategy, milestones, non-goals and risks.

| Version | Date | Status |
|---|---|---|
| v0.1 | 2026-10-01 | draft, for review |
| v0.2 | 2026-10-01 | section 9's ten questions decided by the maintainer (recorded in section 9 and in the decisions they touch) |
| v0.3 | 2026-10-01 | M0 landed (DspTap `nyquist.h` and `chain.h`, tap/DspTap#48); 2.3's half- and third-band counts are measured by the shipped designer, the harris estimates kept beside them |
| v0.4 | 2026-10-01 | M1 landed: the `rational/` tree, `tap::sr::rational`, `ratio<L, M>` and `ratio_traits` (R1), `profile` and `design_stage<R>` (R5, R4), the family's dependency-rule tests over three engines, the compile-fail charter test; the M2 table's half- and third-band columns are re-measured through the engine's own design |
| v0.6 | 2026-10-02 | M3 landed: `stage.h` (`basic_stage<S, R>`: the L-phase schedule machine for interpolators and mixed ratios with trimmed phase rows, the M-branch commutator for decimators summing its nonzero branches under one finalize, bridge's call shapes, the exact-rational latency), `converter.h` (the aliases), the committed scipy vectors for every ratio of the vocabulary, the M3 battery; the symmetry-halved table of section 4's layout is deferred to the codegen levers after M6's baselines, as bridge did (M7 lever 3) |
| v0.8 | 2026-10-02 | M5 landed: the fixed-point profiles measured and pinned for every ratio at every profile (`tests/test_fixed_point.cpp`): exact-unity rows, bit-pinned Q15 / Q31 tables, the structural zeros out of the dot, Q31 a decade under float's floor, Q15's floor and attained stopband stated per stage, saturation without wrap, full-scale DC exact; two honest limits recorded (a Q15 decimator's stopband loses about 20 log10 M; a mixed ratio going down multiplies its band's zeros), section 6 |
| v0.7 | 2026-10-02 | M4 landed: `chain.h` (`basic_chain<S, R...>` over DspTap's `chain<>`, each stage designed at its **design divisor** — its lower rate over the chain's lowest, 3.1 — and the 20 named multi-stage chains), `tools/coverage/matrix.py` committed and re-run on the pinned lengths with the per-stage relaxation pins (`design.h`'s relaxation tables, verified by `test_design.cpp`), section 3 regenerated from measured counts (35 of 182 rows changed chain, the ledger in section 8 S1), `test_matrix.cpp` over all 182 rows (every row measured, none skipped: 2.2 had landed), DspTap's `chain<>::flush` (tap/DspTap#52) |
| v0.5 | 2026-10-02 | M2 landed: the design spike (`notebooks/design_spike.ipynb`, executed) pins N for every band ∈ {2, 3, 4, 6, 8} × profile on a 16384-point grid, the mixed ratios' taps per phase, measured worst stopband and ripple; `profile` carries the pins and `test_design.cpp` enforces them; the M2 table below is filled from measurements. Finding: the designer's 1024-point default grid under-pins two rows (tap/DspTap#50 made the search grid a parameter) |
| v0.9 | 2026-10-02 | M6 landed: the C ABI (`capi/`, `libtap_sr_rational_capi`: the 28 named within-family chains as stable `TAP_SR_RATIONAL_*` constants, one stage of the vocabulary at a stated design divisor for chains through `bridge`, exact accounting, latency and MACs as rationals; exactly sixteen exported symbols), the ctypes binding and `matrix.ipynb` executed through the C ABIs (all 728 row × profile pins reproduced), the icount ratchet (twelve workloads, `RATIONAL_ICOUNT_DONE`, baselines on M33 / M55 / Hexagon, gated two-sided at ±3 % in CI), family version 0.5.0 |
| v0.10 | 2026-10-02 | The codegen levers after M6, each measured (section 6): the Helium Q15 dot (tap/DspTap#53, shipped: the M55 Q15 workloads −21 … −48 %); the Q15 decimators quantized per branch with the 1 / M in the single rounding (tap/DspTap#54's `finalize_divided`, shipped: ↓6 / ↓8 attain −71.5 / −71.7 dB at `economy`, the Q15 decimator's table is its band's interpolator table); the sparse rows for the mixed ratios going down (built, measured, declined: Q15 +22 … +60 %); the symmetry-halved table (declined: memory only, 1.6 KB at most). The 2/3 workloads join the ratchet |
| v0.11 | 2026-10-06 | After the levers: the Q15 mixed ratios going down hold their rows at a power-of-two gain (2/3 and 3/4: 2, 3/8: 4), shifted out in the rounding, so every Q15 stage attains the 70 dB tier (−70.0 … −72.4 dB, from −68.2 … −69.7); DspTap pinned at its `main` with tap/DspTap#55 (`decimate.h`'s Q15 tables at M·h, which this engine's tests use only in float) |
| v0.12 | 2026-10-07 | The C ABI carries Q15 and Q31 beside float (`tap_sr_rational_create_format`, `_create_stage_format`, `_format`, `_process_q15` / `_q31`, `_flush_q15` / `_q31`; 23 exported symbols), each format pinned bit for bit against `basic_chain`; a call in another format than the converter's is refused |
| v0.13 | 2026-10-08 | The host-vs-target table difference of v0.11 traced to its cause and fixed at the root (tap/DspTap#56): `nyquist.h`'s per-branch normalization summed mirror branches in opposite orders, so their gains could differ by an ulp (equal under glibc, unequal under newlib for the third-band m = 8 design) and a mirrored pair in one quantized row tied only by luck; the designer now copies its second half from the first, bit for bit. The 2^-34 snap of `stage.h` is removed; every Q15 pin holds, and two Q31 pins move on the host (↓6 `balanced`, 3/4 `economy`: one LSB between the members of a mirror pair, the two designs where glibc's gains differed), the tables now identical on the host and the Cortex-M legs |
| v0.14 | 2026-10-08 | The siblings adopt this ABI's shape: `async`'s and `bridge`'s C ABIs carry Q15 and Q31 the same way (`create_format`, `_format`, the `_q15` / `_q31` entry points, hidden visibility, each format pinned bit for bit against the engine's `basic_converter`); the family keeps fixed point at every engine's C boundary rather than moving to a separate uniform ABI |
| v0.15 | 2026-10-10 | The thirteen streaming ratchet rows re-recorded on all three targets with their reason (section 6: `ratio23_q15_eco` +1.93 % on the M55 is v0.11's stated move, the rest a sub-0.02 % construction-side residue from v0.13's designer and tap/DspTap#57), the first in-band re-record of this engine; `bridge` and `async` re-measure exact on every target |
| v0.16 | 2026-11-18 | A13b: the channel count is the type's (R17, the family's D18): `basic_stage<S, Ch, R>` over `stage_base<S, R>`, `basic_chain<S, Ch, R...>`, the aliases `converter<Ch, R>` and the named chains count-first, zero channels a compile error; the C ABI one and two channels native and any other count a fan-out of one-channel converters; DspTap pinned at `v0.2.0` (the chain takes its count from its stages). The ratchet re-recorded on every target and the ceilings on the three, with the M55's `ratio23_q15_eco` gated at the midpoint of its two builds (section 6, the A13 record) |
| v0.19 | 2026-11-21 | X-a4 (`docs/X4_X8_PLAN.md` X4): a chain per attenuation tier — the 70 dB tier's selected at `economy`, the 120 dB tier's at `transparent` (R3, 3.1); 25 rows split (17 within the families, 8 across), seven chains serve only the 120 dB tier, three of them new (`up2_ratio3over2`, `up2_up6_up4`, `down4_down6_down2`, C values 40 to 42, `CHAIN_END` 43, 33 live values); the generated row format holds both chains, the matrix test, the binding, `matrix.ipynb` and the coverage figure read it; the relaxation tables unchanged; the 6th- and 8th-band designs at divisor 2 at `transparent` bit-pinned; the ratchet's `down8_down2_float_tr` workload (the 1/16 chain at `transparent`), every existing count and ceiling unchanged (section 6, the X-a4 record) |
| v0.18 | 2026-11-21 | X-a3 (`docs/X4_X8_PLAN.md` X3): decision 6 amended, the 4th-band stage admitted (`up4` / `down4` in `ratio.h`, the C ABI's `create_stage` at 4/1 and 1/4); X-a2's frozen selection removed and R3's restored, 46 rows moved at `economy` (30 within, 16 across; S1), the named chains twelve added and fourteen gone (ten retired under D7, four kept for X-a4), the C ABI's values stable (`CHAIN_END` 40 for the retired `CHAIN_COUNT`, retired values refused), band 4's tables bit-pinned at every matrix divisor, the scipy vectors with a 4th-band stage, the ratchet's `down4_down2_q15_eco`, `callmax_down4_*` and host `down4_*` workloads and the 4th-band probes' ceilings (section 6, the X-a3 record) |
| v0.17 | 2026-11-20 | X17 (`docs/X4_X8_PLAN.md`, X-a2): the designer's Kaiser β for A + 4 dB, accepted at A + 1 dB as before (`k_design_beta_offset_db`, no DspTap change); x = 4 chosen over 3 on the measured Q15 / Q31 floors and ripple; every pin, relaxation table, Q15 / Q31 table and scipy vector re-pinned, the matrix re-costed with its chains frozen for one PR (R3 suspended until X-a3, 3.1), every ratchet row and the probes' ceilings re-recorded with the reason (section 6, "The designer's β, X17") |

**Status.** This draft is the reviewed-plan deliverable that the family
plan's "Next — `rational` (separate plan)" entry asks for
(`../PLAN.md:1001-1006`: the L-th-band design math and `chain<>` under D12 in
DspTap, the engine, and the generated matrix under 2.1's rule). The family
plan sequences the 2.2 follow-up — `bridge`'s `ratio_traits` rate-scale `k`,
so that 88.2 ↔ 96 and 176.4 ↔ 192 can be expressed (`../PLAN.md:124-152`,
"sequenced before `rational`") — ahead of this engine's implementation.
This draft does not change that order: every milestone below that composes
with `bridge` at k > 0 is marked as waiting for it. M0 is
implemented (v0.3: DspTap's `nyquist.h` and `chain.h`), M1 (v0.4: the
tree, the ratio types, the profiles and the stage design), M2 (v0.5: the
pinned lengths per band and profile, measured), M3 (v0.6: the single
stages, every ratio of the vocabulary, pinned against scipy), M4 (v0.7:
the chains and the matrix, every row of section 3 regenerated from the
pinned lengths and pinned by `test_matrix.cpp`) and M5 (v0.8: the
fixed-point profiles, measured per stage). The numbers still marked
*est.* — 2.1's arithmetic and 3.2's worked cases — are Kaiser/harris
length estimates (`tap::dsp::estimate_taps`, `kaiser.h:77-85`) kept as the
derivation of the rules; the measured chains and counts are 3.3–3.6, and
where a worked case chose differently on the pinned lengths, 3.2 says so. v0.2
records the maintainer's decisions on v0.1's ten open questions (section
9, each "as recommended") in the R-rows they touch; no question remains
open.

---

## 1. Charter

**`rational` converts within a rate family, synchronously, as fast as
possible.** Ratios L/M with L, M ∈ {2^a · 3^b}, gcd(L, M) = 1, realised as
chains of Nyquist (L-th-band) stages. It converts the number, never the
clock (D5, `../PLAN.md:40`).

The boundaries are identity, not policy, as `bridge`'s are
(`../bridge/PLAN.md` §1):

- **Within a family only.** The 14 supported rates (`../PLAN.md:100-101`)
  fall into two families, and `rational` serves pairs inside one family:
  - the 48 kHz family: 8, 12, 16, 24, 32, 48, 96, 192, 384 kHz
    (8 kHz · 2^a · 3^b with b ≤ 1);
  - the 44.1 kHz family: 11.025, 22.05, 44.1, 88.2, 176.4 kHz
    (11.025 kHz · 2^a).

  The single-stage ratios these need are, within the 48 family, the
  integer factors 2, 3, 4, 6, 8 and their reciprocals (4 since X-a3: decision
  6 as amended 2026-11-21, R3; the matrix's chain by 4 stays two half-band
  stages, cheaper by MACs), and the mixed
  ratios 3/2, 4/3, 8/3 and their reciprocals (every ratio between two of
  the nine rates reduces to 2^x · 3^y with x ∈ [−5, 5] and y ∈ {−1, 0, 1};
  section 3.3 lists all 26); within the 44.1 family, 2, 4, 8, 16 and
  their reciprocals (3.4). **Crossing the families is `bridge`** (147/160
  at k = 0, 1, 2), never this engine: a 44.1-family rate is reached from a
  48-family rate only by a chain that passes through `bridge`. **Absorbing
  a clock is `async`**, reached by composition only.
- **Never routed to by rate (D12, `../PLAN.md:47`).** The caller declares
  the topology by choosing a type: `ratio<L, M>` names the number,
  `chain<...>` names the stages. There is no `(in_hz, out_hz)` lookup,
  here or in the C ABI (whose enumerators name chains, not rates; 4.4).
  The coverage matrix (section 3) documents chains; it dispatches nothing.
- **Speed-first, like `bridge`.** Every inaudible quality-vs-speed trade
  goes to speed in the default profile; the ratio is a compile-time type so
  every stage's trip count and schedule are compile-time facts; the stage
  factoring (R3) is chosen by MACs.
- **Correctness-first gating.** Exhaustive phase coverage of every stage,
  an independent golden reference (scipy vectors), and the matrix test
  that pins (f_pass, A, δ), MACs and latency per pair, before any
  optimization lever.
- **Does not absorb DspTap's `decimate.h`** (`../PLAN.md:61,70`). That
  primitive keeps its own contract (asymmetric transition, no zero taps,
  mono, 16 kHz output; `decimate.h:14-52`) for MuTap. `rational` shares the
  substrate under it — `kaiser.h`, `sample_traits.h`, `fir_kernels.h`,
  `quantize.h` — and adds the L-th-band designer next to it (R6).

## 2. Settled decisions

| # | Decision | Rationale |
|---|---|---|
| R1 | **Ratio as a compile-time type**: `ratio<L, M>` with `static_assert`s that L and M are of the form 2^a · 3^b, gcd(L, M) = 1, and L ≠ M; `ratio_traits<ratio<L, M>>` carries L, M, the composite rate factor and the band index as `constexpr` numbers, after `bridge`'s `direction` / `ratio_traits<D, K>` (`design.h:21-73`) and `decimate_traits<M>`'s charter `static_assert` (`decimate.h:107`) | The charter is a `static_assert`, not a runtime check. Every schedule, trip count and table size is known at compile time, which is what lets the hot loop specialize (`converter.h:114-129`) |
| R2 | **A stage is one Nyquist filter.** `stage<ratio<L, M>>`: an L-th-band interpolator (M = 1), an M-th-band decimator (L = 1), or, for a mixed ratio (L, M > 1), one polyphase stage at the composite rate L · f_in with the cutoff at the lower rate's Nyquist (an L-th band when L > M, an M-th band when M > L) driven by `bridge`'s `(phase, advance)` schedule (`schedule.h:22-45`) | One filter per stage keeps the exhaustive-phase discipline per stage (every stage has L phases, each visited once per L outputs) and the alignment contract one sentence long (2.5) |
| R3 | **Stage factoring by MACs, then by stage count**: an integer factor K factors into a chain of stages from {2, 3, 4, 6, 8} (4 since X-a3), with the by-2 stage at the low-rate end and the larger factors at the high-rate end; a mixed ratio with both L, M > 1 is one stage (R2); the chain per rate pair is the MAC-minimal factorization under the harris estimate (section 3, generated) **unless a factorization with strictly fewer stages is within 10 % of it (*est.*) — 10 % of the rational stages' MACs per output: the `bridge` stage is identical in both candidates (same k, same direction) and excluded from the comparison — in which case the chain with the fewest stages within 10 % is chosen** (decision 2). **The rule runs once per attenuation tier since X-a4** (X4, `docs/X4_X8_PLAN.md`): at `economy` for the 70 dB tier's chain, which a row runs at `super_economy`, `economy` and `balanced`, and at `transparent` for the 120 dB tier's; they differ in 25 rows (3.1). **The 4th-band stage is in the vocabulary since X-a3** (`up4` / `down4`, `stage<ratio<4, 1>>` and `<1, 4>`; decision 6 as amended 2026-11-21, `docs/X4_X8_PLAN.md` X3 and X17), and the generator chooses it wherever this rule does; ↑4 / ↓4 themselves stay two half-bands, which cost less (10.8 against 15.25 MACs per output going up, 43 against 61 going down, at `economy`); 4/3 and 3/4 stay, as mixed stages. Pinned by the matrix test | 2.1 shows the arithmetic: ↓4 as ↓2 · ↓2 costs 37 MACs per output against 55 for one 4th-band stage (−33 %, *est.*), while 48 → 32 as one 2/3 stage costs 26 against 52 for ↑2 · ↓3. Fewer stages mean less latency, less state and fewer test rows, and a few per cent is below the harris estimate's own error until M2 pins: 3/1 is one ↑3 stage (12.0) rather than 3/2 · ↑2 (11.0), and 16 → 44.1 is ↑3 · `bridge`↓ (71.1) rather than 3/2 · ↑2 · `bridge`↓ (70.0). On X17's pins the 4th-band stage wins 46 rows at `economy` (X-a3; 1/8 is ↓4 · ↓2 at 71 MACs per output, against 79 for three half-bands and 87 for v0.7's ↓3 · 3/4 · ↓2), and a tie between it and two half-bands goes to fewer stages and less latency |
| R4 | **Nyquist (L-th-band) Kaiser-windowed-sinc design**: cutoff at π/L of the stage's higher rate, odd length N = 2mL − 1, so h[c ± kL] = 0 exactly for k ≥ 1 (c the centre), the zeros written as exact zeros by the designer (2.2) | The zeros are structural: the half-band ↑2/↓2 stage computes about half the MACs of a generic FIR of the same length, and branch 0 of every ↑L stage is a pure copy (zero MACs). The symmetric transition this forces is exactly the coverage rule's bound (2.1(a)) with equality, so the structure costs no design freedom |
| R5 | **Profiles reuse `bridge`'s vocabulary** — `super_economy`, `economy` (default), `balanced`, `transparent` — as (stopband A in dB, passband edge p as a fraction of the chain's lowest rate r_min): the same four edges `bridge` has at 48 kHz (16, 18, 19, 20 kHz, `design.h:86-89`), stated as fractions from day one, as the 2.2 follow-up makes `bridge`'s (`../PLAN.md:138-139`). Numbers pinned by the M2 design spike (2.3, section 6) | One vocabulary across the sync engines means a chain that composes `bridge` and `rational` at the same profile name has one (f_pass, A) throughout — the matrix test's pin is then the chain's, not a per-engine translation |
| R6 | **The design math and the composition helper land in DspTap first**: `tap/dsp/nyquist.h` (the L-th-band designer, its `m`-search and its property test) and `tap/dsp/chain.h` (`tap::dsp::chain<Stages...>` over a `sync_stage` concept: `process`, `outputs_for`, `frames_needed`, `flush`, `reset`, `latency`). The engine holds only its ratio types, stages, schedule and converter | Substrate discipline (`../CLAUDE.md`, "Shared code lands in DspTap first"). `chain<>` must live *below* both engines: a chain that composes a `bridge` converter with `rational` stages — every cross-family row of section 3 — can then be written by the caller without either engine naming the other (4.2). The family plan already places `chain<>` there (`../PLAN.md:47,1004`); **confirmed** (decision 4): M0 proves the helper on DspTap's own `decimate.h` first |
| R7 | **Alignment and latency contract** (2.5): zero-primed, causal, scipy `upfirdn`'s streaming prefix per stage (as `bridge`, `converter.h:51-56`, and `decimate.h:17-20`); group delay an integer number of samples at the stage's *higher* rate, (N − 1)/2; a chain's latency is the sum of its stages' delays converted to the chain's output rate, **reported as the exact rational** (`latency_output_frames()` returns numerator and denominator) plus `latency_seconds()` (decision 8); `outputs_for` and `frames_needed` compose stage by stage, exactly | Latency as a number, not a claim, per family convention (`../bridge/PLAN.md` §8, `decimate.h:181-182`). `bridge` keeps its `double` (`converter.h:237`) and may adopt the same form later without breaking |
| R8 | **Sample formats** double / float / Q15 / Q31 through `tap::dsp::sample_traits` (`sample_traits.h:107-266`), coefficient rows quantized with `quantize_row_preserving_sum` (`quantize.h:56-99`) after per-branch DC normalization, so every branch's DC gain is exactly 1 in every format; the zero taps are zero in every format and are never multiplied. Aliases `converter<ratio>` (float), `converter_q15`, `converter_q31`, and `double` as the golden model | DspTap's four-profile ladder (`submodules/dsptap/CLAUDE.md`, "Double is the golden model"); `bridge`'s per-branch normalization (`design.h:180-199`) and its reasons |
| R9 | **Real-time rules**: geometry fixed at construction, every buffer allocated and every filter designed there (may throw); `process`, `pull`, `flush`, `reset` are `noexcept`, lock-free and allocation-free; one stream per instance, channels share the coefficient row per frame | The family contract (`converter.h:58-61`, `decimate.h:51-52`) |
| R10 | **Bit-exact repeatability** per format and platform, as `bridge` (`../bridge/PLAN.md` §3): integer paths exactly, float through the fixed accumulation order of the `tap::dsp` kernels (`fir_kernels.h:77-79`); a chain's output is the same for any chunking of its input | Lets the output-hash leg (`test_output_hash.cpp`) and the ratchet's `checksum=` line (`scripts/icount.py:100`) stand |
| R11 | **One family version** (D13): `TAP_SR_VERSION_*` token-identically in `rational.h` — 0.4.0 until M6, **0.5.0 at M6**, 0.6.0 since the 2026-10 audit response (the async vocabulary pass), 0.7.0 since A13 (every channel count the type's), bumped in all three umbrella headers and the root `project()` together (decision 10); `tap_sr_rational_version()` bit-packed; `tests/family/version_macros.cpp` gains a third include and `static_assert` | `../PLAN.md:48`; a new engine is a minor bump |
| R12 | **Tests carry the engine prefix and label** `rational.` / `rational` (D16, `../PLAN.md:51`), including the `*_tests_emulated` entries; guest icount marker `RATIONAL_ICOUNT_DONE`, byte-stable from its first baseline | `../CLAUDE.md`, "Guest markers … never change" |
| R13 | **`bridge` at the lowest k** (decision 1): a cross-family chain contains exactly one `bridge` stage, at the lowest k ∈ {0, 1, 2} whose lower rate satisfies 2.1(a) for the chain's f_pass (3.1). `transparent` needs no exception: its 20 kHz passband clears both stopband edges at k = 0 (44.1 − 20 = 24.1 ≥ 24 up, ≥ 22.05 down) | `bridge` costs 58 (down) or 38 (up) MACs per output at `economy` whatever k is, so its cost scales with the rate it runs at, while the ↑2 / ↓2 stages that move a chain between k levels cost 9–19; 48 → 88.2 at k = 0 is 62 % of the MACs of k = 1 at the same (f_pass, A). The family plan's illustrative row (`../PLAN.md:120`) was corrected to match |
| R14 | **Dipping rows are flagged** (decision 3): every row of 3.5/3.6 whose `bridge` stage runs — its output rate — at ≥ 2× the chain's output rate carries ⚑ in a trailing column, "covered, not recommended". Documentation only: the flag dispatches nothing and D12 is untouched | In such a row `bridge` alone costs ≥ 116 MACs per output at `economy` (≥ 368 at `transparent`), 2–4× a within-family chain's total; a consumer who reads the matrix sees the price before choosing. 38 rows carry it |
| R15 | **384 kHz stays** (decision 5): the family plan's 14 rates are settled (`../PLAN.md:100-101`) | Its cost is 16 generated rows, the ↑8 / ↓8 stages already in the vocabulary and the 64 / 128 kHz intermediates; no new stage kind |
| R16 | **The C ABI exposes the named within-family chains** of 3.3/3.4 as its `chain` enumerators (decision 9; 28 of them on the pinned lengths, v0.7 — the union of the two tables' chain columns) — chains, not rates, and not the single-stage ratios alone. **Every value is stable** (`docs/X4_X8_PLAN.md` X7): since X-a3 a new chain takes a new value (28 to 39), a retired chain's value is never reused and `create` refuses it (10, 11, 14, 15, 16, 21, 22, 25, 26, 27), and `TAP_SR_RATIONAL_CHAIN_END`, one past the largest value ever assigned, replaces the retired `CHAIN_COUNT`; since X-a4 the 120 dB tier's three new chains take 40 to 42 and `CHAIN_END` is 43: 33 live values, the 25 named chains of `chain.h` (seven of them used only at the 120 dB tier, X4) and the eight one-stage chains | D12 at the C boundary: an enumerator names what the caller composes, as `bridge`'s `int direction` does; the binding and the notebook then need no chain arithmetic of their own (4.4) |
| R17 | **The channel count is the type's** (2026-11-17, the family's A13 and D18): `Ch`, a compile-time constant of at least 1, is `basic_stage<S, Ch, R>`'s and `basic_chain<S, Ch, R...>`'s second template parameter and the aliases' first; the count-independent state and cold code live in `stage_base<S, R>`, built once per format and ratio; `process`, `pull` and `flush` are each the out-of-line block for their count; the C ABI serves one and two channels natively and any other count as a fan-out of one-channel converters | The maintainer's decision that every count is known at compile time; the base keeps an image that runs a type at several counts from paying for its construction code per count (the plan's audit, design-2); the placement is gate 0's option 3, its three rows above the rule accepted (section 6) |

### 2.1 Stage factoring: the arithmetic behind R3

All lengths below are harris estimates at the `economy` candidate
(A = 70 dB, p = 3/8, so a stage whose lower rate is r has passband edge
0.375 r and, by 2.1(a) with equality, stopband edge r − 0.375 r = 0.625 r;
transition width 0.25 r). N = (A − 8) / (2.285 · Δω) with
Δω = 2π · (transition) / (higher rate) (`kaiser.h:83`), then rounded up to
the Nyquist length N = 2mL − 1 (2.2), whose nonzero tap count is
2m(L − 1) + 1.

**↓4 as one stage vs ↓2 · ↓2** (4r → r):

- One 4th-band stage: Δω = 2π · 0.25r / 4r = π/8 = 0.3927;
  N_est = 62 / (2.285 · 0.3927) = 69.1 → m = 9, N = 71, nonzero = 55.
  **55 MACs per output.** Group delay 35 samples at 4r = 8.75 outputs.
- ↓2 (4r → 2r; lower rate 2r, so its stopband edge may sit at
  2r − 0.375r = 1.625r and the transition is 1.25r wide):
  Δω = 2π · 1.25 / 4 = 1.963; N_est = 13.8 → m = 4, N = 15, nonzero = 9,
  at two outputs per final output: 18. Then ↓2 (2r → r):
  Δω = 2π · 0.25 / 2 = 0.785; N_est = 34.5 → m = 9, N = 35, nonzero = 19.
  **37 MACs per output (−33 %).** Group delay 7 at 4r + 17 at 2r = 41
  samples at 4r = 10.25 outputs (+1.5 outputs against the single stage).

**↓6**: ↓3 (6r → 2r: N = 23, nonzero 17, ×2) · ↓2 (N = 35, 19) = 53; the
other order ↓2 (6r → 3r: N = 15, 9, ×3) · ↓3 (3r → r: N = 53, 37) = 64;
one 6th-band stage N = 107, nonzero 91. Hence "larger factor at the
high-rate end, by-2 at the low-rate end". **↑6** by transposition:
↑2 · ↑3 = 18/6 + 16/3 = 8.3 per output against ↑3 · ↑2 = 10 and a single
↑6 at 15.

**Mixed ratio 2/3 (48 → 32)**: one polyphase stage at 96 kHz, cutoff at
16 kHz (a third band of 96 kHz), transition 8 kHz: Δω = 0.5236,
N_est = 51.8 → T = 26 taps per phase over L = 2 phases: **26 MACs per
output.** As ↑2 (48 → 96: N = 19, 10 MACs per input = 15 per 32 kHz
output) · ↓3 (N = 53, 37): 52. The single stage wins by 2×, so a mixed
ratio is one stage (R2).

A generated search over every monotone factorization through the
2^a · 3^b lattice (restricted to L, M ∈ {2, 3, 4, 6, 8}) confirms these
rules for all 92 within-family pairs; section 3.3 lists its result per
ratio. Where the search and a chain with fewer stages differ by less than
10 % (3/1 as 3/2 · ↑2 at 11.0 against one ↑3 at 12.0), the chain with
fewer stages is chosen (R3, decision 2) and the matrix test pins it; the
search's pick is recorded next to it where it differs (3.2, 3.3).

**On the pinned lengths (M4, v0.7)** the rules stand but eleven of the 26
within-48 ratios factor differently from this arithmetic, because on
those pins a relaxed half-band at a passband below 3/16 of its lower rate
stayed at m = 7 (N = 27, 7.5 MACs per output) at the divisors 2 to 4,
while a third-band stage there was m = 5 (N = 29, 7 per output) and
serves three times the rate, so the "by-2 at the low-rate end" chains
lost to third-band and mixed stages (8/1 is ↑2 · 4/3 · ↑3, 12/1 is
↑2 · ↑6, 1/8 is ↓3 · 3/4 · ↓2). Section 3 holds the measured result; the
ledger is in section 8, S1.

**That floor was the designer's, not the Kaiser fit's (X17, v0.17).** The
designer took its window's β for A dB while the pin search accepts a
design only at A + 1 dB, and a short windowed sinc with β tuned for 70 dB
has sidelobes at −68 to −70.6 dB, so it missed the margin until m = 7 —
and the search was not even monotone in m. With β for A + 4 dB (X17,
`design.h`'s `k_design_beta_offset_db`) the relaxed `economy` half-band is
m = 5 at divisor 2 and m = 4 at divisors 3 to 12 (N = 19 and 15), and on
these pins three half-bands cost less than the third-band and mixed chains
above: 1/8 as ↓2 · ↓2 · ↓2 is 79 MACs per output against ↓3 · 3/4 · ↓2's
87, 8/1 as ↑2 · ↑2 · ↑2 9.88 against 10.38. X-a2 (X17) re-pinned the
lengths and froze the selection for one pull request; X-a3 (v0.18)
regenerated it under the new designer with the 4th-band stage admitted.

**The 4th-band stage, admitted (X-a3, v0.18; decision 6 as amended).** The
arithmetic above still holds for ×4 itself: on the pins one 4th-band stage
costs 61 MACs per output going down and 15.25 going up at `economy`, two
half-bands 43 and 10.8, so the matrix's chain by 4 stays ↓2 · ↓2 / ↑2 · ↑2.
Where the 4th band sits *above* a stage, at a divisor of 2 or more, its
relaxed design is short (m = 4 at divisors 2 to 4, m = 3 from 6 up, N = 31
and 23 at `economy`), and it wins: 1/8 is ↓4 · ↓2 at 71 MACs per output
against three half-bands' 79 (11 % more, so R3's fewer-stages rule does
not even need to decide) and v0.7's ↓3 · 3/4 · ↓2 at 87; 8/1 is ↑2 · ↑4 at
8.88 against 9.88 and 10.38. The rule is unchanged; the vocabulary it
searches gained a band, and section 3 is regenerated from it: 46 rows moved
at `economy` (30 within the families, 16 across), the ledger in section 8,
S1 (`docs/X4_X8_PLAN.md` X3, X17; section 6, the X-a3 record).

### 2.2 The Nyquist (L-th-band) design: the math behind R4

An L-th-band lowpass has impulse response h[n] with h[c] = 1/L and
h[c ± kL] = 0 for every k ≥ 1 (Mintzer 1982; Vaidyanathan, *Multirate
Systems and Filter Banks*, §4.6). The Kaiser-windowed sinc with cutoff
exactly π/L gives this *by construction*: the ideal kernel is
(1/L) · sinc((n − c)/L) (`kaiser.h:134-136` with `cutoff_norm = 1/L` and
`num_phases = 1`, the call `decimate.h:118` already makes), whose zeros sit
at n − c = ±L, ±2L, …; the window multiplies them, so they stay zero.
The designer:

- takes N = 2mL − 1 for an integer m ≥ 2 (centre c = mL − 1, so the taps
  at c ± kL, k = 1 … m − 1, are the 2(m − 1) zeros and the end taps are
  nonzero; nonzero count 2m(L − 1) + 1; N odd for an integer group delay);
- writes the zero taps as exact zeros (libm's sin(πk)/(πk) is ~1e−16, not
  0; the contract says 0, and the fixed-point tables are bit-pinned);
- normalizes per polyphase branch to DC gain exactly 1 (as
  `design.h:180-199`): for ↑L, branch 0 is the single tap h[c] and becomes
  exactly 1.0 — a copy; for ↓M the whole filter is one row with sum 1
  (`decimate.h:22-23`'s convention);
- searches m upward until the stopband spec holds with ≥ 1 dB margin on a
  fine grid (the `bridge` criterion, `design.h:79-82`), so tap counts step
  by 2L; since X17 (v0.17) every candidate's window takes Kaiser's β for
  A + 4 dB, not for A (`design.h`'s `k_design_beta_offset_db`; section 6,
  "The designer's β, X17").

Consequences that the tests pin:

- **Symmetric transition.** The response of an L-th-band filter is
  antisymmetric about π/L, so passband edge f_p and stopband edge f_s of a
  stage whose lower rate is r satisfy f_p + f_s = r. The coverage rule
  2.1(a) demands f_s ≤ r − f_pass; with f_p = f_pass the Nyquist structure
  gives exactly f_s = r − f_pass. The structure is the rule.
- **Half the MACs for ↑2/↓2.** N = 4m − 1 taps, 2m + 1 nonzero: a ↓2 stage
  computes 2m + 1 MACs per output, a ↑2 stage 2m per *input* (branch 0 is
  the copy), against N for a generic FIR of the same length.
- **Branch 0 of ↑L is the identity**, so an L-th-band interpolator costs
  2m(L − 1) MACs per input sample, (L − 1)/L of a generic polyphase
  interpolator's.
- **The family's 2.1 remark on 16 → 44.1** (`../PLAN.md:96-98`) is
  satisfied, not evaded: an ↑3 stage from 16 kHz at `economy` has
  f_pass = 6 kHz and stopband edge 10 kHz (= 16 − 6), so every image of
  passband content lands at ≥ 10 kHz, above f_pass; the "8–9 kHz images"
  the family plan warns about belong to a wider stage that v2's rules
  allowed and 2.1(a) does not.

### 2.3 Profiles (pinned by the M2 design spike)

| Profile | A (dB) | p = f_pass / r_min | at r_min = 48 kHz | half-band N / nonzero (worst stop) | third-band N / nonzero (worst stop) |
|---|---|---|---|---|---|
| `super_economy` | 70 | 1/3 | 16 kHz | 31 / 17 (−72.4 dB); *est.* 27 / 15 | 47 / 33 (−73.6 dB); *est.* 41 / 29 |
| `economy` (default) | 70 | 3/8 | 18 kHz | 39 / 21 (−73.1 dB); *est.* 35 / 19 | 59 / 41 (−72.5 dB); *est.* 53 / 37 |
| `balanced` | 70 | 19/48 | 19 kHz | 47 / 25 (−72.5 dB); *est.* 43 / 23 | 71 / 49 (−72.4 dB); *est.* 65 / 45 |
| `transparent` | 120 | 5/12 | 20 kHz | 99 / 51 (−122.2 dB); *est.* 95 / 49 | 149 / 101 (−122.8 dB); *est.* 143 / 97 |

The first number of each cell is **measured and pinned** (v0.3 found
them, v0.5's M2 spike confirmed them and filled the other bands, v0.17
re-pinned them under X17's rule, section 6): the smallest m whose design,
its window's β taken for A + 4 dB, meets the stopband A with ≥ 1 dB margin
on a **16384-point grid** from the stopband edge to F_hi / 2, by
`tap::dsp::search_nyquist_m` on the shipped designer (DspTap `nyquist.h`),
the next-shorter length missing the spec; `design.h`'s `profile` carries
the pins per band and `test_design.cpp` enforces every row. The grid is
stated because it mattered: with β for A, `nyquist.h`'s 1024-point
default stepped over a sidelobe of the long designs — the 8th-band
`transparent` candidate read −121.7 dB on 1024 points and −119.3 dB on
8192 — and under-pinned two rows of the full table (band 4 `balanced`
103 → 111, band 8 `transparent` 383 → 399); 16384 points agree with 65536
within 0.003 dB for every design up to N = 399, and tap/DspTap#50 made the
search's grid a parameter. Under X17's rule the 1024-point grid happens to
find every pin of the tables (measured 2026-11-20), and the grid stays
part of the criterion. The harris estimates (*est.*) are kept beside the
measured counts: with β for A + 4 dB the Kaiser fit needs exactly one more
tap per branch than the estimate at the half and third bands, every
profile (with β for A it needed one to seven more); M4 regenerated
section 3 from the pinned counts (v0.7). The
counts are for the stage adjacent to r_min (the narrowest transition in a
chain); stages further up a chain are shorter, designed at their design
divisor (3.1) with the pins of `design.h`'s relaxation tables. `bridge`'s experience says a pinned count can also move
non-monotonically with the spec (`design.h:79-82`, the 38-vs-40 quirk in
`../bridge/PLAN.md` §4), which is why every (band, profile) was pinned
from the designer rather than extrapolated. Ripple, measured per stage on
a 2000-point passband grid: ≤ 0.0024 dB at the 70 dB tiers and
≤ 0.00001 dB at `transparent` (≤ 0.0025 and ≤ 0.00001 with β for A; the
relaxed designs ≤ 0.0043 and ≤ 0.000011 dB), inside the candidates for 2.1(c) — ±0.01 dB
per 70 dB stage, ±0.0001 dB per transparent stage (`bridge` measures
±0.003 / ±0.00001, `../bridge/PLAN.md` §4) — which `test_design.cpp` now
holds as bounds; the chain's δ is the sum (2.1(c)), pinned per pair by the
matrix test.

### 2.4 Where the code lives (R6)

| Asset | Home | Why |
|---|---|---|
| L-th-band designer `design_nyquist(h, L, beta)` and the `m`-search | DspTap `tap/dsp/nyquist.h` | design math is substrate; `decimate.h` may adopt it later for MuTap without touching this engine |
| `chain<Stages...>` and the `sync_stage` concept | DspTap `tap/dsp/chain.h` | the composition point below both engines (R6) |
| `ratio<L, M>`, `ratio_traits`, `profile`, stage schedule, `basic_stage`, `basic_converter`, aliases | `rational/include/tap/sr/rational/` | engine-owned datapath (D11, `../PLAN.md:46`) |
| coverage matrix, cross-validation, scipy vectors, icount workloads, C ABI, notebook | `rational/{tests,bench,capi,notebooks}` | per engine (D6, D8) |

### 2.5 Alignment, first-output timing and latency (R7)

Mirrors `decimate.h:17-24` and `converter.h:51-56`, per stage:

- **↓M** (`ratio<1, M>`): y[k] = Σ_t h[t] · x[kM − t], x[n < 0] = 0. Output
  k is produced as input kM arrives; n inputs from a fresh instance yield
  ceil(n / M) outputs; `outputs_for()` gives the exact count from the
  current phase. Group delay (N − 1)/2 input samples, an integer.
- **↑L** (`ratio<L, 1>`): y[n] = Σ_t h[t] · x[(n − t)/L] over t ≡ n (mod L),
  x[n < 0] = 0 — scipy `upfirdn(h, x, up=L)`'s streaming prefix. n inputs
  yield exactly nL outputs. Group delay (N − 1)/2 *output* samples, an
  integer (= m − 1/L input samples, not an integer: the contract states the
  delay at the higher rate on purpose).
- **Mixed L/M**: `bridge`'s contract verbatim (`converter.h:51-56`):
  y[n] = Σ_k x[⌊nM/L⌋ − k] · h[phase(n) + kL]; group delay (N − 1)/2
  samples at the composite rate, i.e. (L · T − 1)/(2L) input samples
  (`phase_table.h:83-85`).
- **Chain**: outputs are the last stage's; `outputs_for` composes forward,
  `frames_needed` composes backward (each stage's is exact arithmetic,
  `schedule.h:54`): `outputs_for(frames_needed(k)) >= k` and one frame
  fewer falls short. Not an equality: one input to an interpolating stage
  completes L outputs at once, so `process(frames_needed(k))` can write
  more than k; size the output by `outputs_for()`. (A single stage's
  `pull()` stops at k; a chain has no pull.) Latency is
  Σ_i (N_i − 1)/2 · (f_out / f_hi,i), an exact rational at the output
  rate, reported as `latency_output_frames()` (numerator, denominator) and
  `latency_seconds()`; the matrix test pins it per pair. `flush()` drains
  every stage's tail in order (each stage fed its `window_frames()` of
  zeros through the stages after it, DspTap `chain.h`), writing
  `flush_output_frames()` frames, bit-identical to zero-padding the input.
  Each stage of a chain is designed at its design divisor (3.1).

## 3. The coverage matrix

### 3.1 The rule, applied

For each ordered pair, the chain declares f_pass = p · r_min with
r_min = min(f_in, f_out), and every rate-changing stage whose lower rate is
r is designed with stopband edge r − f_pass (2.1(a) with equality, which is
the Nyquist structure, 2.2) at attenuation A (2.1(b)); the chain's ripple
is the sum of its stages' (2.1(c)); the matrix test pins (f_pass, A, δ),
MACs per output and latency per pair (2.1(d)).

**Each stage at its own rate: the design divisor (M4).** A stage whose
lower rate is r is designed with its passband at f_pass, i.e. at the
fraction p · r_min / r of its own lower rate. `chain.h` computes that
quotient per stage at compile time and takes its **design divisor** d: the
largest 2^a · 3^b at or below r / r_min — exact within a family, where the
quotient is such a number, and conservative across (a passband at or above
f_pass, so 2.1(a) still holds) — or the quotient itself when it is below 1,
the one stage that runs *below* a 48-family r_min, at `bridge`'s rate
(44.1 · 2^k over 48 · 2^k = 147/160: 48 → 88.2's ↑2 from 44.1 must pass
18 kHz, a tightening). `profile::relaxed(d)` is the stage's profile: the
passband over d and the pinned m per band from the profile's relaxation
table (`design.h`, one row per divisor the matrix uses — 147/160, 1, 2, 3,
4, 6, 8, 12 since X-a3, whose selection no longer runs a stage at 9 or 16,
so those two rows left with it — found by the same search as the M2 pins and verified
against the shipping designer by `test_design.cpp`; a divisor without a
row, or a custom profile, is searched at construction). The generator
costs every stage this way, so the matrix's counts are the chains'.

**Placing `bridge` (R13, decision 1).** A chain between the families
contains exactly one `bridge` stage, at the lowest k ∈ {0, 1, 2} whose
lower rate 44.1 · 2^k satisfies 2.1(a) for the chain's f_pass — i.e. the
lowest k with 44.1 · 2^k − f_pass ≥ `bridge`'s stopband edge at k
(22.05 · 2^k down, 24 · 2^k up; `design.h:56,73`) and f_pass ≤ `bridge`'s
passband at k. For r_min ≤ 48 that is k = 0; for r_min ∈ {88.2, 96} k = 1;
for r_min ∈ {176.4, 192} k = 2. The reason is MACs: `bridge` costs 58
(down) or 38 (up) MACs per *output* at `economy` regardless of k, so its
cost scales with the rate it runs at, while the ↑2/↓2 stages that move the
chain between k levels cost 9–19 MACs per output. Example, 48 → 88.2
(r_min = 48, f_pass = 18 kHz): `bridge`↓ at k = 0 (44.1 kHz output,
58 × 44 100 = 2.56 M MAC/s) then ↑2 to 88.2 (N = 51, *est.*, 26 MACs per
input; 26 × 44 100 = 1.15 M MAC/s), 3.7 M MAC/s in all, against ↑2 first
(48 → 96, N = 35; 0.9 M MAC/s) then `bridge`↓ at k = 1
(58 × 88 200 = 5.1 M MAC/s), 6.0 M MAC/s. The rule is settled: the
family plan's illustrative row, which put this pair at k = 1, was
corrected to `bridge` k = 0 → ↑2 (`../PLAN.md:120`) with the note that
this plan places `bridge` at the lowest k, at 62 % of the MACs with the
same (f_pass, A). `transparent` needs no exception: its 20 kHz passband
clears both of `bridge`'s stopband edges at k = 0 (44.1 − 20 = 24.1 ≥ 24
up, ≥ 22.05 down).

**Fewer stages within 10 % (R3, decision 2).** The generator finds the
MAC-minimal chain, then the MAC-minimal chain for every smaller stage
count around the same `bridge` stage (same k, same direction) where there
is one; the chain chosen is the one with the fewest stages whose
**rational stages'** MACs per output are within 10 % of the minimum's. `bridge`'s stage is identical in both candidates and is
excluded from the comparison: the rule compares factorizations of the
rational part, and a `bridge`-dominated total must not absorb a worse
factorization (with the total in the comparison, 8 → 11.025 would have
collapsed ↑2 · ↑3 into one ↑6 at 297 → 326 MACs per output). Less
latency, less state and fewer test rows are worth a few per cent that
the measurement resolves to a few taps. On the pinned lengths (v0.7) the
rule bites on 24 of the 182 pairs at `economy`: the three 3/1 pairs
(↑3 for ↑2 · 3/2, +2.3 %), the three 12/1 (↑2 · ↑6 for ↑2 · ↑3 · ↑2,
+5.0 %), the two 24/1 (+3.0 %) and the 48/1 pair (↑2 · ↑8 · ↑3 for five
stages, +0.5 %), their mirrors 1/12 (↓6 · ↓2, +5.0 %), 1/24 (+3.0 %) and
1/48 (+0.5 %), 16 → 11.025 / 22.05 / 44.1 / 88.2 / 176.4 (↑3 for
↑2 · 3/2 ahead of `bridge`, +0.6–2.3 % on the rational part), and in the
44.1 family 8/1 (↑3 · 8/3 for ↑2 · ↑2 · ↑2, +2.7 %) and 1/8 (3/8 · ↓3 for
three half-bands, +9.7 %), and `tools/coverage/matrix.py rule` printed
the list. Since X-a3 (X17's pins, the 4th-band stage admitted) it bites on
18: the three 3/1 pairs (↑3 for ↑2 · 3/2, +7.9 %), the three 12/1
(↑3 · ↑4 for ↑2 · ↑3 · ↑2, +6.4 %) and their mirrors 1/12 (↓4 · ↓3), 48/1
(↑3 · ↑4 · ↑4 for four stages, +2.1 %) and 1/48, 16 → 11.025 / 22.05 /
44.1 / 88.2 / 176.4 (↑3 ahead of `bridge`, +2.8–7.9 % on the rational
part), 384 → 11.025 (↓4 for ↓2 · ↓2 after `bridge`, +7.3 %) and
11.025 → 384 (↑4 for ↑2 · ↑2 ahead of it, +7.3 %). Within-family chains have no `bridge`, so
the rule there is simply 10 % of the chain.

**A chain per tier (X4, since X-a4, v0.19).** The rule runs once per
attenuation tier, a second search: the 70 dB tier's chain is selected at
`economy` and is the chain a row runs at `super_economy`, `economy` and
`balanced` (so `super_economy`'s exact ×4 tie does not move a row), and the
120 dB tier's is selected at `transparent` and is the chain it runs there.
Until X-a4 the `transparent` columns cost the `economy` chains at
`transparent`. The two selections differ in 25 rows, 17 within the
families and 8 across (the ledger is section 6's X-a4 record), and seven
within-family chains serve only the 120 dB tier: ↑2 · 3/2 (3/1), ↑2 · ↑6
(12/1), ↑2 · ↑8 (16/1), ↑2 · ↑6 · ↑4 (48/1) and the mirrors ↓6 · ↓2,
↓8 · ↓2 and ↓4 · ↓6 · ↓2. At `transparent` the fewer-stages rule bites on
25 rows: 12/1 (↑2 · ↑6 for ↑2 · ↑2 · ↑3, +3.9 %), 16/1 (↑2 · ↑8 for
↑2 · ↑2 · ↑4, +6.1 %), 48/1 (↑2 · ↑6 · ↑4 for four stages, +3.2 %), their
mirrors, 1/3 (↓3 for 2/3 · ↓2, +3.1 %, so 1/3 stays one stage at both
tiers), and eight cross rows (+1.3 … +7.8 % on the rational part), as
`matrix.py rule` prints per tier. Every named chain still runs at every
profile; a tier says where the matrix selects a chain.

**The selection, frozen for one pull request and restored (X17, X-a3).**
X-a2 (v0.17) re-tuned the designer's β (2.2, section 6) and kept every
row's chain as the rule chose it on v0.7's pins, through a committed
`FROZEN_SELECTION` table in the generator: R3's selection was suspended for
that one PR, so the chain set would change once. X-a3 (v0.18) removed the
table and restored `best_chain` with the 4th-band stage admitted (decision 6
as amended): on X17's pins the rule moves 46 rows at `economy` (30 within
the families, 16 across), every one of them onto a chain that runs the
4th-band stage, and the named chains gain twelve and lose fourteen, ten of
them retired and four kept for the per-tier matrix (`docs/X4_X8_PLAN.md` X3,
X17; section 6, the X-a3 record; the ledger in section 8, S1).

**Chains that dip.** A pair below 44.1/48 in different families (8–32 kHz
↔ 11.025/22.05) can only cross through the k = 0 pair, so its chain climbs
to 48 (or 44.1), crosses, and descends. Every stage still meets 2.1(a) —
the stage that descends from 44.1 to 22.05 with f_pass = 3 kHz has a
stopband edge at 19.05 kHz — so these rows are covered, at the price the
table shows (the `bridge` stage runs at 44.1 kHz for a 11.025 kHz output).
**Such rows are flagged** (R14, decision 3): the trailing ⚑ column of
3.5/3.6 marks every row whose `bridge` stage's output rate is ≥ 2× the
chain's output rate — `bridge` alone then costs ≥ 116 MACs per output at
`economy` — as covered, not recommended. 38 rows carry the flag: the 18
rows into 11.025 / 22.05 and the 20 rows into 8 / 12 / 16 / 24. The flag
is documentation; nothing dispatches on it (D12).
A within-family chain never dips: its intermediate rates stay between
f_in and f_out.

**Exclusions.** None of the 182 ordered pairs is excluded: every chain
passes `bridge` at k ≤ 2 and needs no rate outside the 14 except the
2^a · 3^b intermediates of its own family (64 and 128 kHz appear as stage
boundaries before X-a3, e.g. 16 → 32 → 64 → 192 and 384 → 64 → 48 →
44.1 → 22.05; since X-a3 only 64 kHz does, in 32 → 64 → 192 and its
mirror; they are not supported rates and no chain starts or ends
there). The
family plan's exclusions stand as rates, not pairs: 352.8 kHz would need
k = 3, 37.8 and 50.4 kHz are ratios of 7, and the 1000/1001 pull-downs are
synchronous ratios that no engine's charter names (`../PLAN.md:103-109`).

### 3.2 Worked cases

Lengths are harris estimates (*est.*) at `economy` unless marked `tr`
(`transparent`); the arithmetic is the one-line form of 2.1, kept as the
derivation. The measured chains and numbers are 3.3–3.6; four of the six
cases below are the measured chain too, and two chose differently on the
pinned lengths — at v0.7 384 → 44.1 was ↓3 · 3/4 · ↓2 · `bridge`↓ k = 0
(172.3 MACs per output) and 8 → 192 ↑2 · ↑6 · ↑2 (8.7), for the reason 2.1
ends with; since X-a3 they are ↓4 · ↓2 · `bridge`↓ k = 0 (135.3) and
↑2 · ↑3 · ↑4 (7.0), the 4th-band stage at the high-rate end (2.1).

- **176.4 → 48** (`../PLAN.md:93-95`): r_min = 48, f_pass = 18 kHz
  (`tr`: 20 kHz). Chain ↓2 · ↓2 · `bridge`↑ k = 0.
  ↓2 (176.4 → 88.2; lower 88.2): stopband edge 88.2 − 18 = 70.2 kHz,
  transition 52.2 kHz at 176.4: Δω = 1.859, N = 15, nonzero 9 (`tr`:
  stopband 68.2, N = 31, nonzero 17).
  ↓2 (88.2 → 44.1; lower 44.1): stopband edge 44.1 − 18 = 26.1 kHz,
  transition 8.1 kHz at 88.2: Δω = 0.577, N = 51, nonzero 27 (`tr`:
  stopband edge 44.1 − 20 = **24.1 kHz, the number the family plan
  states**, transition 4.1 kHz, N = 171, nonzero 87).
  `bridge`↑ (44.1 → 48): its stopband edge 24 kHz ≤ 44.1 − 18 = 26.1 ✓
  (`tr`: ≤ 24.1 ✓, the family plan's "f_pass ≤ 20.1 kHz"). 38 MACs per
  output (`tr`: 96).
  Per 48 kHz output: 9 · 88.2/48 + 27 · 44.1/48 + 38 = 16.5 + 24.8 + 38 =
  **79.3** (`tr`: 31.2 + 79.9 + 96 = **207.2**). Latency 0.75 ms (`tr`:
  2.14 ms).
- **16 → 44.1** (`../PLAN.md:96-98`): r_min = 16, f_pass = 6 kHz. Through
  ↑3 (lower 16): stopband edge 16 − 6 = 10 kHz, transition 4 kHz at 48:
  Δω = 0.5236, N = 53, 12 MACs per 48 kHz output; `bridge`↓ k = 0 (its
  stopband edge 22.05 ≤ 44.1 − 6 ✓): 58. Per 44.1 kHz output:
  12 · 48/44.1 + 58 = **71.1**. The search's MAC minimum is 3/2 · ↑2 ·
  `bridge`↓ (16 → 24 → 48: N = 54 as T = 18 over 3 phases, then N = 19)
  at 70.0, three stages; the rational parts are 13.1 against 12.0, 9.1 %
  apart, so the two-stage chain wins under the fewer-stages rule (R3).
  Latency 1.15 ms (3/2 · ↑2: 1.34 ms).
- **384 → 44.1**: r_min = 44.1, f_pass = 16.54 kHz. ↓2 (384 → 192: lower
  192, transition 192 − 33.1 = 158.9 kHz, N = 11, nonzero 7) · ↓2
  (192 → 96: N = 15, 9) · ↓2 (96 → 48: lower 48, stopband edge 31.5,
  transition 14.9 kHz at 96, N = 31, 17) · `bridge`↓ k = 0 (58). Per
  output: 7 · 192/44.1 + 9 · 96/44.1 + 17 · 48/44.1 + 58 = 30.5 + 19.6 +
  18.5 + 58 = **126.6**. Latency 0.81 ms.
- **8 → 192**: r_min = 8, f_pass = 3 kHz. ↑3 (8 → 24: stopband edge 5 kHz,
  transition 2 kHz at 24, Δω = 0.5236, N = 53, nonzero 37, 36 MACs per
  input) · ↑8 (24 → 192: lower 24, stopband edge 21, transition 18 kHz at
  192, Δω = 0.589, N_est = 46.1 → m = 3, N = 47 = 2 · 3 · 8 − 1, nonzero
  43, 42 per input). Per 192 kHz output: 36/24 + 42/8 = 1.5 + 5.25 =
  **6.8**. Latency 26 samples at 24 kHz + 23 at 192 = 231 output samples
  = 1.20 ms, almost all of it the ↑3. The search's MAC minimum is
  ↑2 · ↑2 · ↑6 (8 → 16 → 32 → 192) at 6.4 and 263 output samples; 5 %
  apart, so the two-stage chain wins (R3). The ↑8 at the top is R3's
  "larger factor at the high-rate end": with the transition 18 kHz wide
  relative to 192, a single 8th-band stage of 47 taps costs 5.25 per
  output where three half-bands from 24 would cost about 5.9.
- **11.025 → 48**: r_min = 11.025, f_pass = 4.13 kHz. ↑2 (→ 22.05:
  stopband edge 6.9, N = 35) · ↑2 (→ 44.1: N = 15) · `bridge`↑ k = 0
  (its stopband edge 24 ≤ 44.1 − 4.1 ✓; 38). Per output: 18 · 11.025/48 +
  8 · 22.05/48 + 38 = 4.1 + 3.7 + 38 = **45.8**. Latency 1.36 ms.
- **8 → 11.025** (a dipping chain, flagged ⚑): r_min = 8, f_pass = 3 kHz.
  ↑2 (8 → 16: stopband edge 5 kHz, N = 35, 18 MACs per input) · ↑3
  (16 → 48: lower 16, stopband edge 13, transition 10 kHz at 48, N = 23,
  nonzero 17, 16 per input), `bridge`↓ k = 0 to 44.1, ↓2 (44.1 → 22.05:
  lower 22.05, stopband edge 19.05, N = 15, 9) · ↓2 (22.05 → 11.025:
  lower 11.025, stopband edge 8.025 kHz, N = 19, 11). Per 11.025 kHz
  output: 18 · 8/11.025 + 16 · 16/11.025 + 58 · 4 + 9 · 2 + 11 = 13.1 +
  23.2 + 232 + 18 + 11 = **297.3** (`bridge` alone is 232). The
  four-stage ↑6 · `bridge`↓ · ↓2 · ↓2 costs 326.3 — within 10 % of the
  total, but its rational part is 94 against 65, so the fewer-stages rule
  does not take it (R3). Latency 2.46 ms. Covered, honest, and nobody's
  default: the row carries the flag (R14).

### 3.3 Within the 48 kHz family (72 ordered pairs, 26 ratios)

A chain per tier (3.1, X4): "Chain eco" is the 70 dB tier's, selected at
`economy` and run at `super_economy`, `economy` and `balanced`; "Chain tr"
the 120 dB tier's, selected at `transparent`, the same chain where the two
selections agree, and the `tr` columns are its numbers (until X-a4 they
cost the `economy` chain at `transparent`). MAC/out = MACs per output frame
at the chain's output rate; latency in output frames. **Measured** (v0.7;
re-costed on X17's pins at v0.17, the chains re-selected with the 4th-band
stage admitted at v0.18, X-a3, and per tier at v0.19, X-a4; 3.1): every
number is the shipping chain's,
generated by `tools/coverage/matrix.py` from the pinned lengths and pinned
exactly — as a rational — per row by `tests/test_matrix.cpp`.

| Ratio L/M | Pairs (from → to, kHz) | Chain eco | MAC/out eco | Latency eco (out) | Chain tr | MAC/out tr | Latency tr (out) |
|---|---|---|---|---|---|---|---|
| 4/3 | 12→16, 24→32 | 4/3 | 15.2 | 13.0 | 4/3 | 37.8 | 33.0 |
| 3/2 | 8→12, 16→24, 32→48 | 3/2 | 13.7 | 14.5 | 3/2 | 33.7 | 37.0 |
| 2/1 | 8→16, 12→24, 16→32, 24→48, 48→96, 96→192, 192→384 | ↑2 | 10.5 | 19.0 | ↑2 | 25.5 | 49.0 |
| 8/3 | 12→32 | ↑2 · 4/3 | 14.1 | 30.3 | ↑2 · 4/3 | 31.4 | 75.7 |
| 3/1 | 8→24, 16→48, 32→96 | ↑3 | 13.7 | 29.0 | ↑2 · 3/2 | 28.0 | 85.0 |
| 4/1 | 8→32, 12→48, 24→96, 48→192, 96→384 | ↑2 · ↑2 | 10.8 | 47.0 | ↑2 · ↑2 | 22.2 | 115.0 |
| 6/1 | 8→48, 16→96, 32→192 | ↑2 · ↑3 | 9.2 | 68.0 | ↑2 · ↑3 | 19.5 | 170.0 |
| 8/1 | 12→96, 24→192, 48→384 | ↑2 · ↑4 | 8.9 | 91.0 | ↑2 · ↑4 | 18.6 | 227.0 |
| 12/1 | 8→96, 16→192, 32→384 | ↑3 · ↑4 | 9.7 | 131.0 | ↑2 · ↑6 | 17.8 | 341.0 |
| 16/1 | 12→192, 24→384 | ↑2 · ↑4 · ↑2 | 8.9 | 189.0 | ↑2 · ↑8 | 17.3 | 455.0 |
| 24/1 | 8→192, 16→384 | ↑2 · ↑3 · ↑4 | 7.0 | 283.0 | ↑2 · ↑3 · ↑4 | 12.6 | 699.0 |
| 32/1 | 12→384 | ↑2 · ↑4 · ↑4 | 7.0 | 375.0 | ↑2 · ↑4 · ↑4 | 12.4 | 927.0 |
| 48/1 | 8→384 | ↑3 · ↑4 · ↑4 | 7.2 | 535.0 | ↑2 · ↑6 · ↑4 | 12.2 | 1383.0 |
| 3/4 | 16→12, 32→24 | 3/4 | 26.3 | 9.8 | 3/4 | 66.3 | 24.8 |
| 2/3 | 12→8, 24→16, 48→32 | 2/3 | 29.5 | 9.7 | 2/3 | 74.5 | 24.7 |
| 1/2 | 16→8, 24→12, 32→16, 48→24, 96→48, 192→96, 384→192 | ↓2 | 21.0 | 9.5 | ↓2 | 51.0 | 24.5 |
| 3/8 | 32→12 | 3/4 · ↓2 | 41.7 | 11.4 | 3/4 · ↓2 | 93.0 | 28.4 |
| 1/3 | 24→8, 48→16, 96→32 | ↓3 | 41.0 | 9.7 | ↓3 | 101.0 | 24.7 |
| 1/4 | 32→8, 48→12, 96→24, 192→48, 384→96 | ↓2 · ↓2 | 43.0 | 11.8 | ↓2 · ↓2 | 89.0 | 28.8 |
| 1/6 | 48→8, 96→16, 192→32 | ↓3 · ↓2 | 55.0 | 11.3 | ↓3 · ↓2 | 117.0 | 28.3 |
| 1/8 | 96→12, 192→24, 384→48 | ↓4 · ↓2 | 71.0 | 11.4 | ↓4 · ↓2 | 149.0 | 28.4 |
| 1/12 | 96→8, 192→16, 384→32 | ↓4 · ↓3 | 116.0 | 10.9 | ↓6 · ↓2 | 213.0 | 28.4 |
| 1/16 | 192→12, 384→24 | ↓2 · ↓4 · ↓2 | 143.0 | 11.8 | ↓8 · ↓2 | 277.0 | 28.4 |
| 1/24 | 192→8, 384→16 | ↓4 · ↓3 · ↓2 | 169.0 | 11.8 | ↓4 · ↓3 · ↓2 | 303.0 | 29.1 |
| 1/32 | 384→12 | ↓4 · ↓4 · ↓2 | 223.0 | 11.7 | ↓4 · ↓4 · ↓2 | 397.0 | 29.0 |
| 1/48 | 384→8 | ↓4 · ↓4 · ↓3 | 344.0 | 11.1 | ↓4 · ↓6 · ↓2 | 585.0 | 28.8 |

What the pinned lengths changed at v0.7 (the v0.2 arithmetic is in 2.1
and its ledger in section 8, S1): on those pins a relaxed half-band stayed
at m = 7 (7.5 MACs per output, 15 per output as a decimator) at the
divisors 2 to 4, so the chains that used to end in a run of half-bands put
a third-band, 4th-band or mixed stage where the transition is wide — 8/1
is ↑2 · 4/3 · ↑3 (12.5 against 14.1 for three half-bands), 1/8 is
↓3 · 3/4 · ↓2 (105 against 113), 12/1 is ↑2 · ↑6 and 1/12 is ↓6 · ↓2 (the
fewer-stages rule, +5 % over ↑2 · ↑3 · ↑2 / ↓2 · ↓3 · ↓2), and the ↑8 / ↓8
stage moved from the high-rate end to the middle (24/1 is ↑2 · ↑6 · ↑2,
32/1 is ↑2 · ↑8 · ↑2, 1/32 is ↓2 · ↓8 · ↓2, 1/48 is ↓3 · ↓8 · ↓2): at a
divisor of 16 the 8th-band stage was m = 4 (N = 63, 57 nonzero) and a
half-band above it m = 3 (N = 11, 3.5 per output), cheaper than ↓8 at the
top. Decision 6 then kept the single 4th-band stage out (1/16 was
↓8 · ↓2, 165 MACs per output; 16/1 ↑2 · ↑8, 10.3). The `transparent`
columns cost the same chains until X-a4 (3.1). **Since X-a3 (v0.18)** the chains are
re-selected on X17's pins with the 4th-band stage admitted (decision 6 as
amended; X17 had found the half-band's floor at m = 7 to be the designer's
β, 2.1): the 4th band runs at the high-rate end of every chain by 8 and
above, relaxed to m = 4 or 3 — 8/1 and 1/8 are ↑2 · ↑4 / ↓4 · ↓2 (8.9 / 71
MACs per output, from v0.7's chains at 10.4 / 87), 12/1 and 1/12
↑3 · ↑4 / ↓4 · ↓3, 16/1 and 1/16 ↑2 · ↑4 · ↑2 / ↓2 · ↓4 · ↓2 (8.9 / 143,
from 10.2 / 163), 24/1 and 1/24 ↑2 · ↑3 · ↑4 / ↓4 · ↓3 · ↓2, 32/1 and 1/32
↑2 · ↑4 · ↑4 / ↓4 · ↓4 · ↓2, 48/1 and 1/48 ↑3 · ↑4 · ↑4 / ↓4 · ↓4 · ↓3 —
while ×4 stays two half-bands, cheaper than one 4th-band stage (2.1).
**Since X-a4 (v0.19)** the 120 dB tier has chains of its own (X4): at
`transparent` seven of the 26 ratios choose differently — 3/1 is
↑2 · 3/2 (28.0 MACs per output, against 33.7 for ↑3), 12/1 and 1/12
↑2 · ↑6 / ↓6 · ↓2 (17.8 / 213, against 19.2 / 230 for the `economy`
chains run there), 16/1 and 1/16 ↑2 · ↑8 / ↓8 · ↓2 (17.3 / 277, against
18.8 / 301), 48/1 and 1/48 ↑2 · ↑6 · ↑4 / ↓4 · ↓6 · ↓2 (12.2 / 585,
against 12.5 / 602) — and 1/3 stays ↓3 (the fewer-stages rule over
2/3 · ↓2, +3.1 %). The other nineteen ratios run one chain at both tiers.

### 3.4 Within the 44.1 kHz family (20 ordered pairs, 8 ratios)

All eight chains are the 48 family's since X-a3. Until then 8/1 and 1/8
differed, because the intermediate lattices differ relative to r_min —
11.025 · 3 = 33.075 kHz is a 2^a · 3^b multiple of the family's base where
12 · 8/3 = 32 kHz is one of the 48 family's — so the 44.1 family's 8/1 was
↑3 · 8/3 and its 1/8 3/8 · ↓3, two more named chains; with the 4th-band
stage both families run ↑2 · ↑4 / ↓4 · ↓2, whose rates are in both
lattices (`docs/X4_X8_PLAN.md` X6). At the 120 dB tier (X4, X-a4) 16/1 and
1/16 run ↑2 · ↑8 / ↓8 · ↓2 here as in the 48 family.

| Ratio L/M | Pairs (from → to, kHz) | Chain eco | MAC/out eco | Latency eco (out) | Chain tr | MAC/out tr | Latency tr (out) |
|---|---|---|---|---|---|---|---|
| 2/1 | 11.025→22.05, 22.05→44.1, 44.1→88.2, 88.2→176.4 | ↑2 | 10.5 | 19.0 | ↑2 | 25.5 | 49.0 |
| 4/1 | 11.025→44.1, 22.05→88.2, 44.1→176.4 | ↑2 · ↑2 | 10.8 | 47.0 | ↑2 · ↑2 | 22.2 | 115.0 |
| 8/1 | 11.025→88.2, 22.05→176.4 | ↑2 · ↑4 | 8.9 | 91.0 | ↑2 · ↑4 | 18.6 | 227.0 |
| 16/1 | 11.025→176.4 | ↑2 · ↑4 · ↑2 | 8.9 | 189.0 | ↑2 · ↑8 | 17.3 | 455.0 |
| 1/2 | 22.05→11.025, 44.1→22.05, 88.2→44.1, 176.4→88.2 | ↓2 | 21.0 | 9.5 | ↓2 | 51.0 | 24.5 |
| 1/4 | 44.1→11.025, 88.2→22.05, 176.4→44.1 | ↓2 · ↓2 | 43.0 | 11.8 | ↓2 · ↓2 | 89.0 | 28.8 |
| 1/8 | 88.2→11.025, 176.4→22.05 | ↓4 · ↓2 | 71.0 | 11.4 | ↓4 · ↓2 | 149.0 | 28.4 |
| 1/16 | 176.4→11.025 | ↓2 · ↓4 · ↓2 | 143.0 | 11.8 | ↓8 · ↓2 | 277.0 | 28.4 |

### 3.5 48 kHz family → 44.1 kHz family (45 ordered pairs)

`bridge`↓ at k is the 147/160 converter running from 48 · 2^k to
44.1 · 2^k; its MACs and latency are `bridge`'s pinned `economy` /
`transparent` numbers (58 / 184 taps per phase, `design.h:86-89`). Every
row is measured, the k > 0 rows included (2.2 landed before M4). ⚑ (R14):
the `bridge` stage's output rate is ≥ 2× the chain's output rate —
covered, not recommended as a default. The rational stages of a cross row
are designed at their design divisors (3.1): the lattice floor of the
quotient, or 147/160 for the stage at `bridge`'s rate below a 48-family
r_min (48 → 88.2's ↑2, 96 → 44.1's ↓2 and their k > 0 twins). As in 3.3,
"Chain tr" is the 120 dB tier's chain (X4), whose `bridge` stage sits at the
same k and rates as the 70 dB tier's (R13), so a row's ⚑ holds at both.

| From → To | Chain eco | f_pass eco (kHz) | MAC/out eco | Latency eco (ms) | Chain tr | MAC/out tr | Latency tr (ms) | ⚑ |
|---|---|---|---|---|---|---|---|---|
| 8 → 11.025 | ↑2 · ↑3 · bridge↓k0 · ↓2 · ↓2 | 3.000 | 314.9 | 3.09 | ↑2 · ↑3 · bridge↓k0 · ↓2 · ↓2 | 909.9 | 8.07 | ⚑ |
| 8 → 22.05 | ↑2 · ↑3 · bridge↓k0 · ↓2 | 3.000 | 147.0 | 2.22 | ↑2 · ↑3 · bridge↓k0 · ↓2 | 429.4 | 5.84 | ⚑ |
| 8 → 44.1 | ↑2 · ↑3 · bridge↓k0 | 3.000 | 68.0 | 2.02 | ↑2 · ↑3 · bridge↓k0 | 205.2 | 5.46 |  |
| 8 → 88.2 | ↑2 · ↑3 · bridge↓k0 · ↑2 | 3.000 | 38.5 | 2.10 | ↑2 · ↑3 · bridge↓k0 · ↑2 | 112.1 | 5.65 |  |
| 8 → 176.4 | ↑2 · ↑3 · bridge↓k0 · ↑4 | 3.000 | 23.2 | 2.11 | ↑2 · ↑3 · bridge↓k0 · ↑4 | 62.1 | 5.61 |  |
| 12 → 11.025 | ↑2 · ↑2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 321.8 | 2.65 | ↑2 · ↑2 · bridge↓k0 · ↓2 · ↓2 | 921.9 | 6.92 | ⚑ |
| 12 → 22.05 | ↑2 · ↑2 · bridge↓k0 · ↓2 | 4.500 | 160.4 | 2.01 | ↑2 · ↑2 · bridge↓k0 · ↓2 | 467.4 | 5.42 | ⚑ |
| 12 → 44.1 | ↑2 · ↑2 · bridge↓k0 | 4.500 | 69.7 | 1.58 | ↑2 · ↑2 · bridge↓k0 | 208.2 | 4.31 |  |
| 12 → 88.2 | ↑2 · ↑2 · bridge↓k0 · ↑2 | 4.500 | 39.4 | 1.66 | ↑2 · ↑2 · bridge↓k0 · ↑2 | 113.6 | 4.51 |  |
| 12 → 176.4 | ↑2 · ↑2 · bridge↓k0 · ↑4 | 4.500 | 23.7 | 1.67 | ↑2 · ↑2 · bridge↓k0 · ↑4 | 62.8 | 4.47 |  |
| 16 → 11.025 | ↑3 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 334.5 | 2.27 | 3/2 · ↑2 · bridge↓k0 · ↓2 · ↓2 | 939.6 | 6.42 | ⚑ |
| 16 → 22.05 | ↑3 · bridge↓k0 · ↓2 | 6.000 | 166.8 | 1.64 | ↑2 · 3/2 · bridge↓k0 · ↓2 | 480.0 | 4.80 | ⚑ |
| 16 → 44.1 | ↑3 · bridge↓k0 | 6.000 | 72.9 | 1.21 | ↑2 · 3/2 · bridge↓k0 | 214.5 | 3.69 |  |
| 16 → 88.2 | ↑3 · bridge↓k0 · ↑2 | 6.000 | 41.9 | 1.31 | ↑2 · 3/2 · bridge↓k0 · ↑2 | 116.7 | 3.88 |  |
| 16 → 176.4 | ↑3 · bridge↓k0 · ↑4 | 6.000 | 24.5 | 1.29 | ↑3 · bridge↓k0 · ↑4 | 67.4 | 3.63 |  |
| 24 → 11.025 | ↑2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 298.9 | 1.86 | ↑2 · bridge↓k0 · ↓2 · ↓2 | 866.4 | 4.88 | ⚑ |
| 24 → 22.05 | ↑2 · bridge↓k0 · ↓2 | 8.269 | 159.9 | 1.43 | ↑2 · bridge↓k0 · ↓2 | 474.5 | 4.05 | ⚑ |
| 24 → 44.1 | ↑2 · bridge↓k0 | 9.000 | 69.4 | 1.00 | ↑2 · bridge↓k0 | 211.8 | 2.94 |  |
| 24 → 88.2 | ↑2 · bridge↓k0 · ↑2 | 9.000 | 45.2 | 1.22 | ↑2 · bridge↓k0 · ↑2 | 131.4 | 3.49 |  |
| 24 → 176.4 | ↑2 · bridge↓k0 · ↑2 · ↑2 | 9.000 | 27.1 | 1.26 | ↑2 · bridge↓k0 · ↑2 · ↑2 | 75.2 | 3.59 |  |
| 32 → 11.025 | 3/2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 299.7 | 1.78 | 3/2 · bridge↓k0 · ↓2 · ↓2 | 872.9 | 4.76 | ⚑ |
| 32 → 22.05 | 3/2 · bridge↓k0 · ↓2 | 8.269 | 166.8 | 1.34 | 3/2 · bridge↓k0 · ↓2 | 492.3 | 3.80 | ⚑ |
| 32 → 44.1 | 3/2 · bridge↓k0 | 12.000 | 72.9 | 0.91 | 3/2 · bridge↓k0 | 220.6 | 2.69 |  |
| 32 → 88.2 | 3/2 · bridge↓k0 · ↑2 | 12.000 | 46.9 | 1.12 | 3/2 · bridge↓k0 · ↑2 | 135.8 | 3.24 |  |
| 32 → 176.4 | 3/2 · bridge↓k0 · ↑2 · ↑2 | 12.000 | 29.0 | 1.17 | 3/2 · bridge↓k0 · ↑2 · ↑2 | 77.4 | 3.34 |  |
| 48 → 11.025 | bridge↓k0 · ↓2 · ↓2 | 4.134 | 275.0 | 1.67 | bridge↓k0 · ↓2 · ↓2 | 825.0 | 4.52 | ⚑ |
| 48 → 22.05 | bridge↓k0 · ↓2 | 8.269 | 137.0 | 1.03 | bridge↓k0 · ↓2 | 419.0 | 3.03 | ⚑ |
| 48 → 44.1 | bridge↓k0 | 16.538 | 58.0 | 0.60 | bridge↓k0 | 184.0 | 1.92 |  |
| 48 → 88.2 | bridge↓k0 · ↑2 | 18.000 | 42.5 | 0.89 | bridge↓k0 · ↑2 | 136.5 | 2.90 |  |
| 48 → 176.4 | bridge↓k0 · ↑2 · ↑2 | 18.000 | 31.8 | 1.00 | bridge↓k0 · ↑2 · ↑2 | 93.8 | 3.18 |  |
| 96 → 11.025 | ↓2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 314.2 | 1.74 | ↓2 · bridge↓k0 · ↓2 · ↓2 | 907.7 | 4.70 | ⚑ |
| 96 → 22.05 | ↓2 · bridge↓k0 · ↓2 | 8.269 | 160.9 | 1.13 | ↓2 · bridge↓k0 · ↓2 | 460.4 | 3.20 | ⚑ |
| 96 → 44.1 | ↓2 · bridge↓k0 | 16.538 | 80.9 | 0.80 | ↓2 · bridge↓k0 | 239.5 | 2.43 |  |
| 96 → 88.2 | bridge↓k1 | 33.075 | 58.0 | 0.30 | bridge↓k1 | 184.0 | 0.96 |  |
| 96 → 176.4 | bridge↓k1 · ↑2 | 36.000 | 42.5 | 0.44 | bridge↓k1 · ↑2 | 136.5 | 1.45 |  |
| 192 → 11.025 | ↓4 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 383.8 | 1.75 | ↓4 · bridge↓k0 · ↓2 · ↓2 | 1012.2 | 4.66 | ⚑ |
| 192 → 22.05 | ↓4 · bridge↓k0 · ↓2 | 8.269 | 191.4 | 1.11 | ↓4 · bridge↓k0 · ↓2 | 525.7 | 3.19 | ⚑ |
| 192 → 44.1 | ↓2 · ↓2 · bridge↓k0 | 16.538 | 104.8 | 0.85 | ↓2 · ↓2 · bridge↓k0 | 280.9 | 2.52 |  |
| 192 → 88.2 | ↓2 · bridge↓k1 | 33.075 | 80.9 | 0.40 | ↓2 · bridge↓k1 | 239.5 | 1.21 |  |
| 192 → 176.4 | bridge↓k2 | 66.150 | 58.0 | 0.15 | bridge↓k2 | 184.0 | 0.48 |  |
| 384 → 11.025 | ↓4 · ↓2 · bridge↓k0 · ↓4 | 4.134 | 497.6 | 1.59 | ↓4 · ↓2 · bridge↓k0 · ↓2 · ↓2 | 1177.7 | 4.75 | ⚑ |
| 384 → 22.05 | ↓2 · ↓4 · bridge↓k0 · ↓2 | 8.269 | 269.8 | 1.13 | ↓8 · bridge↓k0 · ↓2 | 665.0 | 3.19 | ⚑ |
| 384 → 44.1 | ↓4 · ↓2 · bridge↓k0 | 16.538 | 135.3 | 0.84 | ↓4 · ↓2 · bridge↓k0 | 346.2 | 2.51 |  |
| 384 → 88.2 | ↓2 · ↓2 · bridge↓k1 | 33.075 | 104.8 | 0.42 | ↓2 · ↓2 · bridge↓k1 | 280.9 | 1.26 |  |
| 384 → 176.4 | ↓2 · bridge↓k2 | 66.150 | 80.9 | 0.20 | ↓2 · bridge↓k2 | 239.5 | 0.61 |  |

### 3.6 44.1 kHz family → 48 kHz family (45 ordered pairs)

| From → To | Chain eco | f_pass eco (kHz) | MAC/out eco | Latency eco (ms) | Chain tr | MAC/out tr | Latency tr (ms) | ⚑ |
|---|---|---|---|---|---|---|---|---|
| 11.025 → 8 | ↑2 · ↑2 · bridge↑k0 · ↓3 · ↓2 | 3.000 | 342.3 | 2.91 | ↑2 · ↑2 · bridge↑k0 · ↓3 · ↓2 | 815.7 | 7.24 | ⚑ |
| 11.025 → 12 | ↑2 · ↑2 · bridge↑k0 · ↓2 · ↓2 | 4.134 | 234.5 | 2.48 | ↑2 · ↑2 · bridge↑k0 · ↓2 · ↓2 | 554.8 | 6.09 | ⚑ |
| 11.025 → 16 | ↑2 · ↑2 · bridge↑k0 · ↓3 | 4.134 | 184.6 | 2.10 | ↑2 · ↑2 · bridge↑k0 · ↓3 | 450.3 | 5.24 | ⚑ |
| 11.025 → 24 | ↑2 · ↑2 · bridge↑k0 · ↓2 | 4.134 | 106.8 | 1.68 | ↑2 · ↑2 · bridge↑k0 · ↓2 | 251.9 | 4.05 | ⚑ |
| 11.025 → 32 | ↑2 · ↑2 · bridge↑k0 · 2/3 | 4.134 | 83.3 | 1.61 | ↑2 · ↑2 · bridge↑k0 · 2/3 | 198.2 | 3.94 |  |
| 11.025 → 48 | ↑2 · ↑2 · bridge↑k0 | 4.134 | 47.9 | 1.50 | ↑2 · ↑2 · bridge↑k0 | 116.4 | 3.70 |  |
| 11.025 → 96 | ↑2 · ↑2 · bridge↑k0 · ↑2 | 4.134 | 28.4 | 1.57 | ↑2 · ↑2 · bridge↑k0 · ↑2 | 67.7 | 3.87 |  |
| 11.025 → 192 | ↑2 · ↑2 · bridge↑k0 · ↑4 | 4.134 | 18.2 | 1.57 | ↑2 · ↑2 · bridge↑k0 · ↑4 | 39.9 | 3.84 |  |
| 11.025 → 384 | ↑4 · bridge↑k0 · ↑2 · ↑4 | 4.134 | 12.4 | 1.42 | ↑2 · ↑2 · bridge↑k0 · ↑2 · ↑4 | 24.7 | 3.92 |  |
| 22.05 → 8 | ↑2 · bridge↑k0 · ↓3 · ↓2 | 3.000 | 313.3 | 2.05 | ↑2 · bridge↑k0 · ↓3 · ↓2 | 745.4 | 5.02 | ⚑ |
| 22.05 → 12 | ↑2 · bridge↑k0 · ↓2 · ↓2 | 4.500 | 233.6 | 1.84 | ↑2 · bridge↑k0 · ↓2 · ↓2 | 566.7 | 4.60 | ⚑ |
| 22.05 → 16 | ↑2 · bridge↑k0 · ↓3 | 6.000 | 183.9 | 1.47 | ↑2 · bridge↑k0 · ↓3 | 459.3 | 3.74 | ⚑ |
| 22.05 → 24 | ↑2 · bridge↑k0 · ↓2 | 8.269 | 116.3 | 1.26 | ↑2 · bridge↑k0 · ↓2 | 289.9 | 3.22 | ⚑ |
| 22.05 → 32 | ↑2 · bridge↑k0 · 2/3 | 8.269 | 101.0 | 1.16 | ↑2 · bridge↑k0 · 2/3 | 253.6 | 2.97 |  |
| 22.05 → 48 | ↑2 · bridge↑k0 | 8.269 | 47.6 | 0.86 | ↑2 · bridge↑k0 | 119.4 | 2.20 |  |
| 22.05 → 96 | ↑2 · bridge↑k0 · ↑2 | 8.269 | 29.3 | 0.96 | ↑2 · bridge↑k0 · ↑2 | 69.2 | 2.38 |  |
| 22.05 → 192 | ↑2 · bridge↑k0 · ↑4 | 8.269 | 18.2 | 0.94 | ↑2 · bridge↑k0 · ↑4 | 42.1 | 2.36 |  |
| 22.05 → 384 | ↑2 · bridge↑k0 · ↑4 · ↑2 | 8.269 | 13.6 | 0.96 | ↑2 · bridge↑k0 · ↑8 | 29.1 | 2.36 |  |
| 44.1 → 8 | bridge↑k0 · ↓3 · ↓2 | 3.000 | 283.0 | 1.85 | bridge↑k0 · ↓3 · ↓2 | 693.0 | 4.63 | ⚑ |
| 44.1 → 12 | bridge↑k0 · ↓2 · ↓2 | 4.500 | 195.0 | 1.41 | bridge↑k0 · ↓2 · ↓2 | 473.0 | 3.48 | ⚑ |
| 44.1 → 16 | bridge↑k0 · ↓3 | 6.000 | 155.0 | 1.03 | bridge↑k0 · ↓3 | 389.0 | 2.63 | ⚑ |
| 44.1 → 24 | bridge↑k0 · ↓2 | 9.000 | 97.0 | 0.83 | bridge↑k0 · ↓2 | 243.0 | 2.11 | ⚑ |
| 44.1 → 32 | bridge↑k0 · 2/3 | 12.000 | 86.5 | 0.73 | bridge↑k0 · 2/3 | 218.5 | 1.86 |  |
| 44.1 → 48 | bridge↑k0 | 16.538 | 38.0 | 0.43 | bridge↑k0 | 96.0 | 1.09 |  |
| 44.1 → 96 | bridge↑k0 · ↑2 | 16.538 | 29.5 | 0.63 | bridge↑k0 · ↑2 | 73.5 | 1.60 |  |
| 44.1 → 192 | bridge↑k0 · ↑2 · ↑2 | 16.538 | 20.2 | 0.68 | bridge↑k0 · ↑2 · ↑2 | 46.2 | 1.69 |  |
| 44.1 → 384 | bridge↑k0 · ↑2 · ↑4 | 16.538 | 13.6 | 0.67 | bridge↑k0 · ↑2 · ↑4 | 30.6 | 1.68 |  |
| 88.2 → 8 | ↓2 · bridge↑k0 · ↓3 · ↓2 | 3.000 | 332.6 | 1.93 | ↓2 · bridge↑k0 · ↓3 · ↓2 | 797.7 | 4.82 | ⚑ |
| 88.2 → 12 | ↓2 · bridge↑k0 · ↓2 · ↓2 | 4.500 | 228.1 | 1.49 | ↓2 · bridge↑k0 · ↓2 · ↓2 | 542.8 | 3.68 | ⚑ |
| 88.2 → 16 | ↓2 · bridge↑k0 · ↓3 | 6.000 | 185.3 | 1.14 | ↓2 · bridge↑k0 · ↓3 | 441.4 | 2.82 | ⚑ |
| 88.2 → 24 | ↓2 · bridge↑k0 · ↓2 | 9.000 | 135.6 | 1.04 | ↓2 · bridge↑k0 · ↓2 | 336.7 | 2.66 | ⚑ |
| 88.2 → 32 | ↓2 · bridge↑k0 · 2/3 | 12.000 | 115.4 | 0.95 | ↓2 · bridge↑k0 · 2/3 | 288.8 | 2.41 |  |
| 88.2 → 48 | ↓2 · bridge↑k0 | 18.000 | 62.8 | 0.71 | ↓2 · bridge↑k0 | 177.8 | 2.07 |  |
| 88.2 → 96 | bridge↑k1 | 33.075 | 38.0 | 0.22 | bridge↑k1 | 96.0 | 0.54 |  |
| 88.2 → 192 | bridge↑k1 · ↑2 | 33.075 | 29.5 | 0.31 | bridge↑k1 · ↑2 | 73.5 | 0.80 |  |
| 88.2 → 384 | bridge↑k1 · ↑2 · ↑2 | 33.075 | 20.2 | 0.34 | bridge↑k1 · ↑2 · ↑2 | 46.2 | 0.84 |  |
| 176.4 → 8 | ↓4 · bridge↑k0 · ↓3 · ↓2 | 3.000 | 420.8 | 1.93 | ↓4 · bridge↑k0 · ↓3 · ↓2 | 930.0 | 4.78 | ⚑ |
| 176.4 → 12 | ↓4 · bridge↑k0 · ↓2 · ↓2 | 4.500 | 286.9 | 1.49 | ↓4 · bridge↑k0 · ↓2 · ↓2 | 631.0 | 3.64 | ⚑ |
| 176.4 → 16 | ↓4 · bridge↑k0 · ↓3 | 6.000 | 223.9 | 1.12 | ↓4 · bridge↑k0 · ↓3 | 524.1 | 2.81 | ⚑ |
| 176.4 → 24 | ↓2 · ↓2 · bridge↑k0 · ↓2 | 9.000 | 168.7 | 1.08 | ↓2 · ↓2 · bridge↑k0 · ↓2 | 406.5 | 2.76 | ⚑ |
| 176.4 → 32 | ↓2 · ↓2 · bridge↑k0 · 2/3 | 12.000 | 145.8 | 1.00 | ↓2 · ↓2 · bridge↑k0 · 2/3 | 341.2 | 2.51 |  |
| 176.4 → 48 | ↓2 · ↓2 · bridge↑k0 | 18.000 | 101.4 | 0.82 | ↓2 · ↓2 · bridge↑k0 | 271.5 | 2.35 |  |
| 176.4 → 96 | ↓2 · bridge↑k1 | 36.000 | 62.8 | 0.36 | ↓2 · bridge↑k1 | 177.8 | 1.04 |  |
| 176.4 → 192 | bridge↑k2 | 66.150 | 38.0 | 0.11 | bridge↑k2 | 96.0 | 0.27 |  |
| 176.4 → 384 | bridge↑k2 · ↑2 | 66.150 | 29.5 | 0.16 | bridge↑k2 · ↑2 | 73.5 | 0.40 |  |

Count: 72 + 20 + 45 + 45 = 182 = 14 × 13 ordered pairs, every one covered
and measured; 38 carry ⚑. The generator, `rational/tools/coverage/matrix.py`
(committed at M4), is deterministic: the lattice of 2^a · 3^b multiples of
each family's base as intermediates (b ≤ 3; every band of the
vocabulary, the 4th-band stage since X-a3), the stage lengths from the numpy search on the
16384-point grid at each stage's design divisor (the same criterion as the
M2 pins, each design's β for A + 4 dB since X17; its `pins` mode prints
`design.h`'s relaxation tables), the MACs per output as `stage.h` trims
its rows, the same selection (the MAC minimum, then the fewest stages
whose rational part is within 10 % of its rational part around the same
`bridge` stage, then latency), run once per tier since X-a4 (at `economy`
and at `transparent`), the same flag rule. `matrix.py md` prints 3.3–3.6, `rows` writes
`tests/coverage/matrix_rows.h` (the 182 rows as an X-macro with both
tiers' chains, every stage's divisor and the exact MACs and latency per
profile, each profile's from its tier's chain, which `test_matrix.cpp`
pins), `diff` lists the rows whose chain differs from this file's tables,
per tier (0 after a regeneration), and `rule` the rows where the
fewest-stages rule bites, per tier.

## 4. Layout

```
rational/
├── CMakeLists.txt          project(tap_sr_rational), INTERFACE target tap::sr::rational -> tap::dsp only,
│                           TAP_SR_RATIONAL_WERROR, the tap::dsp guard (../bridge/CMakeLists.txt:20-23)
├── README.md  PLAN.md  CLAUDE.md (charter only, as bridge/CLAUDE.md)
├── include/tap/sr/rational/
│   ├── rational.h          umbrella: TAP_SR_VERSION_* (R11), includes the five below
│   ├── ratio.h             ratio<L, M>, ratio_traits, the 2^a·3^b / gcd static_asserts (R1)
│   ├── design.h            profile (R5), design_stage<R>(profile) over tap::dsp::nyquist.h
│   ├── stage.h             basic_stage<S, Ch, R> over stage_base<S, R> (A13): table (per phase or per decimating branch, trimmed to the nonzero span;
│   │                       symmetry halving measured out after M6: section 6, the levers), the
│   │                       schedule, process/pull/flush/reset, the exact-rational latency
│   ├── chain.h             basic_chain<S, Ch, R...>: tap::dsp::chain over basic_stage, each stage at its design
│   │                       divisor (3.1); the 25 named multi-stage chains of 3.3 / 3.4 (up2_ratio3over2 … down4_down6_down2)
│   └── converter.h         basic_converter<S, Ch, R> = basic_stage<S, Ch, R> under the family's converter name, with the
│                           float / Q15 / Q31 aliases (converter<Ch, R>, converter_q15<Ch, R>, converter_q31<Ch, R>)
├── tests/                  test_ratio, test_design, test_stage, test_chain, test_matrix, test_fixed_point,
│   │                       test_output_hash, test_capi, bare_metal_main.cpp (no cross-validation test: section 5
│   │                       says why the bridge-vs-async leg is not repeated here; struck by the 2026-10 audit, F18)
│   ├── reference/          committed scipy vectors (make_reference_vectors.py in tools/reference/), the
│   │                       single stages and the sequenced-upfirdn chain vectors
│   ├── coverage/           matrix_rows.h, generated by tools/coverage/matrix.py: the 182 rows with
│   │                       divisors and exact pins per profile
│   └── support/            bridge_stage.h, bridge's converter as a sync_stage for the cross rows (4.2)
├── tools/reference/        make_reference_vectors.py; tools/coverage/matrix.py (section 3's generator)
├── bench/                  baselines.json (m33 / m55 / hexagon), icount/{CMakeLists.txt, icount_main.cpp}
├── capi/                   tap_sr_rational_capi.{h,cpp} -> libtap_sr_rational_capi
├── notebooks/              design_spike.ipynb, matrix.ipynb, tap_sr_rational_py.py (the binding)
└── examples/               multirate_chain.cpp (a within-family chain); the
                            cross-family composition with bridge lives here
                            too, since examples/ may name a sibling (4.2)
```

Six public headers (`tests/CMakeLists.txt:10-11` gains
`_header_count_rational 6`). The root `CMakeLists.txt:46-54` adds
`add_subdirectory(rational)` and the umbrella `tap::sr` links all three.

**Family rules extended to a third engine:**

- The dependency-rule loop (`tests/CMakeLists.txt:13-61`) runs over three
  engines; `check_includes.cmake`'s `OTHER` becomes the list of *both*
  siblings, so `rational`'s public headers may name neither `tap/sr/async/`
  nor `tap/sr/bridge/` (and vice versa). Link interface exactly `tap::dsp`;
  header isolation per header; header count pinned.
- `tests/family/version_macros.cpp` includes `rational.h` third and
  `static_assert`s the triple equal (R11).
- `scripts/icount.py`'s `ENGINES` table (`scripts/icount.py:48-50`) gains
  `"rational": {"prefix": "tap_sr_rational_icount_", "done":
  "RATIONAL_ICOUNT_DONE"}`; `ci.yml`'s per-(target, engine) correctness
  and ratchet jobs gain the `rational` label; `style.yml` gains the tree.
- Test names `rational.<Suite>.<Test>`, label `rational`
  (`../bridge/tests/CMakeLists.txt:82-83`'s pattern).
- D14 banners on every C/C++/Python file:
  `Copyright 2026 Timothy Place and the SampleRateTap contributors`.
- `cmake/retired_options.cmake` needs no entry: this engine retires
  nothing.

**C ABI (4.4).** `tap_sr_rational_create(int chain, int profile, unsigned
channels)`, where `chain` is an enumerator naming one of **the 28
within-family chains** of 3.3/3.4 — the 26 of the 48 family and the two
the 44.1 family adds (its 8/1 and 1/8), one enumerator per distinct chain
(`TAP_SR_RATIONAL_UP2`, `TAP_SR_RATIONAL_UP2_UP2`,
`TAP_SR_RATIONAL_RATIO2OVER3`, `TAP_SR_RATIONAL_DOWN8_DOWN2`, …; the set is
the union of the two tables' chain columns: 8 one-stage, 20 multi-stage,
the 20 `chain.h` names as types; v0.2 counted 34 before the union was
taken; since X-a3 the values are stable with gaps, 33 live of 43 assigned since X-a4,
`TAP_SR_RATIONAL_CHAIN_END` for the retired `CHAIN_COUNT`, R16) — a
chain, never a rate pair and never a bare rate, so D12 holds at the C
boundary as `bridge`'s `int direction` does
(`../bridge/capi/tap_sr_bridge_capi.h:29`); the single-stage ratios are
the enumerators of their one-stage chains, not a separate set (R16,
decision 9). The rest mirrors `bridge`'s
eleven functions (`outputs_for`, `frames_needed`, `process`, `flush`,
`flush_output_frames`, `reset`, `latency_output_frames` (numerator and
denominator, R7) and `latency_seconds`, `taps`, `version`). The notebook
composes `bridge` through *its* C ABI for the
cross-family rows (a notebook, like a test, may name a sibling).

## 5. Test strategy — three independent legs, plus the family's

As `bridge` (`../bridge/PLAN.md` §6), typed over `float` / `int16_t` /
`int32_t` with `double` as the oracle:

1. **Golden model vs scipy.** Committed reference vectors per
   (single-stage ratio, profile): `scipy.signal.upfirdn` in float64 over
   the same per-branch-normalized Nyquist design, cast to float32
   (`../bridge/tests/reference/reference_vectors.h:1-4`'s provenance
   header), compared sample for sample from n = 0, transient included.
   The chains are then pinned against the composition of their stages'
   vectors (upfirdn applied in sequence), so the chain leg is independent
   of `chain<>`'s bookkeeping.
2. **Exhaustive phase sweeps and cross-precision.** Every phase of every
   stage (L phases for ↑L, M input phases for ↓M, L phases of the mixed
   stage's schedule): row sums, the mirror identity, DC within one LSB
   (`../bridge/tests/test_phase_table.cpp:43-67`); accounting exact from
   every schedule position (`test_converter.cpp:243-246`'s pattern);
   float, Q15 and Q31 against `double` at pinned eps — Q31 at the format
   limit, Q15 format-limited, stated as measured numbers per stage. The
   `bridge` cross-validation against `async` at pinned eps = L/M − 1
   (`test_cross_validation.cpp:79`) is **not** repeated here: eps for ↑2 is
   −0.5, far outside `async`'s charter; the independent leg for this engine
   is scipy (1) plus `decimate.h`'s battery as a second golden at ↓2 / ↓3 /
   ↓6 (same substrate, different design: a disagreement above the two
   designs' documented difference is a finding).
3. **The coverage-matrix test** (`test_matrix.cpp`, M4). For every one
   of the 182 pairs: build the chains of 3.3–3.6 in `double` from the
   generated row table (`tests/coverage/matrix_rows.h`; since X-a4 a row
   holds a chain per tier, the 70 dB tier's run at the three 70 dB
   profiles and the 120 dB tier's at `transparent`); pin MACs per
   output (the stages' trimmed-row counts scaled to the output rate — what
   the kernels execute, not an estimate) and latency exactly, as
   rationals, at all four profiles, each through its tier's chain; check a
   within-family chain is the named chain (its compile-time divisors are
   the table's), and that 25 rows split per tier; measure the promise
   (2.1) with a tone battery at each profile — seven passband tones and up
   to five stopband tones per row, each an exact bin of an analysis window
   whose length makes every rate of the chain a bin too, so a rectangular
   window measures every image and alias candidate (|k R_j ± f| folded,
   over every rate R_j of the chain) leakage-free beside the 0 dB tone:
   every candidate that lands at or below f_pass must be at or below −A,
   the passband gain within the stages' summed ripple candidates; check
   accounting from three positions, the impulse within half a frame of the
   latency, and flush. Cross-family rows build `bridge` from `tests/`
   (`support/bridge_stage.h`, allowed by 4.2); every row is measured, none
   skipped (2.2 had landed). Measured at M4: worst candidate −71.3 dB at
   `economy`, −71.8 at `super_economy`, −71.5 at `balanced`, −121.6 at
   `transparent`; worst passband deviation 0.0081 / 0.0087 / 0.0070 /
   0.00002 dB in that order (about 1700 tones per profile, 10 to 24 s on a
   host; a host suite, excluded on the QEMU legs). Measured at X-a3 (X17's
   pins, the chains re-selected with the 4th-band stage): −72.3 / −71.7 /
   −72.8 / −123.0 dB, passband deviation 0.0048 / 0.0051 / 0.0047 /
   0.00001 dB (1701, 1691, 1704 and 1704 tones). Measured at X-a4 (the
   120 dB tier's own chains at `transparent`): the 70 dB profiles exactly
   as at X-a3, and −122.97 dB at `transparent`, passband 0.00001 dB (1704
   tones).

Plus:

- **The L-th-band property** (DspTap, `tests/test_nyquist.cpp`):
  `h[c ± kL] == 0.0` exactly for every k, every (L, profile); branch 0 of
  every ↑L table is exactly `{1.0}`; the response's antisymmetry about π/L
  within the design's own floor.
- **DC gain exactly 1 after quantization**: every branch's Q1.14 row sums
  to 16384 and every Q1.30 row to 2^30 (`decimate.h`'s
  `DcGainIsExactInQ15`, `test_decimate.cpp:162`); fixed-point tables
  bit-pinned by row sum and FNV-1a-64 per (ratio, profile)
  (`test_decimate.cpp:301`); full-scale drive saturates without wrapping.
- **Output hashes** printed, never pinned (`test_output_hash.cpp:13-20`'s
  reasoning), for the migration-style A/B gates and the ratchet's
  `checksum=` line.
- **Embedded legs.** Cortex-M33 and M55 under `qemu-system-arm` with a
  one-shot `bare_metal_main.cpp` whose baked filter excludes the
  measurement suites and asserts a selected-test floor
  (`../bridge/tests/bare_metal_main.cpp:17-30`); Hexagon under
  `qemu-hexagon`. The icount ratchet (`scripts/icount.py --engine rational
  --target m33|m55|hexagon`) over about ten fixed workloads — ↑2, ↓2, ↑3,
  ↓3 in float and Q15 at `economy`, the ↓2 · ↓2 chain in Q15, and ↑2 / ↓2
  float at `transparent`, 2 s of stereo each as `bridge`'s
  (`../bridge/bench/icount/CMakeLists.txt`) — two-sided at ±3 % against
  `rational/bench/baselines.json`, guest marker `RATIONAL_ICOUNT_DONE
  ok=1 checksum=…` (R12). Construction is measured as its own scenario
  from the first baseline, so the `async` roadmap's "construction as a
  ratcheted scenario" debt (`../async/PLAN.md` §4.1) is not inherited;
  since 2026-10-10 that scenario is the Q15 by-4 chain's workload with
  its stream removed — construction and the input fixture, then one block
  — as the siblings' construct legs are, so `down2_down2_q15_eco` minus
  it is the streaming loop alone (the fixture's libm `sin()` is
  soft-double on the M33 and belongs in the leg, not the difference).
  Since X-a3 the 4th-band stage has workloads of its own: the matrix's
  by-8 chain `down4_down2_q15_eco`, the worst single call of the ↓4 stage
  in float and Q15 (`callmax_down4_*`), the host's float ↓4 at 2 and 12
  channels, and a second probe set (↑4 and ↓4) for the size and stack
  ceilings (section 6, the X-a3 record). Since X-a4 the 120 dB tier's own
  chains have one: the matrix's 1/16 chain there, ↓8 · ↓2 in float at
  `transparent` (`down8_down2_float_tr`), the first workload to run the
  8th-band stage (section 6, the X-a4 record).

## 6. Milestones

| M | Deliverable | Acceptance | Waits for 2.2? |
|---|---|---|---|
| M0 | **DspTap substrate PR**: `tap/dsp/nyquist.h` (R4's designer and m-search), `tap/dsp/chain.h` (`sync_stage` concept, `chain<>`, composed `outputs_for` / `frames_needed` / latency; `flush` deferred to M4, when a stage with a defined flush exists), tests, README sections, per DspTap's "Adding a primitive" checklist. **Done** (tap/DspTap#48, 2026-10-01; this tree's `submodules/dsptap` pin at its merge) | DspTap CI green on every host and QEMU leg ✓; the L-th-band property test ✓ (exact centre and zeros, per-branch unity, the shifted responses summing to 1); `chain<>` of two `basic_decimator`s matches the two run in sequence bit for bit in every sample format ✓; DspTap's existing icount ratchet unmoved ✓ (every key within +0.9 % of baseline) | no |
| M1 | **Skeleton + ratio types + design**: `rational/` tree (section 4), `tap::sr::rational` target, the family tests extended to three engines, `ratio<L, M>` with its `static_assert`s, `profile`, `design_stage<R>`. **Done** (2026-10-01; three of the six public headers — `rational.h`, `ratio.h`, `design.h` — the header count pinned at 3 until M3/M4 add the rest) | Configure and build from the root and from `cmake -S rational` ✓; the four 4.2 checks and `VersionMacrosAgree` pass for all three engines ✓ (`check_includes.cmake` now takes both siblings); `ratio<5, 1>`, `ratio<4, 2>`, `ratio<2, 2>` fail to compile with the charter's message ✓ (`tests/compile_fail/`, a `try_compile` project run as `rational.Ratio.ChartersFailToCompileWithTheMessage`, on the cross legs with their toolchain file); `BadProfilesThrow` ✓; the M33 leg runs the battery in 134 s with the transparent search excluded | no |
| M2 | **Design spike**: `notebooks/design_spike.ipynb`, executed, pins N per (single-stage ratio ∈ {2, 3, 6, 8, 3/2, 2/3, 4/3, 3/4, 8/3, 3/8} × profile; the 4th-band row is measured for the record only, decision 6, and serves 4/3 and 3/4; since X-a3 it is ↑4 / ↓4's own pin too, decision 6 as amended) by the ≥ 1 dB margin criterion on a fine grid, with measured worst stopband and ripple; `test_design.cpp` enforces the pins. **Done** (2026-10-02, v0.5: the notebook is the independent numpy leg from the same published math, `profile::taps_per_branch` carries the pins per band, `design_stage<R>` reads them, a custom profile without pins is searched on the same grid) | The table below filled ✓; every pinned N of the form 2mL − 1 ✓ (mixed: T = ⌈N / L⌉ per phase); measured A ≥ spec + 1 dB on 16384 points ✓ (−71.1 … −72.8 dB at 70 dB, −121.1 … −121.7 dB at 120 dB) with m − 1 missing it ✓; ripple ≤ 2.3's candidate ✓ (≤ 0.0025 / 0.00001 dB); the scipy `upfirdn` end-to-end check of the economy half-band decimator: a tone just past the stopband edge aliases at −72.1 dB, passband tones within 0.001 dB ✓ | no |
| M3 | **Single stages, float golden leg**: `basic_stage<S, R>` for ↑2 / ↓2 (half-band) and ↑3 / ↓3 (third-band) first, then the rest of the vocabulary; both call shapes; `flush`; scipy vectors committed. **Done** (2026-10-02, v0.6: the whole vocabulary at once, since the two machines are generic — the L-phase schedule machine with trimmed phase rows for interpolators and mixed ratios, the M-branch commutator for decimators, its nonzero branches summed under one finalize through DspTap's `accumulate_row` (tap/DspTap#51, the substrate addition M3 needed); MACs per output are the design's nonzero counts for every interpolator and decimator, the half-band decimator 23 of its 43 taps) | Scipy vectors sample-for-sample ✓ (16 vectors, every ratio at `economy` and the by-2 pair at `transparent`; float within 6e-8 of the float64 reference, the 3e-5 tolerance is bridge's); exhaustive phase sweeps ✓ (the impulse reproduces the table bit for bit from every input phase, every format; accounting exact from every position of every superblock); `PullMatchesProcessBitExact` ✓; chunking invariance ✓ (every format); `decimate.h` second-golden agreement at ↓2 / ↓3 / ↓6 within the documented design difference ✓; latency equals (N − 1)/2 at the higher rate by impulse ✓ (every ratio, the exact rational (N − 1)/(2M) at the output rate); DC gain exactly 1 in every format ✓; flush equals zero padding bit for bit ✓ | no |
| M4 | **Chains and the coverage matrix**: `chain.h`'s named chains, `tools/coverage/matrix.py` committed, `test_matrix.cpp` pinning (f_pass, A, δ, MACs/out, latency) for all 182 rows. **Done** (2026-10-02, v0.7: `basic_chain<S, R...>` constructs each stage at its design divisor (3.1) over DspTap's `chain<>`, whose `flush` landed for it (tap/DspTap#52); the relaxation tables in `design.h`; the generator re-run on the pinned lengths, section 3 regenerated — 35 rows changed chain, the ledger in S1 — and the 20 named multi-stage chains follow it) | Every row measured and pinned, the k > 0 rows included (2.2 had landed, so nothing was skipped) ✓; chain vectors against sequenced upfirdn ✓ (seven chains, float within 3e-5 and double within 1e-7 of the float64 reference); a chain equals its stages run in sequence bit for bit in every format ✓; `frames_needed` / `outputs_for` exact from every position of every named chain ✓; flush equals zero padding bit for bit ✓; the tone battery's numbers in section 5 | no |
| M5 | **Fixed-point profiles**: Q15 and Q31 datapaths through the traits; per-branch quantization; bit-pinned tables; cross-precision numbers. **Done** (2026-10-02, v0.8: the datapaths were M3's — the stage is format-generic over `tap::dsp::sample_traits` — so M5 is the battery that states each format's contract as numbers, `tests/test_fixed_point.cpp`, every ratio at every profile; the measured table and the two limits below) | Q31 within the format floor of float on the reference noise ✓ (worst \|Q31 − double\| 3.4e−9 against float's 5.1e−8, bridge's 5e−8); Q15 format-limited numbers stated per stage ✓ (the table below: RMS −85.6 … −95.8 dBFS, attained stopband per stage); exact-unity row sums ✓ (every phase row, every decimator's whole filter, both formats); wrap safety ✓ (±full-scale noise lands within 8.8e−4 / 1.6e−8 of the clamped double model; full-scale DC of either sign is exactly full scale from every phase); the Q15 half-band's zero taps absent from the dot ✓ (23 MACs of 43 taps at `economy`; no structural zero inside any interpolator's, decimator's or mixed-up stage's dot, Q15 MACs pinned per stage); bit-pinned tables ✓ (FNV-1a-64 per ratio, profile and format, 112 pins) | no |
| M6 | **C ABI, notebook, icount baselines**: `libtap_sr_rational_capi`, `tap_sr_rational_py.py`, `matrix.ipynb` executed through the C ABIs (bridge's for cross rows), `bench/icount/` with baselines on M33 / M55 / Hexagon, README icount table, `CApi.VersionIsBitPacked`. **Done** (2026-10-02, v0.9: `tap_sr_rational_create(chain, profile, channels)` takes one of the 28 named within-family chains of sections 3.3 / 3.4 as a stable `TAP_SR_RATIONAL_*` constant, never a rate (D12); `tap_sr_rational_create_stage(L, M, profile, divisor_num, divisor_den, channels)` is one stage of the vocabulary at the design divisor a matrix row states, the piece a chain through `bridge` composes (R16) — the caller still writes that chain; twelve icount workloads, the measured table below) | Notebook measures the shipping C++ and reproduces the pinned matrix numbers ✓ (all 182 rows × four profiles, 728 of 728 pairs reproduce both the MACs-per-output and the latency pins exactly, the cross rows through `bridge`'s C ABI; the tone battery passes 182 / 182 rows at `economy`, worst stopband −71.34 dB and ripple 0.0081 dB, and 182 / 182 at `transparent`, −121.57 dB and 0.00002 dB); every chain constant equals its named `basic_chain` bit for bit with the same latency, MACs and accounting ✓ (`CApi.EveryChainEnumeratorIsItsNamedChain`); ratchet green two-sided on three targets ✓ (re-run `--exact`: every count equal); `nm -D` symbol set recorded ✓ (exactly the sixteen `tap_sr_rational_*` functions — `create`, `create_stage`, `destroy`, `ratio`, `outputs_for`, `frames_needed`, `process`, `flush`, `flush_output_frames`, `reset`, `latency_output_frames`, `latency_seconds`, `macs_per_output`, `stages`, `stage_taps`, `version` — the library built with hidden visibility so no C++ leaks into the table); **family version 0.5.0** ✓ (all three umbrella headers and the root `project()`; `CApi.VersionIsBitPacked` 0x000500 in every engine) | no |

**The M2 table, measured** (N = 2mB − 1 per band × profile, the worst
stopband on the 16384-point grid in parentheses; the harris estimates the
v0.2 plan carried are struck through beside the first two rows; `design.h`'s
`profile` docstring repeats the table as m). Since v0.17 the table is
X17's re-pin, the designer's β for A + 4 dB (section 6, "The designer's β,
X17"), where the estimates are one tap per branch low; M2's own pins, with
β for A, were 2–8 taps per branch above them:

| Ratio (band B) | super_economy | economy | balanced | transparent |
|---|---|---|---|---|
| ↑2 / ↓2 (2) | **31** / 17 nonzero (−72.4 dB); ~~27~~ | **39** / 21 (−73.1 dB); ~~35~~ | **47** / 25 (−72.5 dB); ~~43~~ | **99** / 51 (−122.2 dB); ~~95~~ |
| ↑3 / ↓3 (3) | **47** / 33 (−73.6 dB); ~~41~~ | **59** / 41 (−72.5 dB); ~~53~~ | **71** / 49 (−72.4 dB); ~~65~~ | **149** / 101 (−122.8 dB); ~~143~~ |
| ↑4 / ↓4 (4; one stage since X-a3, decision 6 as amended; the design serves 4/3 and 3/4 too) | **63** / 49 (−74.4 dB) | **79** / 61 (−72.5 dB) | **95** / 73 (−72.5 dB) | **199** / 151 (−122.9 dB) |
| ↑6 / ↓6 (6) | **83** / 71 (−71.9 dB) | **119** / 101 (−72.6 dB) | **143** / 121 (−72.6 dB) | **299** / 251 (−123.0 dB) |
| ↑8 / ↓8 (8) | **111** / 99 (−72.1 dB) | **159** / 141 (−72.7 dB) | **191** / 169 (−72.7 dB) | **399** / 351 (−123.1 dB) |
| 3/2, 2/3, 4/3, 3/4, 8/3, 3/8 (T = ⌈N / L⌉ per phase over the band's design) | **16 / 24 / 16 / 21 / 14 / 37** | **20 / 30 / 20 / 27 / 20 / 53** | **24 / 36 / 24 / 32 / 24 / 64** | **50 / 75 / 50 / 67 / 50 / 133** |

The ↑ and ↓ designs of one ratio are the same prototype (a Nyquist filter
is its own transpose up to the gain convention), so one pin serves both —
unlike `bridge`, whose two directions are asymmetric
(`../bridge/tests/test_design.cpp:112-119`, `DirectionsAreAsymmetric`).
M2 verified that claim (`Design.UpAndDownOfOneBandAreTheSamePrototype`:
the vectors are bit-identical) before relying on it, and a mixed ratio's
design is its larger factor's band (2/3 and 3/2 share the third-band
design; `test_design.cpp` pins it). The mixed stages' MACs per output are
their trimmed rows' lengths over L — a 2/3 stage at `economy` cost 32.5
on M2's pins (29.5 on X17's), not the 26 the v0.2 arithmetic in 2.1
estimated — and the matrix's figures
were regenerated from these counts at M4 (v0.7, section 3; risk S1).

**The fixed-point profiles, measured** (M5, `test_fixed_point.cpp`; the
reference noise, 480 frames at peak 0.9; Q15's attained stopband is the
quantized Q1.14 table's worst response on the 16384-point design grid;
Q31's equals the design's within 0.1 dB at every stage and profile, and its
worst deviation from double is 2.5e−9 (3.4e−9 at M5); Q15 / float MACs are
the counted MACs per superblock). The table is X17's re-measure (v0.17,
the designer's β for A + 4 dB; ↑4 and ↓4 joined at X-a3, v0.18, their
Q15 table the 4/3 stage's, as 3/2's is ↑3's; the M5 table and the levers'
numbers below are the designs of their day):

| Stage | Q15 stopband eco (dB) | Q15 RMS vs double eco (dBFS) | Q15 / float MACs eco | Q15 stopband tr (dB) | Q15 RMS tr (dBFS) | Q15 / float MACs tr |
|---|---|---|---|---|---|---|
| ↑2 | -70.6 | -95.3 | 21 / 21 | -77.1 | -92.0 | 43 / 51 |
| ↓2 | -70.6 | -97.2 | 21 / 21 | -77.1 | -94.4 | 43 / 51 |
| ↑3 | -71.8 | -95.4 | 41 / 41 | -74.2 | -90.5 | 87 / 101 |
| ↓3 | -71.8 | -97.7 | 41 / 41 | -74.2 | -95.6 | 87 / 101 |
| ↑4 | -73.9 | -93.5 | 61 / 61 | -78.1 | -90.7 | 131 / 151 |
| ↓4 | -73.9 | -97.0 | 61 / 61 | -78.1 | -96.3 | 131 / 151 |
| ↑6 | -71.8 | -93.5 | 101 / 101 | -77.8 | -90.8 | 215 / 251 |
| ↓6 | -71.8 | -98.6 | 101 / 101 | -77.8 | -97.5 | 215 / 251 |
| ↑8 | -72.9 | -92.6 | 141 / 141 | -77.9 | -90.0 | 299 / 351 |
| ↓8 | -72.9 | -99.4 | 141 / 141 | -77.9 | -100.5 | 299 / 351 |
| 3/2 | -71.8 | -95.5 | 41 / 41 | -74.2 | -90.5 | 87 / 101 |
| 2/3 | -73.6 | -96.3 | 59 / 59 | -79.1 | -94.4 | 131 / 149 |
| 4/3 | -73.9 | -93.3 | 61 / 61 | -78.1 | -91.0 | 131 / 151 |
| 3/4 | -73.3 | -96.5 | 79 / 79 | -80.7 | -94.6 | 171 / 199 |
| 8/3 | -72.9 | -92.3 | 141 / 141 | -77.9 | -90.1 | 299 / 351 |
| 3/8 | -73.4 | -97.4 | 159 / 159 | -81.5 | -95.5 | 349 / 399 |

Two limits, stated rather than papered over (both pinned by the battery):

- **A Q15 decimator's stopband was format-limited; the lever after M6
  lifted it.** At M5 its table was h / M, the whole filter quantized as one
  row summing to 2^14 so that DC stayed exact, so each coefficient was M
  times smaller than an interpolator's on the same Q1.14 LSB and the
  quantization noise floor of the response rose by about 20 log10 M: ↓6
  reached −64.7 dB and ↓8 −62.9 dB at `economy`, ↓8 −61.1 dB at
  `transparent`. Since the lever (tap/DspTap#54's
  `tap::dsp::finalize_divided`) each of the M branches of the unscaled
  design (each sums to 1) is quantized at its own unity, which makes the
  Q15 decimator's table bit for bit the Q15 interpolator's of the same band
  (`FixedPoint.Q15DecimatorTableIsTheInterpolatorsTable`), and the 1 / M
  rides the single rounding: a 15- or 17-bit shift for ↓2 and ↓8, an
  exact-at-DC multiply-back for ↓3 and ↓6. Measured after it, on the
  designs of the day: every decimator attains its band's interpolator
  stopband (↓6 −71.5 dB, ↓8 −71.7 dB at `economy`; −76 … −79 dB at
  `transparent`; on X17's designs, the table above, −71.8 and −72.9 dB, and
  −74.2 … −77.9 dB), the
  Q15 RMS deviation from double fell by 5–12 dB, full-scale DC is still
  exact, and ↓3 at `economy` moved from −72.4 to −70.2 dB (still the 70 dB
  tier). Q31 and float are unchanged. The mixed ratios going down had the
  same shrinkage at a smaller factor — each L-phase row normalized to unity
  puts its centre tap near L / M (2/3: 0.67, 3/8: 0.38), so they attained
  −68.2 … −69.7 dB at the 70 dB tiers — and the same remedy at a
  power-of-two gain followed: their Q15 rows sum to G x unity, G the
  largest power of two with G L / M < 2 (2/3 and 3/4: 2, 3/8: 4), shifted
  back out exactly in the rounding (`k_table_gain`). Measured after it,
  every Q15 stage attains the 70 dB tier at every 70 dB profile: the mixed
  ratios going down −70.0 … −72.4 dB (3/4 at `economy` moved from −72.8 to
  −70.0, its tightest; 3/4 at `balanced` from −68.2 to −72.4), their Q15
  RMS from double 4–10 dB lower. On X17's designs (the table above) it
  still holds, with more margin: every Q15 stage −70.4 … −73.9 dB at the
  70 dB profiles (↑4 and ↓4, since X-a3, −72.8 / −73.9 / −70.4 dB at
  `super_economy` / `economy` / `balanced`), the mixed ratios going down −71.8 … −73.8 dB (β for
  A + 3 dB, the other offset measured, would have put ↑3 / ↓3 / 3/2 at
  `economy` at −69.9 dB). What remains format-limited in Q15 is
  `transparent` (−74 … −82 dB on X17's designs, −76 … −83 dB before; not
  120: the outer taps of its long designs round to zero and are trimmed,
  Q15 MACs below float's), so at Q15 `economy` is the pairing, as `bridge` found
  (`../bridge/tests/test_converter_fixed_point.cpp`). The 120 dB promise of
  2.3 and section 3 is float's and Q31's.
- **A mixed ratio going down multiplies its band's structural zeros.** 2/3,
  3/4 and 3/8 run the L-phase machine over a band-M design: the zeros stride
  by M, the rows by L, so every row crosses every M-th zero and the trimmed
  dot holds them — 20 of 65 MACs per superblock for 2/3 at `economy`, 20 of
  87 for 3/4, 22 of 191 for 3/8 (on X17's designs 18 of 59, 18 of 79 and 18
  of 159). Interpolators, decimators and the mixed
  ratios going up (band L, every zero in the centre phase) multiply none.
  The sparse-row lever was built and measured after M6, and declined: input
  i on sub-line i mod M makes each row's M residue classes of lag
  contiguous, so the zeros' class drops out (MACs per output fall to the
  design's nonzero count, 2/3 at `economy` 32.5 → 22.5; no chain of the
  matrix changes), with float, Q15 and Q31 outputs bit-identical. But the
  per-output bookkeeping — M delay lines, a short sub-dot per class — costs
  more than the third of the MACs it saves wherever the dot is cheap: the
  2/3 workload at `economy` ran −22 % (M33) and −25 % (Hexagon) in float
  but +13 % on the M55, and +22 % / +60 % / +24 % in Q15 on M33 / M55 /
  Hexagon, the flagship embedded profile. The matrix's MAC figures count
  what runs: the dense rows.

- **The relaxed designs are the ones a chain runs, and their Q15 tables
  are measured separately** (`FixedPoint.RelaxedQ15TablesStopbandsAreStated`,
  2026-10-09, the audit's F05): every (profile, divisor, band) of the
  relaxation tables in Q15, the worst per profile pinned within 0.5 dB.
  With β for A: super_economy −70.5 dB, economy −67.6 dB, balanced
  −70.0 dB, transparent −72.3 dB, each at the 147/160 divisor (the stage
  at `bridge`'s rate, the widest transition the tables relax to), so
  "every Q15 stage attains the 70 dB tier" held for the vocabulary at its
  own rate and the `economy` half-band relaxed to 147/160 missed it by
  2.4 dB. **With β for A + 4 dB (X17, v0.17) every relaxed Q15 design
  attains the 70 dB tier**, pinned since (`EXPECT_LE` −70 dB per profile):
  super_economy −70.5 dB (↑6 at divisor 4), economy −70.4 dB (↑3 at 2),
  balanced −70.1 dB (↑2 at 147/160), transparent −72.7 dB (↑2 at 147/160);
  the `economy` half-band at 147/160 now attains −72.1 dB, so a chain
  through `bridge` at Q15 `economy` keeps the tier. Since X-a3 the
  relaxation tables hold eight divisors, not ten (9 and 16 left with the
  matrix's last stage at them), and the four worst figures are unchanged;
  the 4th band's own column is measured through ↑4 (until X-a3 through
  4/3, the same table).
- **The 4th-band stage at every divisor the matrix runs it at (X-a3,
  v0.18)** is bit-pinned on its own
  (`FixedPoint.FourthBandTablesAreBitPinnedAtEveryMatrixDivisor`: the Q15
  table, one for ↑4 and ↓4, and the two Q31 tables, at the divisors 2, 3,
  4, 6, 8 and 12 at every profile, 24 rows beside the four at divisor 1
  above, each row summing to exact unity), with its Q15 stopbands
  (`FixedPoint.FourthBandRelaxedQ15TablesStopbandsAreStated`, a host
  suite, each within 0.5 dB and at the 70 dB profiles ≤ −70 dB): −70.9 dB at
  the m = 4 design (divisors 2 to 4 at `economy`, 2 and 3 at
  `super_economy`, 3 and 4 at `balanced`), −73.0 … −73.4 dB at m = 3
  (divisors 6 to 12, and `super_economy`'s 4), −74.7 dB at `balanced`'s
  divisor 2 (m = 5), −82.8 … −84.1 dB at `transparent` (m = 5 to 8); Q31
  keeps each design's spec (−71.1 … −74.5 dB at 70 dB, −122.1 … −124.5 dB at
  120 dB). At divisor 1 Q31 tracks double within 1.9e−9 and float within
  4.5e−8, and Q15's RMS floor is −90.7 … −94.9 dBFS going up and
  −96.3 … −98.7 dBFS going down (the table above). The contract point holds
  for the new stage: every Q15 stage of the vocabulary, ↑4 and ↓4 among
  them, attains the 70 dB tier at its own rate, and so does every relaxed
  Q15 design a chain runs.

**The icount baselines, measured** (M6, `bench/icount/`: 2 s of stereo at
48 kHz streamed in 32-frame blocks, counted under QEMU by the family's
plugin, `bench/baselines.json`, as re-recorded after the levers; the
README carries the full table, the 2/3 workloads included):

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `down2_float_eco` | 366,459,868 | 24,433,817 | 72,735,466 |
| `down2_q15_eco` | 47,494,332 | 19,807,076 | 18,558,597 |
| `up2_float_eco` | 732,659,056 | 43,442,641 | 137,930,790 |
| `up2_q15_eco` | 67,354,970 | 34,150,666 | 28,133,327 |
| `up3_float_eco` | 1,407,620,233 | 75,331,049 | 254,904,021 |
| `up3_q15_eco` | 92,695,852 | 51,156,233 | 39,794,001 |
| `construct_q15_eco` | 26,810,642 | 1,567,568 | 8,341,715 |

The construct row is the 2026-10-10 re-record: the leg became the by-4
chain's workload with its stream removed — construction and the 0.25 s
input fixture, then one block — as the siblings' construct legs are, so
`down2_down2_q15_eco` minus it is the streaming loop alone: 372
instructions per stereo frame on the M33, 277 on the M55, 145 on
Hexagon, with the leg 43 % / 5.6 % / 38 % of the chain's total. The old
construction-only figures (777,433 / 34,522 / 193,048) say what the two
designs and tables cost by themselves: under a million everywhere, which
is why the comparison document's construction column for this engine is
mostly fixture (`docs/COMPARISON.md`).

The streaming rows are the 2026-10-10 re-record (v0.15), an in-band one:
every count sat inside the ±3 % gate, and the rows were re-recorded so the
gate measures from the tree it guards, as `bridge`'s M7e and `async`'s
Hexagon-isolation entries did. The rows had been the 2026-10-02 record
(v0.10), and three commits since had moved them without re-recording, each
traced by rebuilding the M55 workloads at every commit in between
(arm-none-eabi-gcc 13.2.1, CodeLinaro 19.1.5, qemu 8.2.2 with the tree's
plugin — the toolchains that reproduce the committed rows to the
instruction): v0.11's power-of-two row gain for the Q15 mixed ratios going
down (ab8fea3) is the one visible move, `ratio23_q15_eco` +355,117 on the
M55 (+1.93 %, stated in its commit as +1.9 %), +7,346 on the M33 (+0.014 %)
and −224 on Hexagon; v0.13's mirror-exact designer with the `stage.h` snap
removed (d4fff5c) added a constant per workload — on the M55 +76 to every
by-2 row, +112 to every by-3 and 2/3 row, +196 to the two `transparent`
rows and +128 to the by-4 chain, construction-side, since the construct
leg (recorded after it) is exact — and the DspTap bump to tap/DspTap#57
(82cd646, stages reset at construction) added +798 to the chain's
construction. On the M33 the same residue reads +192 on the by-2 rows,
+158 on the by-3 and 2/3 rows, +545 on the `transparent` rows and +1,102 on
the chain; on Hexagon +52, +69, +132 and +2,201; every other commit in the
range re-measures identically on the M55 — the sweep also shows #72's S1
(ff9fb5a) sitting +3.5 … +8.8 % on the interpolator and 2/3 rows until its
`stage.h` commit (391145d) took them back to the instruction, the move the
M55 ratchet caught on that PR and its commit records. `bridge`'s twelve and
`async`'s eight rows re-measure exact (`--exact`) on all three targets, so
only this engine is re-recorded. The construct row is unchanged.

One finding, recorded for the codegen levers, and the first lever it
pulled: at M6, Q15 on the M55 was no faster than float (↓2 25.07 M against
24.58 M, ↑2 47.49 M against 43.44 M), where on the M33 it executes 7.7–15×
fewer instructions and on Hexagon 3.9–6.4× fewer (the ↓2, ↑2 and ↑3
pairs). The cause was the substrate's Q15 dot: arm-none-eabi-gcc 13 left it
one scalar SMLALBB per tap on Helium. DspTap's Helium Q15 kernel
(tap/DspTap#53: eight lanes per VMLALDAVA, bit-exact) took the M55 Q15 rows
above to the counts shown — ↓2 −21 %, ↑2 −28 %, ↑3 −32 %, the by-4 chain
−48 % — with every output bit, every float and Q31 count and every M33 and
Hexagon count unchanged (bridge's Q15 workloads fell 55–62 % and async's
Q15 pipelines 21–42 % on the same pin). Q15 is now 19 % (↓2) to 32 % (↑3)
under float on the M55; what remains per output is the stage's own
bookkeeping around the dot.

**A finding from A3 (2026-10-10), recorded for the levers.** The streaming
rows depend on the constructor being inlined into its caller. GCC
constant-folds the channel count through that inlining into the stage's
`append_frame` / `emit` loops (`for c < m_channels`), and the ratchet's
workloads, which construct with a literal channel count in `main`, price
that specialization. The first A3 tree rejected zero channels through
`tap::dsp::raise`, which GCC kept out of line as a `[[noreturn]]` template;
the constructor grew enough, as GCC estimates size, to stop being inlined,
`main` shrank from 900 to 478 bytes on the M55 with the constructor out of
line at 844, and with no change to the hot path the M55 streaming rows read
+11 … +37 % (`up2_float_eco` +15.6 M, 81 instructions per output frame;
`ratio23_float_eco` +12.2 M; `down2_q15_eco` −4.3 %) and the M33 Q15 rows
+16 … +51 % (`ratio23_q15_eco` +26.8 M), Hexagon (clang) exact throughout;
bisected to that one call, and exact again to the instruction with `raise`
forced inline (DspTap's `TAP_DSP_RAISE_INLINE`, which is why it is). Two
consequences stand. A consumer that constructs a stage behind a boundary GCC
does not inline through (a factory in another translation unit, a runtime
channel count) runs the un-specialized loops and pays these numbers; the
ratchet does not see that build. And the baselines are hostage to the
inliner's estimate of the constructor, as `async`'s ledger already found for
`run<S>()`. The lever this points at — a compile-time channel count on the
stage, or `channels == 1` / `channels == 2` fast paths that hold without the
inlining — is unpulled; it would move every Arm count and wants its own
measurement.

**The codegen levers after M6** (one lever per change, measured, as
`bridge`'s M7 campaign): the Helium Q15 dot above (shipped); the Q15
decimators' per-branch quantization (shipped, a quality lever: the limit
above, lifted); the sparse rows for the mixed ratios going down (built,
measured, declined: the second limit above); and the symmetry-halved table
(declined unbuilt). Halving stores each mirrored phase once and dots it
backward (`tap::dsp::dot_row_reversed`, bit-identical), so it saves table
memory only — and this engine's tables are small (the largest stage, 3/8 at
`transparent`, holds 399 coefficients: 1.6 KB in float, 0.8 KB in Q15),
against `bridge`'s 147 / 160 phases, while on the M55 the reversed Q15 dot
reads its row through a gather, slower than the contiguous load. A
memory-bound consumer can reopen it with these numbers.

**The caller-independent hot path (A12, 2026-11-05): the lever the A3
finding names, pulled.** It surfaced in the family's exceptions-off
configuration (`TAP_SR_NO_EXCEPTIONS_RTTI`, the roadmap's A12). With the
engine inlined into the ratchet's `main` as before, `-fno-exceptions
-fno-rtti` moved the M55 float interpolators +39 … +53 % and the M33 Q15
rows up to +26 %, the output bits identical (the checksums). Read from the
disassembly: DspTap's dot kernel compiles to the same instructions either
way (the kernel alone, both ways, differs in nothing but unwind-table
directives); inlined into a 2 KB `main` whose control flow differed only
by its EH landing pads, GCC's register allocator left the inner MAC loop's
two pointers on the stack, 6 instructions a tap becoming 10, the four extra
being the pointers' load and store. Outlining the per-block entries alone
(`process`, `pull`, `flush` behind `TAP_SR_RATIONAL_NOINLINE`) made the two
configurations identical to the instruction but cost the M55 float
interpolators +5 … +11 %, for two further reasons an out-of-line `this`
creates: every field the loops read was written back and reloaded around
each frame, since `this` is reachable from `compact()`'s `memmove`; and
GCC's `-fversion-loops-for-strides`, on at `-O3`, versioned the
per-channel append for a stride of one. The shape that holds is in
`stage.h`: each block runs against a `cursor` (the stream state and the
hot constants as locals, loaded by `open()`, stored once by `close()`; the
decimator's row spans travel in it, the interpolator reads its spans by
phase from the members — each the cheaper, measured), the history is one
planar allocation (`m_hist`, channels × `k_lines` lines of `m_hist_cap`),
and the entries dispatch the channel count as a compile-time constant for
one and two channels (`process_block<Ch>`, as `bridge`'s `process()` does
for its tap count). Against the baselines before it, M55 Release: every
row within the gate and 17 of 18 improved — Q15 −9.5 … −17.8 %, the
down2·down2 chain −12.9 %, the float interpolators within +0.65 %, ↓3
float −8.4 %, ↓2 float +1.8 % (the one row up; its `transparent` row
+0.9 %) — and identical to the instruction with and without exceptions.
The M33, Hexagon and host rows are the baselines re-recorded with this
change. The footprint lost its `P ch sizeof(std::vector)` term (the
README's table). Measured and declined on the way, on the M55: `emit()`
out of line per output (the configurations identical, the float
interpolators +4.8 %); the cursor with the entries inlined (floats
−1.5 … −2.8 %, but the chain +13.4 % without exceptions and the two
configurations apart again: the caller's register pressure); and
`-fno-version-loops-for-strides` on the outlined entries (1.5 points: the
versioning was not the bulk). The engine's cost is now a property of the
engine; the ratchet's `main` no longer measures its own register allocation.

**The channel count in the type (A13, 2026-11-18): gate 0's option 3, as
shipped.** `Ch` is the type's (R17): the count-independent state and cold
code are `stage_base<S, R>`'s (the constructor, `reset()` and the table,
once per format and ratio), and `process`, `pull` and `flush` are each the
out-of-line block for their count. Measured against gate 0's same-job
reference (2ed199a) on every ratchet target in both configurations
(`docs/channels/a13b_measurements.json`): every checksum equal; the
streaming sums −2.93 % on the M55 (−3.08 % without exceptions), −0.17 % on
the M33, −0.35 % on Hexagon and −2.33 % on the host; every row within gate
0's rule but the three it accepted, which shipped at −2.62 % (the M55's
`up2_q15_eco`), −3.70 % (`up3_q15_eco`) and +1.29 % (the M33's
`down3_float_eco`). The rises above 0.1 % are traced: Hexagon's `up3_q15_eco`
+0.49 % and its float interpolators +0.11 … +0.14 %, the host's
`down2_float_2ch` +0.31 %. Two findings on the way, each measured:

- *`emit()` forced inline under clang only.* Once each block was a function
  of its own, clang for Hexagon left `emit()` out of line in the float
  stages, a call per output frame (+0.7 … +0.8 % on the float
  interpolators); `TAP_SR_RATIONAL_ALWAYS_INLINE` brings them to the figures
  above. GCC inlines it on its own, and forcing it there moved the M55's Q15
  register allocation instead (`ratio23_q15_eco` +1.78 % with exceptions), so
  the macro forces it under clang alone.
- *One M55 row split between the builds.* The M55's `ratio23_q15_eco` runs
  15,341,916 instructions with exceptions and 14,577,966 without (−0.71 and
  −5.65 % against the reference's 15,451,527 in both), the one row of the
  engine more than 0.1 % apart. The two builds' optimized GIMPLE for the block
  is identical; they part in RTL PRE. GCC's `compute_can_copy` (`gcse.cc`)
  runs once per translation unit, emitting a test insn per condition-code
  mode (24 on Arm) into whichever function first reaches PRE: with
  exceptions that is the out-of-line `~stage_base` that `main`'s cleanups
  call, without them the block itself. Its UID counter then sizes PRE's
  expression table differently (93 or 97 buckets), PRE hoists the block's
  two loop invariants (`k_schedule`'s address and the schedule entry's stack
  slot) in the opposite order, and the allocator, numbering the pseudos the
  other way, spills the 64-bit Q15 accumulator in the build without
  exceptions. Measured and declined: the entry's fields read from the table
  (the gap 1.35 %, but Hexagon's Q15 interpolators +0.66 … +0.97 % and the
  M33's `ratio23_q15_eco` +0.80 %: a Q15 output may alias the table, so a
  field read after the stores is reloaded); both fields read into scalars
  before the stores (the M55's floats a further −0.5 … −1.2 points, but
  its Q15 interpolators 6.5 … 7 % apart); the destructor out of line in both builds (PRE then
  identical, the block still apart downstream). The maintainer accepted the
  row with its baseline at the midpoint of the two builds (14,959,941, each
  within ±2.55 %), the second such row in the family after `bridge`'s
  `down_q15_se` (A13's gate 0); re-record it at the midpoint of the two
  builds' counts, never with `--update` from one of them.

The footprint fell on all three targets: the probes' engine `.text`
−15.6 … −34.1 %, their whole images −2.0 … −5.8 %, the M33's call stacks
−16 … −56 B; the engine `.rodata` +54 B on the Arm targets is libstdc++'s
length-error text (`vector::_M_default_append`), which the out-of-line
constructor of each of the probe's two stages now carries. The multi-count
probe (`docs/channels/multicount`, an up-2 and a down-2 stage at each count
of a set in one object) against the reference: one count −11.3 … −39.7 %,
counts 1 and 2 −22.1 … +1.6 % (the M33's `MinSizeRel` float the one rise),
counts 1, 2 and 6 −4.1 … +15.0 % (Hexagon `Release` float +15.0 %, the Arm
targets' `MinSizeRel` +10.4 … +12.0 %), below gate 0's K12 prototype in
every row but one (Hexagon `Release` float at three counts, +84 B). The
gain is measured in instructions only; the hardware confirmation waits for
0.7.0 (`docs/CHANNELS_PLAN.md`, K11).

**The designer's β, X17 (2026-11-20, v0.17).** `docs/X4_X8_PLAN.md` X17,
decided by the maintainer on spike X-0's numbers
(`rational/tools/spikes/x4x8/`, its section 2), landed as X-a2. The
designer took its window's Kaiser β for A dB while the pin search accepts
a design only at A + 1 dB; a short design's sidelobes then sit at −68 to
−70.6 dB, inside the margin, and the search runs long (2.1). X17 takes β
for A + x dB, x fixed, the acceptance unchanged. X-0 measured x = 1 … 4
for MACs and pins; the offset was chosen here between 3 and 4 on what X-0
did not measure: the Q15 / Q31 attained stopbands and floors per stage and
the passband ripple of each design. **x = 4.**

*Where it lives, with no DspTap change.* `design.h`'s
`k_design_beta_offset_db` (4.0) and `design_beta(p)` (`kaiser_beta(A + 4)`)
design every stage; an unpinned profile is searched by DspTap's
`search_nyquist_m(band, p, A + 4, 1 − 4, …)`, which designs with β for
A + 4 and accepts at −(A + 1) dB: the same m as a search with the β as its
own argument at every key the matrix designs (X-0's `margin_trick`, re-run
here for x = 0, 3 and 4 against `pins_beta`: 300 of 300 keys each).
`matrix.py`'s `BETA_OFFSET_DB` and `make_reference_vectors.py`'s carry the
same rule. 0.8.0 stays on DspTap 0.2.0; a β argument in DspTap 0.3.0 can
retire the margin arithmetic.

*The comparison* (x = 0 is the designer before; the pins from DspTap's
designer through X-0's `pins_beta` on the 16384-point grid; the
fixed-point and ripple numbers from `test_fixed_point.cpp`'s and
`test_design.cpp`'s own measurement code built against each x's tables;
GCC 13.3, x86-64; the per-stage numbers are the vocabulary at its own rate,
divisor 1, every profile):

| | β for A (before) | β for A + 3 dB | **β for A + 4 dB (chosen)** |
|---|---|---|---|
| relaxation pins moved, of 200 (`economy`'s, of 50) | — | 102 (23) | 127 (32) |
| `economy`'s pins at divisor 1, bands 2 / 3 / 4 / 6 / 8 | 11 / 11 / 11 / 12 / 12 | 10 / 10 / 10 / 10 / 10 | 10 / 10 / 10 / 10 / 10 |
| mean `economy` MAC/out over the 182 rows, the chains kept (what ships) | 93.66 | 89.45 (−4.5 %) | 87.76 (−6.3 %) |
| the same with R3 re-selecting, decision 6 kept (X-0's column) | 93.66 | 88.30 (−5.7 %) | 86.37 (−7.8 %) |
| rows R3 would move on the new pins (within the families) | 0 | 20 (14) | 26 (20) |
| ×4: ↓2 · ↓2 / ↑2 · ↑2, MAC/out | 53 / 13.25 | 47 / 11.75 | 43 / 10.75 |
| ×8, the matrix's chain (↓3 · 3/4 · ↓2 / ↑2 · 4/3 · ↑3) | 105 / 12.46 | 103 / 12.21 | 87 / 10.38 |
| ×8, three half-bands; ↓4 · ↓2 / ↑2 · ↑4 (X-a3's) | 113 / 14.12; 85 / 10.62 | 83 / 10.38; 83 / 10.38 | 79 / 9.88; 71 / 8.88 |
| ×16, ↓8 · ↓2 / ↑2 · ↑8 | 165 / 10.31 | 163 / 10.19 | 163 / 10.19 |
| Q15 attained stopband, every stage, 70 dB profiles | −70.0 … −73.2 dB | **−69.9** … −74.1 dB | −70.4 … −73.9 dB |
| Q15 attained stopband at `transparent` | −76.1 … −83.1 dB | −74.7 … −80.2 dB | −74.2 … −81.5 dB |
| Q15 RMS from double on the reference noise | −90.2 … −99.8 dBFS | −89.8 … −99.7 dBFS | −90.0 … −100.5 dBFS |
| Q31: worst from double; attained stopband against the design | 3.4e−9; within 0.05 dB | 3.7e−9; within 0.06 dB | 2.5e−9; within 0.05 dB |
| float: worst from double (the test's bound 6e−8) | 5.1e−8 | 5.4e−8 | 5.7e−8 |
| relaxed Q15 designs, worst per profile (se / eco / bal / tr) | −70.5 / **−67.6** / −70.0 / −72.3 dB | −70.7 / **−69.7** / −70.2 / −72.5 dB | −70.5 / −70.4 / −70.1 / −72.7 dB |
| design's worst stopband at divisor 1 (70 dB; 120 dB) | −71.1 … −72.8; −121.1 … −121.7 dB | −71.2 … −73.8; −121.3 … −122.2 dB | −71.9 … −74.4; −122.2 … −123.1 dB |
| passband ripple at divisor 1, worst (70 dB; 120 dB) | 0.0025; 0.000008 dB | 0.0026; 0.000007 dB | 0.0024; 0.000007 dB |
| ripple of the 200 designs: better / worse / same | — | 152 / 42 / 6 | 141 / 52 / 7 |
| ripple of the relaxed designs, worst (se / eco / bal / tr) | 0.0039 / 0.0039 / 0.0040 / 0.000014 dB | 0.0040 / 0.0031 / 0.0032 / 0.000011 dB | 0.0039 / 0.0039 / 0.0043 / 0.000011 dB |
| Q15 chain floors, RMS: 1/48, 48/1, 1/8 (44.1); Q31 1/48 worst | −103.4 / −92.9 / −97.7 dBFS; 2.0e−10 | −102.6 / −92.8 / −99.6 dBFS; 2.8e−10 | −103.5 / −92.6 / −98.3 dBFS; 2.4e−10 |

*The choice, by the rule the maintainer set:* x = 4 unless a contract
point this plan or `rational/CLAUDE.md` states fails at 4 and holds at 3.
None does. One fails at 3 and holds at 4: **"every Q15 stage of the
vocabulary attains the 70 dB tier at its own rate"** — at x = 3 the
third-band `economy` design (m = 10, the same length as at x = 4) leaves
↑3, ↓3 and 3/2 at −69.87 dB, and 2/3 and 3/4 at `balanced` at −69.91 and
−69.98 dB; at x = 4 the worst is 4/3 at `balanced`, −70.42 dB. x = 4 also
lifts the limit the
audit's F05 recorded: every relaxed Q15 design a chain runs attains the
70 dB tier (the `economy` half-band at 147/160 −72.1 dB, from −67.6;
x = 3 leaves the third band there at −69.7), now pinned
(`FixedPoint.RelaxedQ15TablesStopbandsAreStated`, `EXPECT_LE` −70 dB). Q31
holds the design within 0.05 dB and a decade under float's floor at either
offset; float's worst against double rises to 5.7e−8, inside the test's
6e−8.

*Passband ripple does not improve everywhere.* At the stage's own rate the
worst falls (0.0025 → 0.0024 dB at the 70 dB tiers, 0.000008 → 0.000007 dB
at `transparent`), and 141 of the 200 designs of the tables are flatter.
52 are not: 42 because the re-pin shortened them, and 10 at an unchanged
length, all with little ripple to begin with (the 70 dB half-bands at
divisors 12 and 16, 0.0006 → 0.0012 dB at `super_economy`'s 16; the
`transparent` half- and third-bands at divisors 6 to 16, ≤ 0.00001 dB). The worst over the
relaxed designs rises at `super_economy` (0.00385 → 0.00391 dB), `economy`
(0.00385 → 0.00391) and `balanced` (0.00398 → 0.00426) and falls at
`transparent`. Every design stays inside 2.3's bounds (0.01 / 0.0001 dB,
which `test_design.cpp` holds at divisor 1), and the matrix's tone battery
measures flatter chains than before: worst passband deviation 0.0077 dB at
the 70 dB tiers (0.0087 before) and 0.00001 dB at `transparent`
(0.00002), worst image or alias −71.7 dB at `super_economy`, −72.3 at
`economy`, −72.8 at `balanced` and −123.0 at `transparent` (−71.8, −71.3,
−71.5 and −121.6 before).

*The chain selection, frozen for this one PR (the maintainer's decision).*
On the new pins R3 would move 26 rows at `economy`, 20 within the families
and 6 across (`matrix.py frozen`): 8/1 and 1/8 (both families) to three
half-bands, 12/1 and 1/12 to ↑2 · ↑3 · ↑2 and ↓2 · ↓3 · ↓2, 24/1 and 1/24
to ↑2 · ↑2 · ↑2 · ↑3 and ↓3 · ↓2 · ↓2 · ↓2, and 192 → 22.05,
384 → 11.025, 384 → 44.1, 11.025 → 384, 22.05 → 192 and 44.1 → 384 across;
eight named chains would leave the matrix (`up2_ratio4over3_up3`,
`down3_ratio3over4_down2`, `up3_ratio8over3`, `ratio3over8_down3`,
`up2_up6`, `down6_down2`, `up2_up6_up2`, `down2_down6_down2`) and six join
(`up2_up2_up2`, `down2_down2_down2`, `up2_up3_up2`, `down2_down3_down2`,
`up2_up2_up2_up3`, `down3_down2_down2_down2`). At x = 3 it would have been
20 rows (14 within), six leaving and four joining (the same lists without
`up2_up6`, `down6_down2`, `up2_up3_up2`, `down2_down3_down2`). X-a2 keeps
every row's chain instead: `matrix.py`'s `FROZEN_SELECTION` is today's
selection (v0.7 to v0.16), committed as data, the tables, `matrix_rows.h`
and the relaxation pins are regenerated from it (the same design divisors,
re-pinned), and the named chains, the C ABI's constants and
`matrix.ipynb`'s rows are unchanged. **R3's selection is suspended for this
PR only:** X-a3 regenerates it under the new designer with the 4th-band
stage admitted, so the chain set changes once, with X7's stable values,
before the 0.8.0 tag (`docs/X4_X8_PLAN.md` X17, X3, section 8).

*What was regenerated, and how.* `design.h`'s pins and relaxation tables
by `matrix.py pins`; `tests/coverage/matrix_rows.h` by `matrix.py rows`;
3.3–3.6 by `matrix.py md` (`diff` against them: 0 rows); the scipy vectors
by `tools/reference/make_reference_vectors.py` (`k_input` unchanged);
`test_fixed_point.cpp`'s 56 rows (the FNV-1a-64 of every Q15 and Q31
table, the MACs, the Q15 stopbands and RMS floors) and its stated floors
by the test's own measurement code; `test_design.cpp`'s M2 table and the
chain and stage pins from the shipping C++; the coverage figure by
`scripts/book_figures.py coverage-matrix`. `design_spike.ipynb` is M2's
record of the pins found then, with β for A, and stays as it was executed.

*The ratchet, re-recorded (every `rational` row; `bridge`'s and `async`'s
re-measure exact in the exceptions build on every target, and their
ceilings equal).* Both builds were counted on every key, with the CI
toolchains (arm-none-eabi-gcc 13.2.1, QEMU 8.2.2 with the tree's plugin
over the pinned `qemu-plugin.h`, CodeLinaro clang 19.1.5 and the
plugin-enabled `qemu-hexagon`, GCC 13.3 and callgrind for the host). A row
whose two builds agree within 0.1 % takes the exceptions build's count, as
the file always has; the one row they split by more, the M55's
`ratio23_q15_eco` (15,140,167 with exceptions, 14,312,206 without: 5.5 %
apart, as at A13), is at their midpoint, 14,726,186, each build within
±2.81 %. The counts fall with the MACs: the float rows −4.8 … −8.8 % at
`economy` and −14.7 … −18.8 % at `transparent` (the half-band's m 31 →
25), the Q15 rows −0.1 … −7.0 %, the host's −6.0 … −7.5 %. One row rises:
Hexagon's `ratio23_q15_eco`, +3.12 % with 9 % fewer MACs (2/3 at `economy`
is 30 taps a row, from 33); not investigated further.

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `callmax_down2_float` | 107,977 (-8.45 %) | 7,605 (-5.00 %) | 21,543 (-8.62 %) |
| `callmax_down2_q15` | 6,685 (-3.69 %) | 5,624 (-1.13 %) | 3,763 (-6.56 %) |
| `callmax_up2_float` | 210,879 (-8.55 %) | 12,524 (-5.83 %) | 40,873 (-8.65 %) |
| `callmax_up2_q15` | 11,488 (-3.49 %) | 9,212 (-0.35 %) | 5,880 (-5.22 %) |
| `construct_q15_eco` | 26,681,136 (-0.43 %) | 1,560,152 (-0.24 %) | 8,303,115 (-0.32 %) |
| `down2_down2_q15_eco` | 52,537,300 (-3.12 %) | 24,216,725 (-0.50 %) | 19,567,759 (-6.99 %) |
| `down2_float_eco` | 343,562,765 (-7.88 %) | 23,353,785 (-4.75 %) | 66,524,317 (-7.30 %) |
| `down2_float_tr` | 790,228,053 (-18.49 %) | 40,912,993 (-14.65 %) | 142,826,681 (-17.77 %) |
| `down2_q15_eco` | 45,602,994 (-1.42 %) | 16,879,879 (-0.28 %) | 16,905,026 (-2.79 %) |
| `down3_float_eco` | 434,872,408 (-8.20 %) | 27,445,097 (-5.34 %) | 82,591,396 (-7.78 %) |
| `down3_q15_eco` | 48,745,572 (-1.73 %) | 17,640,706 (-0.28 %) | 18,300,152 (-3.29 %) |
| `ratio23_float_eco` | 538,739,305 (-8.78 %) | 30,545,005 (-7.02 %) | 115,766,432 (-7.78 %) |
| `ratio23_q15_eco` | 48,128,267 (-2.57 %) | 14,726,186 (-1.56 %) | 19,232,880 (+3.12 %) |
| `up2_float_eco` | 650,842,262 (-8.26 %) | 38,707,334 (-5.65 %) | 123,990,515 (-7.75 %) |
| `up2_float_tr` | 1,542,330,040 (-18.83 %) | 73,546,460 (-15.93 %) | 275,750,044 (-18.21 %) |
| `up2_q15_eco` | 61,032,738 (-1.98 %) | 28,687,571 (-0.17 %) | 25,064,644 (-3.37 %) |
| `up3_float_eco` | 1,243,186,839 (-8.74 %) | 66,371,040 (-6.51 %) | 229,335,777 (-8.31 %) |
| `up3_q15_eco` | 82,905,394 (-2.81 %) | 42,624,043 (-0.12 %) | 36,815,849 (-4.28 %) |

The host key: `down2_float_12ch` 146,817,769 (−6.60 %), `down2_float_2ch`
26,912,629 (−6.03 %), `up2_float_12ch` 256,387,855 (−7.48 %),
`up2_float_2ch` 48,551,143 (−6.65 %), identical in both builds.

*The ceilings, re-recorded (`ceilings.py measure --update`):* the probes'
engine `.text` and their images grow by 48 B on the M33, 32 B on the M55
and 64 B on Hexagon in every format, the β offset's two additions (the
search's attenuation and the design's β for A + 4) in the
construction-time design code — soft double on the Arm targets — that every
probe links; `.rodata` and every stack figure are unchanged.

*Counts for the next PRs* (band 4 admitted in scratch only, x = 4,
against these tables) are in `docs/X4_X8_PLAN.md` X17, "What it does to
X3 and X4".

**The 4th-band stage admitted, X-a3 (2026-11-21, v0.18).**
`docs/X4_X8_PLAN.md` X3, decided by the maintainer (decision 6 as amended,
section 9; R3). What X-a2 froze is released and the vocabulary gains a band
in one step, so the chain set changes once.

*The selection.* `matrix.py`'s `FROZEN_SELECTION` and its `frozen` mode are
gone and `best_chain` selects again, over every band of the vocabulary (the
4th band's single-stage exclusion is gone with them). On X17's pins it
moves **46 rows at `economy`, 30 within the families and 16 across**
(`matrix.py diff` against v0.17's tables; the ledger in section 8, S1),
exactly what X-a2 measured in scratch, every new chain running the 4th-band
stage, ×4 itself staying two half-bands. The mean `economy` MAC/out over
the 182 rows falls from 87.76 to 84.59 (−3.6 %), the dearest row from 514.5
to 497.6 (384 → 11.025) and the cheapest from 7.7 to 7.0 (12 → 384). The
fewer-stages rule bites on 18 rows (3.1). The tone battery: every row meets
its promise at every profile, worst candidate −72.3 / −71.7 / −72.8 /
−123.0 dB (`economy` / `super_economy` / `balanced` / `transparent`),
passband 0.0051 dB at worst (section 5); `matrix.ipynb`, re-executed
through the C ABIs, reproduces all 728 (row, profile) pins and 182 / 182
rows at `economy` and `transparent`.

*The names and the C ABI (X7).* Twelve chains join (`up2_up4`, `up3_up4`,
`up2_up4_up2`, `up2_up3_up4`, `up2_up4_up4`, `up3_up4_up4` and their
mirrors, C values 28 to 39) and ten are retired under D7, their names and
constants in `scripts/retired_words.txt` and their values (10, 11, 14, 15,
16, 21, 22, 25, 26, 27) never reused: `tap_sr_rational_create` refuses
them through a table before any factory runs. `up2_up6`, `up2_up8`,
`down6_down2` and `down8_down2` (12, 13, 23, 24) run no row at `economy`
now and are kept, since the per-tier matrix (X-a4) selects them at
`transparent`. `TAP_SR_RATIONAL_CHAIN_COUNT` is retired for
`TAP_SR_RATIONAL_CHAIN_END` (40), one past the largest value ever assigned;
`CApi.ChainValuesAreStableAndRetiredOnesAreRefused` pins it, every retired
value refused and every live one constructing in all three formats.
`create_stage` takes 4/1 and 1/4 (`CApi.StageConstructorAcceptsTheFourthBand`),
so the cross rows that carry a single ↓4 or ↑4 beside `bridge` build
through the ABI. The ctypes binding maps names through an explicit table.
The exported symbol set is unchanged (the constants are macros).

*What was regenerated, and how.* `design.h`'s relaxation tables by
`matrix.py pins` (eight divisors from ten: no stage of the new selection
runs at 9 or 16, so those rows left with it; every remaining pin
unchanged); `tests/coverage/matrix_rows.h` by `matrix.py rows`; 3.3–3.6 by
`matrix.py md` (`diff` 0); the scipy vectors by
`tools/reference/make_reference_vectors.py`, which gained ↑4 and ↓4 and the
chains `up3_up4_up4`, `down4_down4_down3`, `down4_down2` and `up2_up4` for
the retired three (the 4th band at the divisors 1, 2, 3 and 12; the other
vectors byte-identical); `test_fixed_point.cpp`'s 4th-band rows and its
band-4 table at every matrix divisor by the test's own measurement code;
the coverage figure by `scripts/book_figures.py coverage-matrix`. The
fixed-point numbers are in the table and the bullets above; the chain
floors move with the chains (Q15 RMS from double on the reference noise,
`economy`): 1/48 (↓4 · ↓4 · ↓3) −100.6 dBFS, worst 1.4e−5; 48/1
(↑3 · ↑4 · ↑4) −93.7 dBFS, 8.2e−5; 1/8 (↓4 · ↓2) −97.8 dBFS, 2.9e−5; Q31
1/48 2.4e−10 (the retired chains measured −103.5 / −92.6 / −98.3 dBFS and
2.4e−10). The footprint test covers all sixteen stages and the 22 named
chains: 3.3 KB to 10.9 KB at `economy` (f32 stereo, `up3_up4_up4` the
largest), 11.8 KB at `transparent`.

*The ratchet (every key, both builds, the CI toolchains as at X17).* The
existing rows do not move in `bench/baselines.json`: measured against it,
every row of every engine is exact in the exceptions build but two of this
engine's, `construct_q15_eco` and `down2_down2_q15_eco`, 14 instructions
fewer on the Arm targets and 8 on Hexagon (the construction's scan of the
relaxation table, two rows shorter); `bridge`'s and `async`'s counts and
checksums equal X17's in both builds on every key. The new rows, recorded
from the exceptions build (the two builds agree within 0.001 % on each):

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `callmax_down4_float` | 155,453 | 9,708 | 30,609 |
| `callmax_down4_q15` | 8,101 | 6,241 | 5,603 |
| `down4_down2_q15_eco` | 47,659,519 | 18,466,975 | 16,790,347 |

and on the host `down4_float_2ch` 34,142,581 and `down4_float_12ch`
194,807,923 (identical in both builds). The by-8 chain counts 9 %, 24 % and
14 % fewer instructions than the by-4 chain `down2_down2_q15_eco` on the
same input.

*The ceilings.* A second probe set measures the 4th-band pair, an `up4`
and a `down4` stage, as the first measures the by-2 pair
(`tap_sr_rational_probe4_<fmt>`, `stack4_<fmt>`; `ceilings.py`'s
`PROBE_SETS`), its figures recorded with `--update` and merged under the
keys `<fmt>_by4`: engine `.text` 9,308 / 10,836 / 10,512 B on the M33 (f32 /
Q15 / Q31), 8,036 / 9,524 / 9,276 on the M55, 22,356 / 26,068 / 23,952 on
Hexagon; the `down` call's stack 200 / 208 / 184 B static on the M33. The
by-2 probes' figures are not re-recorded: their `.rodata` and images
measure 80 B below the record on every target (the relaxation tables' two
rows) and Hexagon's f32 `.text` 16 B below, inside every ceiling; their
`.text` and stacks are unchanged.

**A chain per tier, X-a4 (2026-11-21, v0.19).** `docs/X4_X8_PLAN.md` X4,
decided by the maintainer (v5, its scope v6, re-sized on X17's pins v7);
R3 and 3.1. The matrix's `transparent` columns cost the `economy` chains
until now; the generator selects a chain per attenuation tier instead.

*The selection.* `matrix.py` runs `best_chain` once per tier (`TIERS`): at
`economy` for the chain a row runs at `super_economy`, `economy` and
`balanced`, and at `transparent` for the 120 dB tier's. The `economy`
selection is unchanged (`diff` 0 at that tier, `rule` printing X-a3's 18
rows exactly); at `transparent` the chain differs in **25 rows, 17 within
the families and 8 across** (`matrix.py diff` against v0.18's tables,
which carried one chain per row), exactly what X-a2 measured in scratch and
X-a3 re-measured. Within the 48 family (15 rows): 3/1 ↑3 → ↑2 · 3/2 (8 → 24,
16 → 48, 32 → 96), 12/1 ↑3 · ↑4 → ↑2 · ↑6 (8 → 96, 16 → 192, 32 → 384),
16/1 ↑2 · ↑4 · ↑2 → ↑2 · ↑8 (12 → 192, 24 → 384), 48/1 ↑3 · ↑4 · ↑4 →
↑2 · ↑6 · ↑4 (8 → 384), 1/12 ↓4 · ↓3 → ↓6 · ↓2 (96 → 8, 192 → 16,
384 → 32), 1/16 ↓2 · ↓4 · ↓2 → ↓8 · ↓2 (192 → 12, 384 → 24), 1/48
↓4 · ↓4 · ↓3 → ↓4 · ↓6 · ↓2 (384 → 8); within the 44.1 family (2) 16/1 and
1/16 the same; across (8) 16 → 11.025 (↑3 → 3/2 · ↑2 ahead of `bridge`),
16 → 22.05 / 44.1 / 88.2 (↑3 → ↑2 · 3/2 ahead of it), 384 → 11.025 (↓4 →
↓2 · ↓2 after it), 384 → 22.05 (↓2 · ↓4 → ↓8 ahead of it), 11.025 → 384
(↑4 → ↑2 · ↑2 ahead of it) and 22.05 → 384 (↑4 · ↑2 → ↑8 after it). On
those rows the `transparent` cost falls 2.5 to 16.8 % (3/1 33.7 → 28.0
MACs per output, 1/16 301 → 277, 384 → 11.025 1239.7 → 1177.7); the mean
`transparent` MAC/out over the 182 rows falls from 211.89 to 210.18
(−0.8 %), and the 70 dB profiles' means are unchanged. Latency moves with
the chain, up or down (3/1 74 → 85 output frames, 16/1 471 → 455, 1/48
27.3 → 28.8). The fewer-stages rule bites on 25 rows at `transparent`
(3.1); `diff` reports 0 at both tiers after the regeneration. The tone
battery: every row meets its promise at every profile, the 70 dB profiles'
worst exactly as at X-a3 and −122.97 dB at `transparent` (section 5);
`matrix.ipynb`, re-executed through the C ABIs, reproduces all 728 (row,
profile) pins, each through its tier's chain, and 182 / 182 rows meet the
promise at `economy` (worst candidate −72.31 dB, 48 → 8) and at
`transparent` (−123.23 dB, 44.1 → 32; −123.17 at X-a3).

*The row format.* `tests/coverage/matrix_rows.h` carries both chains per
row: `ROW(src, dst, flagged, (70 dB tier's stages), (divisors), (120 dB
tier's stages), (divisors), (MACs, latency) × 4 profiles)`, each profile's
numbers its tier's chain's (X-0's prototype, `generator/rows_pertier.txt`).
`test_matrix.cpp` builds both chains of every row, pins each profile
through its tier's chain, checks every within-family chain is the named
chain, streams both, pins the 25 split rows (17 within) and the flag at
both tiers, and runs the tone battery through the tier's chain; the
notebook and `scripts/book_figures.py` read the per-tier selection (the
figure, at `economy`, is unchanged).

*The names and the C ABI (X7).* Seven within-family chains serve only the
120 dB tier: `up2_up6`, `up2_up8`, `down6_down2` and `down8_down2`, which
X-a3 kept for it (C values 12, 13, 23, 24), and three new ones,
`up2_ratio3over2` (3/1), `up2_up6_up4` (48/1) and `down4_down6_down2`
(1/48), at the C values 40 to 42 (`TAP_SR_RATIONAL_UP2_RATIO3OVER2`,
`_UP2_UP6_UP4`, `_DOWN4_DOWN6_DOWN2`). `TAP_SR_RATIONAL_CHAIN_END` is 43:
33 live values, the 25 named chains and the eight one-stage chains, the
ten retired values unchanged. `CApi.ChainValuesAreStableAndRetiredOnesAreRefused`
pins `CHAIN_END` and every live value constructing in all three formats,
`CApi.EveryChainEnumeratorIsItsNamedChain` the three new constants bit for
bit, and the binding's table names them. No alias and no alias constant
(X-b's), and the exported symbol set is unchanged.

*What was regenerated, and how.* `tests/coverage/matrix_rows.h` by
`matrix.py rows`; 3.3–3.6 by `matrix.py md` (`diff` 0 at both tiers);
`design.h`'s relaxation tables by `matrix.py pins`, unchanged: the 120 dB
tier's chains run no divisor the 70 dB tier's do not (147/160, 1, 2, 3, 4,
6, 8 and 12), and every profile's table keeps the union, so every named
chain constructs at every profile from the tables. The scipy vectors are
not regenerated: `make_reference_vectors.py`'s chains are a fixed list at
`economy`, independent of the selection. The coverage figure is the
70 dB tier's at `economy`, unchanged: `scripts/book_figures.py
coverage-matrix` (matplotlib 3.11.2) reproduces the committed SVG but for
its timestamp, so the file stays.

*Fixed point.* The 120 dB tier's chains run two designs no row ran before:
the 6th and the 8th band at divisor 2 at `transparent` (↑6 / ↓6 in 12/1,
48/1 and their mirrors, ↑8 / ↓8 in 16/1 and 1/16), both m = 8 (N = 95 and
127; 81 and 113 MACs per superblock in float and Q31, 71 and 99 in Q15,
whose outer taps round away). Every other design a `transparent` chain
runs, the 4th band at 12 among them, is one the 70 dB tier's chains already
ran at `transparent`. Their tables are bit-pinned in the style of the
4th-band rows (`FixedPoint.TheTransparentTiersNewTablesAreBitPinned`: the
Q15 table, the decimator's being the interpolator's, and the Q31 tables of
both, every row at exact unity), with the Q15 stopbands they attain on the
16384-point grid from the relaxed edge (a host suite): −85.0 dB (band 6)
and −82.6 dB (band 8), the format's floor, short of the 120 dB promise; Q31
keeps the spec (−122.8 / −122.1 dB). Q15 RMS from double on the reference
noise: ↑6 −94.2 dBFS, ↓6 −99.1, ↑8 −93.5, ↓8 −99.3; Q31 within 1.6e−9.

*The ratchet and the ceilings.* No existing row moves. X-a4 changes no
design table, stage or chain that a workload or a probe runs (the new chains
and their C ABI factories are in neither), so every count and checksum of
every engine on every key (M33, M55, Hexagon, host), in both builds, is
X-a3's measurement exactly, with the CI toolchains as at X-a3; every gate
passes, and every ceiling of the three engines measures as X-a3 left it. No
existing baseline or ceiling is re-recorded. One workload joins, at the
maintainer's direction: `down8_down2_float_tr`, the matrix's 1/16 chain at
its 120 dB tier (↓8 · ↓2 in float at `transparent`), the first workload to
run a chain only that tier selects and the first to run the 8th-band
stage, on the M33, M55 and Hexagon (no host row and no `callmax_`
scenario). Its input is the other float workloads' (2 s of stereo, the
997 Hz sine drawn at 48 kHz, 32-frame blocks), so its count compares with
`down2_float_tr`'s on the same stream. Both builds were counted on every
key and agree within 0.01 %, so the file takes the exceptions build's
count, as it always has:

| Key | With exceptions | Without exceptions or RTTI | Recorded |
|---|---:|---:|---:|
| Cortex-M33 | 538,963,615 | 538,961,776 | 538,963,615 |
| Cortex-M55 | 31,889,072 | 31,889,156 | 31,889,072 |
| Hexagon | 104,910,201 | 104,903,330 | 104,910,201 |

That is 0.68, 0.78 and 0.73 of `down2_float_tr`'s count on the same input
for 0.68 of its MACs (277 per output at 3 kHz against 51 at 24 kHz, per
channel). The README's icount table is regenerated from the file
(`scripts/update_icount_docs.py`).

**Order and gating.** M0 lands in DspTap and the family bumps its pin
(`../CLAUDE.md`, "Substrate discipline"); M1–M6 are done; the 2.2
follow-up landed before M4, so nothing waits for it. Every milestone's PR
carries its measurements, as `bridge`'s M7 entries do.

## 7. Non-goals

- **Ratios with a factor other than 2 or 3**: 37.8 and 50.4 kHz (7), and
  anything the `static_assert` in R1 rejects.
- **The 1000/1001 pull-down rates**: synchronous, not 2^a · 3^b, and never
  `async`'s either (`../PLAN.md:107-109`, `../async/PLAN.md` §1).
- **Crossing 44.1 ↔ 48 inside this engine**: that is `bridge`, reached by
  the chains of 3.5/3.6, written by the caller.
- **Absorbing a clock**: `async`, by composition only, and only as an
  example (`../bridge/examples/bluetooth_bridge.cpp`'s pattern with a
  `rational` stage in front, if a consumer asks).
- **Absorbing `decimate.h`** (`../PLAN.md:61,70`): it stays in DspTap with
  its contract for MuTap; this engine's ↓2 / ↓3 / ↓6 are a different design
  (symmetric transition, zero taps) and a second golden for each other.
- **Routing by rate**, including through the C ABI, the notebook, or the
  matrix (which documents chains and dispatches nothing, D12).
- **A `(f_pass, A)` other than the four profiles' per chain**: custom
  profiles exist as `bridge`'s do (runtime-length instantiation), but the
  matrix pins only the named four.
- **352.8 kHz, 768 kHz, 7.35 kHz**: not in the 14; adding a rate is a
  family-plan change (384 kHz stays, decision 5, R15).
- ~~**A single 4th-band stage**~~ (`ratio<4, 1>`, `<1, 4>`): a non-goal
  until X-a3, which admitted it on the matrix's cost (decision 6 as
  amended 2026-11-21, R3, `docs/X4_X8_PLAN.md` X3).
- **Optimization levers** before M6: superblock codegen, committed trip
  counts and symmetry halving are inherited from `bridge`'s pattern at M3
  where they cost nothing (the stage reuses `phase_table.h`'s layout and
  `converter.h`'s dispatch); anything beyond is M7+, one lever per change,
  measured, as `bridge`'s campaign.

## 8. Risks

| # | Risk | Mitigation |
|---|---|---|
| S1 | The harris estimates mislead the factoring policy (R3): a pinned N lands on the other side of a stage-count decision | M2 pins before M4 chooses; `matrix.py` is re-run on the pinned Ns and the matrix table is regenerated from measured lengths, not estimates; a flip is recorded in this plan's ledger. **Ledger (M4, v0.7): 35 of the 182 rows changed chain** (`matrix.py diff` against the v0.6 tables). Within the 48 family, eleven ratios: 8/1 ↑2 · ↑2 · ↑2 → ↑2 · 4/3 · ↑3; 12/1 ↑2 · ↑2 · ↑3 → ↑2 · ↑6; 16/1 ↑2 · ↑2 · ↑2 · ↑2 → ↑2 · ↑8; 24/1 ↑3 · ↑8 → ↑2 · ↑6 · ↑2; 32/1 ↑2 · ↑2 · ↑8 → ↑2 · ↑8 · ↑2; 48/1 ↑2 · ↑3 · ↑8 → ↑2 · ↑8 · ↑3; 1/8 ↓2 · ↓2 · ↓2 → ↓3 · 3/4 · ↓2; 1/12 ↓3 · ↓2 · ↓2 → ↓6 · ↓2; 1/16 unchanged; 1/24 ↓8 · ↓3 → ↓2 · ↓6 · ↓2; 1/32 ↓8 · ↓2 · ↓2 → ↓2 · ↓8 · ↓2; 1/48 ↓8 · ↓3 · ↓2 → ↓3 · ↓8 · ↓2 (22 rows). Within the 44.1 family, 8/1 ↑2 · ↑2 · ↑2 → ↑3 · 8/3, 16/1 ↑2 · ↑2 · ↑2 · ↑2 → ↑2 · ↑8 and 1/8 ↓2 · ↓2 · ↓2 → 3/8 · ↓3 (5 rows). Cross-family, eight rows: 192 → 22.05 (↓2 · ↓2 → ↓3 · 3/4 ahead of `bridge`), 384 → 11.025 (↓8 → ↓2 · ↓2 · ↓2), 384 → 22.05 (↓6 · 3/4 → ↓8), 384 → 44.1 (↓2 · ↓2 · ↓2 → ↓3 · 3/4 · ↓2), 11.025 → 384 (↑8 → ↑2 · ↑2 · ↑2 after `bridge`), 22.05 → 192 (↑2 · ↑2 → 4/3 · ↑3), 22.05 → 384 (4/3 · ↑6 → ↑8), 44.1 → 384 (↑2 · ↑2 · ↑2 → ↑2 · 4/3 · ↑3). The cause was 2.1's: on those pins a relaxed half-band stayed at m = 7 while a relaxed third-band stage was m = 5 — a floor X17 (v0.17) traced to the designer's β for A, not the Kaiser fit (2.1); with β for A + 4 dB the rule would move 26 of the rows back (20 within the families, the half-band runs among them; section 6, "The designer's β, X17"), which X-a3 makes with the 4th-band stage admitted, the selection frozen until then (3.1). The rules were not changed; a maintainer who prefers integer-factor chains at a few per cent adds a tie-break to `best_chain` and regenerates. **Ledger (X-a3, v0.18): 46 of the 182 rows changed chain at `economy`** (`matrix.py diff` against v0.17's tables, the selection restored from X-a2's freeze with the 4th-band stage admitted, decision 6 as amended), every new chain running the 4th band. Within the 48 family, twelve ratios (24 rows): 8/1 ↑2 · 4/3 · ↑3 → ↑2 · ↑4; 12/1 ↑2 · ↑6 → ↑3 · ↑4; 16/1 ↑2 · ↑8 → ↑2 · ↑4 · ↑2; 24/1 ↑2 · ↑6 · ↑2 → ↑2 · ↑3 · ↑4; 32/1 ↑2 · ↑8 · ↑2 → ↑2 · ↑4 · ↑4; 48/1 ↑2 · ↑8 · ↑3 → ↑3 · ↑4 · ↑4; and their mirrors, 1/8 ↓3 · 3/4 · ↓2 → ↓4 · ↓2, 1/12 ↓6 · ↓2 → ↓4 · ↓3, 1/16 ↓8 · ↓2 → ↓2 · ↓4 · ↓2, 1/24 ↓2 · ↓6 · ↓2 → ↓4 · ↓3 · ↓2, 1/32 ↓2 · ↓8 · ↓2 → ↓4 · ↓4 · ↓2, 1/48 ↓3 · ↓8 · ↓2 → ↓4 · ↓4 · ↓3. Within the 44.1 family (6 rows) 8/1 ↑3 · 8/3 → ↑2 · ↑4, 16/1 ↑2 · ↑8 → ↑2 · ↑4 · ↑2, 1/8 3/8 · ↓3 → ↓4 · ↓2 and 1/16 ↓8 · ↓2 → ↓2 · ↓4 · ↓2, the 48 family's chains (X6). Cross-family, 16 rows: 8 / 12 / 16 → 176.4 (↑2 · ↑2 → ↑4 after `bridge`), 192 → 11.025 (↓2 · ↓2 → ↓4 ahead of `bridge`), 192 → 22.05 (↓3 · 3/4 → ↓4), 384 → 11.025 (↓2 · ↓2 · ↓2 → ↓4 · ↓2 ahead of `bridge`, ↓2 · ↓2 → ↓4 after it), 384 → 22.05 (↓8 → ↓2 · ↓4), 384 → 44.1 (↓3 · 3/4 · ↓2 → ↓4 · ↓2), 11.025 → 192 (↑2 · ↑2 → ↑4 after `bridge`), 11.025 → 384 (↑2 · ↑2 → ↑4 ahead of `bridge`, ↑2 · ↑2 · ↑2 → ↑2 · ↑4 after it), 22.05 → 192 (4/3 · ↑3 → ↑4), 22.05 → 384 (↑8 → ↑4 · ↑2), 44.1 → 384 (↑2 · 4/3 · ↑3 → ↑2 · ↑4), and 176.4 → 8 / 12 / 16 (↓2 · ↓2 → ↓4 ahead of `bridge`). The mean `economy` MAC/out over the 182 rows falls from 87.76 to 84.59 (−3.6 %; `transparent`, costing the same chains, 216.24 to 211.89). The named chains gained twelve and lost fourteen; ten are retired under D7 and four kept for the per-tier matrix (X-a4) |
| S2 | The exact-zero contract breaks under an accelerated kernel or a future compensated design (`design_prototype_compensated` convolves with a rect, `kaiser.h:200-216`, which fills the zeros) | The property test is in DspTap and runs on every leg; the compensated variant is explicitly outside R4 (an L-th-band design with k·fs zeros is a different filter and would be its own documented decision) |
| S3 | Chain latency at low rates (1–2.5 ms at 8–11 kHz, 3.3) surprises a consumer expecting `bridge`'s 0.4–0.8 ms | Latency is a pinned number per pair (R7) and printed by the notebook; the minimum-phase lever stays deferred as `bridge`'s is, pulled by a latency need |
| S4 | The dipping cross rows (3.1) are used as if they were cheap | Mitigated (decision 3, R14): the matrix carries MACs per output for every row and flags ⚑ the 38 rows whose `bridge` stage runs at ≥ 2× the output rate, "covered, not recommended"; the README and the notebook carry the same flag |
| S5 | Three engines triple the CI matrix (`../PLAN.md` section 3.5 timings) | Per-(target, engine) jobs already exist; `rational`'s on-target suite excludes the measurement suites as `bridge`'s does; M33 budget checked at M3 before the suite grows |
| S6 | `chain<>` in DspTap acquires engine knowledge (a `bridge` or `rational` name) and the dependency rule erodes from below | The `sync_stage` concept is structural (member functions only); DspTap's own test composes `basic_decimator`s; no `tap::sr` identifier may appear in DspTap (its existing comment-only references, `../PLAN.md` section 3.7, are the ceiling) |
| S7 | The 2.2 follow-up changes `bridge`'s profile edges or tap counts at k > 0 after this plan's numbers are pinned | 3.5/3.6's `bridge` numbers are quoted from `design.h:86-89` at k = 0 and 2.2's bit-identity claim (`../PLAN.md:143-144`); M4's k > 0 rows pin whatever 2.2 ships |
| S8 | Charter erosion: a consumer asks for 44.1 → 48 "in one call" | D12; the C ABI names chains; the README shows the two-type composition first |

## 9. Decisions on the v0.1 open questions

Each of v0.1's ten questions was decided by the maintainer "as
recommended" (2026-10-01); the numbering is kept so that references stay
valid. **No open questions remain.**

1. **`bridge` placement — the lowest-k rule stands** (R13, 3.1). 48 → 88.2
   is `bridge` k = 0 → ↑2 at 62 % of the MACs of ↑2 → `bridge` k = 1 with
   the same (f_pass, A); the family plan's illustrative row
   (`../PLAN.md:120`) was corrected. `transparent` needs no exception: its
   20 kHz passband clears both stopband edges at k = 0.
2. **Fewer stages within 10 %** (R3, 3.1). When the MAC-minimal chain beats
   a chain with strictly fewer stages by less than 10 % (*est.*), the
   fewer-stage chain is chosen: less latency, less state, fewer test rows,
   and a few per cent is below the harris estimate's own error until M2
   pins. **Precise form:** 10 % of the rational stages' MACs per output;
   `bridge`'s stage is identical in both candidates (same k, same
   direction) and excluded from the comparison, so a `bridge`-dominated
   total cannot absorb a worse rational factorization. 3/1 is one ↑3
   (12.0, not 3/2 · ↑2 at 11.0); 16 → 44.1 is ↑3 · `bridge`↓ (71.1, not
   70.0); 8 → 11.025 stays ↑2 · ↑3 · `bridge`↓ · ↓2 · ↓2 (297.3); 17 rows
   in all (3.1).
3. **Dipping rows flagged** (R14, S4). A trailing ⚑ column marks every row
   whose `bridge` stage runs (its output rate) at ≥ 2× the chain's output
   rate — 38 rows — as covered, not recommended. Documentation only; D12
   untouched.
4. **`chain<>` in DspTap — confirmed** (R6). M0 proves it on `decimate.h`
   first, so the helper is certified on an existing primitive before this
   engine exists.
5. **384 kHz stays** (R15). The family plan's 14 rates are settled; the cost
   is the 64 / 128 kHz intermediates and 16 generated rows, no new stage
   kind.
6. **↑4 / ↓4 is two half-bands** (R3). No single 4th-band stage is shipped
   until a latency-first consumer asks; the rows it would improve (1/16,
   1/24) are named in 3.3 so that the ask reopens exactly them.
   **Amended 2026-11-21** (X-a3; `docs/X4_X8_PLAN.md` X3, decided by the
   maintainer on spike X-0's numbers and re-sized on X17's): the 4th-band
   stage is in the vocabulary (`up4` / `down4`, `ratio.h`; the C ABI's
   `create_stage` takes 4/1 and 1/4), and the generator chooses it by R3
   like every other band. It reopened not on a latency-first ask but on the
   matrix's own cost: with the designer's β for A + 4 dB (X17) it is the
   cheapest stage at the high-rate end of every chain by 8 and above, and 46
   rows move at `economy` (3.1; section 8, S1). ↑4 / ↓4 themselves stay two
   half-bands, which still cost fewer MACs (2.1).
7. **Profile names reuse `bridge`'s four** (R5), edges as fractions of the
   pair's lower rate; one vocabulary across the sync engines keeps a
   composed chain at one (f_pass, A).
8. **Latency as the exact rational** (R7): `latency_output_frames()`
   returns numerator and denominator, plus `latency_seconds()`; `bridge`
   keeps its `double` and may adopt the same form later without breaking.
9. **The C ABI exposes the 34 named within-family chains** (R16, 4.4):
   enumerators name chains, not rates and not the single-stage ratios
   alone.
10. **Family version 0.5.0 at M6** (R11), bumped in all three umbrella
    headers and the root `project()` together (D13); 0.4.0 until then.

---

*Provenance of every number above: `bridge`'s pinned profile and latency
numbers from `bridge/include/tap/sr/bridge/design.h` and
`bridge/PLAN.md` §4/§8; `decimate.h`'s from its header; everything else is
a harris/Kaiser estimate (`kaiser.h:77-85`) generated by the scratch
`matrix.py` described in section 3 (v0.2: with the fewer-stages rule on
the rational part, the flag, and no single 4th-band stage), marked *est.*, and owed a
measurement by M2 and M4.*
