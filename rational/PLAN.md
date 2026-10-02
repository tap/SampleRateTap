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
  integer factors 2, 3, 6, 8 and their reciprocals (4 is two half-band
  stages, never one stage: decision 6, R3), and the mixed
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
| R3 | **Stage factoring by MACs, then by stage count**: an integer factor K factors into a chain of stages from {2, 3, 6, 8}, with the by-2 stage at the low-rate end and the larger factors at the high-rate end; a mixed ratio with both L, M > 1 is one stage (R2); the chain per rate pair is the MAC-minimal factorization under the harris estimate (section 3, generated) **unless a factorization with strictly fewer stages is within 10 % of it (*est.*) — 10 % of the rational stages' MACs per output: the `bridge` stage is identical in both candidates (same k, same direction) and excluded from the comparison — in which case the chain with the fewest stages within 10 % is chosen** (decision 2). **↑4 / ↓4 is two half-bands** and no single 4th-band stage (`stage<ratio<4, 1>>`, `<1, 4>`) is shipped until a latency-first consumer asks for it (decision 6); 4/3 and 3/4 stay, as mixed stages. Pinned by the matrix test | 2.1 shows the arithmetic: ↓4 as ↓2 · ↓2 costs 37 MACs per output against 55 for one 4th-band stage (−33 %, *est.*), while 48 → 32 as one 2/3 stage costs 26 against 52 for ↑2 · ↓3. Fewer stages mean less latency, less state and fewer test rows, and a few per cent is below the harris estimate's own error until M2 pins: 3/1 is one ↑3 stage (12.0) rather than 3/2 · ↑2 (11.0), and 16 → 44.1 is ↑3 · `bridge`↓ (71.1) rather than 3/2 · ↑2 · `bridge`↓ (70.0) |
| R4 | **Nyquist (L-th-band) Kaiser-windowed-sinc design**: cutoff at π/L of the stage's higher rate, odd length N = 2mL − 1, so h[c ± kL] = 0 exactly for k ≥ 1 (c the centre), the zeros written as exact zeros by the designer (2.2) | The zeros are structural: the half-band ↑2/↓2 stage computes about half the MACs of a generic FIR of the same length, and branch 0 of every ↑L stage is a pure copy (zero MACs). The symmetric transition this forces is exactly the coverage rule's bound (2.1(a)) with equality, so the structure costs no design freedom |
| R5 | **Profiles reuse `bridge`'s vocabulary** — `super_economy`, `economy` (default), `balanced`, `transparent` — as (stopband A in dB, passband edge p as a fraction of the chain's lowest rate r_min): the same four edges `bridge` has at 48 kHz (16, 18, 19, 20 kHz, `design.h:86-89`), stated as fractions from day one, as the 2.2 follow-up makes `bridge`'s (`../PLAN.md:138-139`). Numbers pinned by the M2 design spike (2.3, section 6) | One vocabulary across the sync engines means a chain that composes `bridge` and `rational` at the same profile name has one (f_pass, A) throughout — the matrix test's pin is then the chain's, not a per-engine translation |
| R6 | **The design math and the composition helper land in DspTap first**: `tap/dsp/nyquist.h` (the L-th-band designer, its `m`-search and its property test) and `tap/dsp/chain.h` (`tap::dsp::chain<Stages...>` over a `sync_stage` concept: `process`, `outputs_for`, `frames_needed`, `flush`, `reset`, `latency`). The engine holds only its ratio types, stages, schedule and converter | Substrate discipline (`../CLAUDE.md`, "Shared code lands in DspTap first"). `chain<>` must live *below* both engines: a chain that composes a `bridge` converter with `rational` stages — every cross-family row of section 3 — can then be written by the caller without either engine naming the other (4.2). The family plan already places `chain<>` there (`../PLAN.md:47,1004`); **confirmed** (decision 4): M0 proves the helper on DspTap's own `decimate.h` first |
| R7 | **Alignment and latency contract** (2.5): zero-primed, causal, scipy `upfirdn`'s streaming prefix per stage (as `bridge`, `converter.h:51-56`, and `decimate.h:17-20`); group delay an integer number of samples at the stage's *higher* rate, (N − 1)/2; a chain's latency is the sum of its stages' delays converted to the chain's output rate, **reported as the exact rational** (`latency_output_frames()` returns numerator and denominator) plus `latency_seconds()` (decision 8); `outputs_for` and `frames_needed` compose stage by stage, exactly | Latency as a number, not a claim, per family convention (`../bridge/PLAN.md` §8, `decimate.h:181-182`). `bridge` keeps its `double` (`converter.h:237`) and may adopt the same form later without breaking |
| R8 | **Sample formats** double / float / Q15 / Q31 through `tap::dsp::sample_traits` (`sample_traits.h:107-266`), coefficient rows quantized with `quantize_row_preserving_sum` (`quantize.h:56-99`) after per-branch DC normalization, so every branch's DC gain is exactly 1 in every format; the zero taps are zero in every format and are never multiplied. Aliases `converter<ratio>` (float), `converter_q15`, `converter_q31`, and `double` as the golden model | DspTap's four-profile ladder (`submodules/dsptap/CLAUDE.md`, "Double is the golden model"); `bridge`'s per-branch normalization (`design.h:180-199`) and its reasons |
| R9 | **Real-time rules**: geometry fixed at construction, every buffer allocated and every filter designed there (may throw); `process`, `pull`, `flush`, `reset` are `noexcept`, lock-free and allocation-free; one stream per instance, channels share the coefficient row per frame | The family contract (`converter.h:58-61`, `decimate.h:51-52`) |
| R10 | **Bit-exact repeatability** per format and platform, as `bridge` (`../bridge/PLAN.md` §3): integer paths exactly, float through the fixed accumulation order of the `tap::dsp` kernels (`fir_kernels.h:77-79`); a chain's output is the same for any chunking of its input | Lets the output-hash leg (`test_output_hash.cpp`) and the ratchet's `checksum=` line (`scripts/icount.py:100`) stand |
| R11 | **One family version** (D13): `TAP_SR_VERSION_*` token-identically in `rational.h` — 0.4.0 until M6, **0.5.0 at M6**, bumped in all three umbrella headers and the root `project()` together (decision 10); `tap_sr_rational_version()` bit-packed; `tests/family/version_macros.cpp` gains a third include and `static_assert` | `../PLAN.md:48`; a new engine is a minor bump |
| R12 | **Tests carry the engine prefix and label** `rational.` / `rational` (D16, `../PLAN.md:51`), including the `*_tests_emulated` entries; guest icount marker `RATIONAL_ICOUNT_DONE`, byte-stable from its first baseline | `../CLAUDE.md`, "Guest markers … never change" |
| R13 | **`bridge` at the lowest k** (decision 1): a cross-family chain contains exactly one `bridge` stage, at the lowest k ∈ {0, 1, 2} whose lower rate satisfies 2.1(a) for the chain's f_pass (3.1). `transparent` needs no exception: its 20 kHz passband clears both stopband edges at k = 0 (44.1 − 20 = 24.1 ≥ 24 up, ≥ 22.05 down) | `bridge` costs 58 (down) or 38 (up) MACs per output at `economy` whatever k is, so its cost scales with the rate it runs at, while the ↑2 / ↓2 stages that move a chain between k levels cost 9–19; 48 → 88.2 at k = 0 is 62 % of the MACs of k = 1 at the same (f_pass, A). The family plan's illustrative row (`../PLAN.md:120`) was corrected to match |
| R14 | **Dipping rows are flagged** (decision 3): every row of 3.5/3.6 whose `bridge` stage runs — its output rate — at ≥ 2× the chain's output rate carries ⚑ in a trailing column, "covered, not recommended". Documentation only: the flag dispatches nothing and D12 is untouched | In such a row `bridge` alone costs ≥ 116 MACs per output at `economy` (≥ 368 at `transparent`), 2–4× a within-family chain's total; a consumer who reads the matrix sees the price before choosing. 38 rows carry it |
| R15 | **384 kHz stays** (decision 5): the family plan's 14 rates are settled (`../PLAN.md:100-101`) | Its cost is 16 generated rows, the ↑8 / ↓8 stages already in the vocabulary and the 64 / 128 kHz intermediates; no new stage kind |
| R16 | **The C ABI exposes the named within-family chains** of 3.3/3.4 as its `chain` enumerators (decision 9; 28 of them on the pinned lengths, v0.7 — the union of the two tables' chain columns) — chains, not rates, and not the single-stage ratios alone | D12 at the C boundary: an enumerator names what the caller composes, as `bridge`'s `int direction` does; the binding and the notebook then need no chain arithmetic of their own (4.4) |

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
within-48 ratios factor differently from this arithmetic, because a
relaxed stage saturates: a half-band at a passband below 3/16 of its
lower rate stays at m = 7 (N = 27, 7.5 MACs per output) however wide the
transition — the Kaiser fit's floor for the 70 dB tier — while a
third-band stage there is m = 5 (N = 29, 7 per output) and serves three
times the rate, so the "by-2 at the low-rate end" chains lose to
third-band and mixed stages (8/1 is ↑2 · 4/3 · ↑3, 12/1 is ↑2 · ↑6, 1/8 is
↓3 · 3/4 · ↓2). Section 3 holds the measured result; the ledger is in
section 8, S1.

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
  by 2L.

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
| `super_economy` | 70 | 1/3 | 16 kHz | 35 / 19 (−72.8 dB); *est.* 27 / 15 | 47 / 33 (−71.2 dB); *est.* 41 / 29 |
| `economy` (default) | 70 | 3/8 | 18 kHz | 43 / 23 (−71.9 dB); *est.* 35 / 19 | 65 / 45 (−71.3 dB); *est.* 53 / 37 |
| `balanced` | 70 | 19/48 | 19 kHz | 51 / 27 (−71.5 dB); *est.* 43 / 23 | 77 / 53 (−71.2 dB); *est.* 65 / 45 |
| `transparent` | 120 | 5/12 | 20 kHz | 123 / 63 (−121.7 dB); *est.* 95 / 49 | 149 / 101 (−121.1 dB); *est.* 143 / 97 |

The first number of each cell is **measured and pinned** (v0.3 found
them, v0.5's M2 spike confirmed them and filled the other bands, section
6): the smallest m whose design meets the stopband with ≥ 1 dB margin on
a **16384-point grid** from the stopband edge to F_hi / 2, by
`tap::dsp::search_nyquist_m` on the shipped designer (DspTap `nyquist.h`),
the next-shorter length missing the spec; `design.h`'s `profile` carries
the pins per band and `test_design.cpp` enforces every row. The grid is
stated because it matters: `nyquist.h`'s 1024-point default steps over a
sidelobe of the long designs — the 8th-band `transparent` candidate reads
−121.7 dB on 1024 points and −119.3 dB on 8192 — and under-pins two rows
of the full table (band 4 `balanced` 103 → 111, band 8 `transparent`
383 → 399); 16384 points agree with 65536 within 0.003 dB for every design
up to N = 399, and tap/DspTap#50 made the search's grid a parameter. The
harris estimates (*est.*) are kept beside the measured counts: the Kaiser
fit needs 2–8 more taps per branch than the estimate at the half band, 1–7
at the third; M4 regenerated section 3 from the pinned counts (v0.7). The
counts are for the stage adjacent to r_min (the narrowest transition in a
chain); stages further up a chain are shorter, designed at their design
divisor (3.1) with the pins of `design.h`'s relaxation tables. `bridge`'s experience says a pinned count can also move
non-monotonically with the spec (`design.h:79-82`, the 38-vs-40 quirk in
`../bridge/PLAN.md` §4), which is why every (band, profile) was pinned
from the designer rather than extrapolated. Ripple, measured per stage on
a 2000-point passband grid: ≤ 0.0025 dB at the 70 dB tiers and
≤ 0.00001 dB at `transparent`, inside the candidates for 2.1(c) — ±0.01 dB
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
  `schedule.h:54`), so a pull with a source delivering exactly
  `frames_needed(n)` frames yields exactly n outputs. Latency is
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
4, 6, 8, 9, 12, 16 — found by the same search as the M2 pins and verified
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
three half-bands, +9.7 %); `tools/coverage/matrix.py rule` prints the
list. Within-family chains have no `bridge`, so
the rule there is simply 10 % of the chain. The `transparent` columns
cost the same chains at `transparent`, not a second search.

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
boundaries in a few chains, e.g. 16 → 32 → 64 → 192 and 384 → 64 → 48 →
44.1 → 22.05; they are not supported rates and no chain starts or ends
there). The
family plan's exclusions stand as rates, not pairs: 352.8 kHz would need
k = 3, 37.8 and 50.4 kHz are ratios of 7, and the 1000/1001 pull-downs are
synchronous ratios that no engine's charter names (`../PLAN.md:103-109`).

### 3.2 Worked cases

Lengths are harris estimates (*est.*) at `economy` unless marked `tr`
(`transparent`); the arithmetic is the one-line form of 2.1, kept as the
derivation. The measured chains and numbers are 3.3–3.6 (v0.7); four of
the six cases below are the measured chain too, and two chose differently
on the pinned lengths — 384 → 44.1 is ↓3 · 3/4 · ↓2 · `bridge`↓ k = 0
(172.3 MACs per output) and 8 → 192 is ↑2 · ↑6 · ↑2 (8.7) — for the reason
2.1 ends with.

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

Chains at `economy` (the `transparent` columns use the same chains);
MAC/out = MACs per output frame at the chain's output rate; latency in
output frames. **Measured** (v0.7): every number is the shipping chain's,
generated by `tools/coverage/matrix.py` from the pinned lengths and pinned
exactly — as a rational — per row by `tests/test_matrix.cpp`.

| Ratio L/M | Pairs (from → to, kHz) | Chain | MAC/out eco | Latency eco (out) | MAC/out tr | Latency tr (out) |
|---|---|---|---|---|---|---|
| 4/3 | 12→16, 24→32 | 4/3 | 16.8 | 14.3 | 37.8 | 33.0 |
| 3/2 | 8→12, 16→24, 32→48 | 3/2 | 15.0 | 16.0 | 33.7 | 37.0 |
| 2/1 | 8→16, 12→24, 16→32, 24→48, 48→96, 96→192, 192→384 | ↑2 | 11.5 | 21.0 | 31.5 | 61.0 |
| 8/3 | 12→32 | ↑2 · 4/3 | 16.4 | 34.3 | 43.4 | 98.3 |
| 3/1 | 8→24, 16→48, 32→96 | ↑3 | 15.0 | 32.0 | 33.7 | 74.0 |
| 4/1 | 8→32, 12→48, 24→96, 48→192, 96→384 | ↑2 · ↑2 | 13.2 | 55.0 | 26.2 | 141.0 |
| 6/1 | 8→48, 16→96, 32→192 | ↑2 · ↑3 | 10.8 | 77.0 | 21.5 | 206.0 |
| 8/1 | 12→96, 24→192, 48→384 | ↑2 · 4/3 · ↑3 | 12.5 | 117.0 | 25.5 | 318.0 |
| 12/1 | 8→96, 16→192, 32→384 | ↑2 · ↑6 | 10.4 | 155.0 | 27.1 | 443.0 |
| 16/1 | 12→192, 24→384 | ↑2 · ↑8 | 10.3 | 207.0 | 26.8 | 591.0 |
| 24/1 | 8→192, 16→384 | ↑2 · ↑6 · ↑2 | 8.7 | 315.0 | 22.0 | 901.0 |
| 32/1 | 12→384 | ↑2 · ↑8 · ↑2 | 8.7 | 419.0 | 20.9 | 1195.0 |
| 48/1 | 8→384 | ↑2 · ↑8 · ↑3 | 7.8 | 629.0 | 18.6 | 1793.0 |
| 3/4 | 16→12, 32→24 | 3/4 | 29.0 | 10.8 | 66.3 | 24.8 |
| 2/3 | 12→8, 24→16, 48→32 | 2/3 | 32.5 | 10.7 | 74.5 | 24.7 |
| 1/2 | 16→8, 24→12, 32→16, 48→24, 96→48, 192→96, 384→192 | ↓2 | 23.0 | 10.5 | 63.0 | 30.5 |
| 3/8 | 32→12 | 3/4 · ↓2 | 49.0 | 12.9 | 131.7 | 36.9 |
| 1/3 | 24→8, 48→16, 96→32 | ↓3 | 45.0 | 10.7 | 101.0 | 24.7 |
| 1/4 | 32→8, 48→12, 96→24, 192→48, 384→96 | ↓2 · ↓2 | 53.0 | 13.8 | 105.0 | 35.2 |
| 1/6 | 48→8, 96→16, 192→32 | ↓3 · ↓2 | 65.0 | 12.8 | 129.0 | 34.3 |
| 1/8 | 96→12, 192→24, 384→48 | ↓3 · 3/4 · ↓2 | 105.0 | 14.6 | 219.7 | 39.8 |
| 1/12 | 96→8, 192→16, 384→32 | ↓6 · ↓2 | 125.0 | 12.9 | 325.0 | 36.9 |
| 1/16 | 192→12, 384→24 | ↓8 · ↓2 | 165.0 | 12.9 | 429.0 | 36.9 |
| 1/24 | 192→8, 384→16 | ↓2 · ↓6 · ↓2 | 209.0 | 13.1 | 529.0 | 37.5 |
| 1/32 | 384→12 | ↓2 · ↓8 · ↓2 | 277.0 | 13.1 | 669.0 | 37.3 |
| 1/48 | 384→8 | ↓3 · ↓8 · ↓2 | 373.0 | 13.1 | 893.0 | 37.4 |

What the pinned lengths changed (the v0.2 arithmetic is in 2.1 and its
ledger in section 8, S1): a relaxed half-band saturates at m = 7 (7.5 MACs
per output, 15 per output as a decimator), so the chains that used to end
in a run of half-bands now put a third-band, 4th-band or mixed stage where
the transition is wide — 8/1 is ↑2 · 4/3 · ↑3 (12.5 against 14.1 for three
half-bands), 1/8 is ↓3 · 3/4 · ↓2 (105 against 113), 12/1 is ↑2 · ↑6 and
1/12 is ↓6 · ↓2 (the fewer-stages rule, +5 % over ↑2 · ↑3 · ↑2 / ↓2 · ↓3 ·
↓2), and the ↑8 / ↓8 stage moves from the high-rate end to the middle
(24/1 is ↑2 · ↑6 · ↑2, 32/1 is ↑2 · ↑8 · ↑2, 1/32 is ↓2 · ↓8 · ↓2, 1/48 is
↓3 · ↓8 · ↓2): at a divisor of 16 the 8th-band stage is m = 4 (N = 63, 57
nonzero) and a half-band above it m = 3 (N = 11, 3.5 per output), cheaper
than ↓8 at the top. Decision 6 still keeps the single 4th-band stage out
(1/16 is ↓8 · ↓2 at 165; 16/1 is ↑2 · ↑8 at 10.3). The `transparent`
columns cost the same chains (3.1).

### 3.4 Within the 44.1 kHz family (20 ordered pairs, 8 ratios)

Six of the eight chains are the 48 family's; 8/1 and 1/8 differ because
the intermediate lattices differ relative to r_min — 11.025 · 3 = 33.075
kHz is a 2^a · 3^b multiple of the family's base where 12 · 8/3 = 32 kHz
is one of the 48 family's (neither is a supported rate; both are stage
boundaries) — so the 44.1 family's 8/1 is ↑3 · 8/3 and its 1/8 is 3/8 · ↓3,
two more named chains.

| Ratio L/M | Pairs (from → to, kHz) | Chain | MAC/out eco | Latency eco (out) | MAC/out tr | Latency tr (out) |
|---|---|---|---|---|---|---|
| 2/1 | 11.025→22.05, 22.05→44.1, 44.1→88.2, 88.2→176.4 | ↑2 | 11.5 | 21.0 | 31.5 | 61.0 |
| 4/1 | 11.025→44.1, 22.05→88.2, 44.1→176.4 | ↑2 · ↑2 | 13.2 | 55.0 | 26.2 | 141.0 |
| 8/1 | 11.025→88.2, 22.05→176.4 | ↑3 · 8/3 | 14.5 | 98.3 | 33.8 | 229.0 |
| 16/1 | 11.025→176.4 | ↑2 · ↑8 | 10.3 | 207.0 | 26.8 | 591.0 |
| 1/2 | 22.05→11.025, 44.1→22.05, 88.2→44.1, 176.4→88.2 | ↓2 | 23.0 | 10.5 | 63.0 | 30.5 |
| 1/4 | 44.1→11.025, 88.2→22.05, 176.4→44.1 | ↓2 · ↓2 | 53.0 | 13.8 | 105.0 | 35.2 |
| 1/8 | 88.2→11.025, 176.4→22.05 | 3/8 · ↓3 | 124.0 | 12.3 | 292.0 | 28.6 |
| 1/16 | 176.4→11.025 | ↓8 · ↓2 | 165.0 | 12.9 | 429.0 | 36.9 |

### 3.5 48 kHz family → 44.1 kHz family (45 ordered pairs)

`bridge`↓ at k is the 147/160 converter running from 48 · 2^k to
44.1 · 2^k; its MACs and latency are `bridge`'s pinned `economy` /
`transparent` numbers (58 / 184 taps per phase, `design.h:86-89`). Every
row is measured, the k > 0 rows included (2.2 landed before M4). ⚑ (R14):
the `bridge` stage's output rate is ≥ 2× the chain's output rate —
covered, not recommended as a default. The rational stages of a cross row
are designed at their design divisors (3.1): the lattice floor of the
quotient, or 147/160 for the stage at `bridge`'s rate below a 48-family
r_min (48 → 88.2's ↑2, 96 → 44.1's ↓2 and their k > 0 twins).

| From → To | Chain | f_pass eco (kHz) | MAC/out eco | Latency eco (ms) | MAC/out tr | Latency tr (ms) | ⚑ |
|---|---|---|---|---|---|---|---|
| 8 → 11.025 | ↑2 · ↑3 · bridge↓k0 · ↓2 · ↓2 | 3.000 | 332.2 | 3.46 | 934.6 | 9.41 | ⚑ |
| 8 → 22.05 | ↑2 · ↑3 · bridge↓k0 · ↓2 | 3.000 | 154.6 | 2.50 | 435.8 | 6.64 | ⚑ |
| 8 → 44.1 | ↑2 · ↑3 · bridge↓k0 | 3.000 | 69.8 | 2.21 | 207.4 | 6.21 |  |
| 8 → 88.2 | ↑2 · ↑3 · bridge↓k0 · ↑2 | 3.000 | 42.4 | 2.36 | 113.2 | 6.40 |  |
| 8 → 176.4 | ↑2 · ↑3 · bridge↓k0 · ↑2 · ↑2 | 3.000 | 25.7 | 2.40 | 65.1 | 6.49 |  |
| 12 → 11.025 | ↑2 · ↑2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 342.7 | 3.00 | 955.3 | 8.05 | ⚑ |
| 12 → 22.05 | ↑2 · ↑2 · bridge↓k0 · ↓2 | 4.500 | 167.8 | 2.23 | 488.1 | 6.24 | ⚑ |
| 12 → 44.1 | ↑2 · ↑2 · bridge↓k0 | 4.500 | 72.4 | 1.75 | 212.6 | 4.85 |  |
| 12 → 88.2 | ↑2 · ↑2 · bridge↓k0 · ↑2 | 4.500 | 43.7 | 1.90 | 115.8 | 5.05 |  |
| 12 → 176.4 | ↑2 · ↑2 · bridge↓k0 · ↑2 · ↑2 | 4.500 | 26.4 | 1.94 | 67.4 | 5.14 |  |
| 16 → 11.025 | ↑3 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 350.3 | 2.52 | 987.6 | 6.66 | ⚑ |
| 16 → 22.05 | ↑3 · bridge↓k0 · ↓2 | 6.000 | 171.7 | 1.75 | 504.3 | 4.84 | ⚑ |
| 16 → 44.1 | ↑3 · bridge↓k0 | 6.000 | 74.3 | 1.27 | 220.6 | 3.46 |  |
| 16 → 88.2 | ↑3 · bridge↓k0 · ↑2 | 6.000 | 44.7 | 1.42 | 120.8 | 3.67 |  |
| 16 → 176.4 | ↑3 · bridge↓k0 · ↑2 · ↑2 | 6.000 | 29.8 | 1.49 | 69.9 | 3.77 |  |
| 24 → 11.025 | ↑2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 317.7 | 2.12 | 886.7 | 5.51 | ⚑ |
| 24 → 22.05 | ↑2 · bridge↓k0 · ↓2 | 8.269 | 164.0 | 1.52 | 499.6 | 4.57 | ⚑ |
| 24 → 44.1 | ↑2 · bridge↓k0 | 9.000 | 70.5 | 1.04 | 218.3 | 3.19 |  |
| 24 → 88.2 | ↑2 · bridge↓k0 · ↑2 | 9.000 | 46.8 | 1.28 | 140.6 | 3.88 |  |
| 24 → 176.4 | ↑2 · bridge↓k0 · ↑2 · ↑2 | 9.000 | 30.9 | 1.35 | 79.8 | 3.98 |  |
| 32 → 11.025 | 3/2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 315.5 | 2.00 | 888.9 | 5.35 | ⚑ |
| 32 → 22.05 | 3/2 · bridge↓k0 · ↓2 | 8.269 | 171.7 | 1.41 | 504.3 | 4.07 | ⚑ |
| 32 → 44.1 | 3/2 · bridge↓k0 | 12.000 | 74.3 | 0.94 | 220.6 | 2.69 |  |
| 32 → 88.2 | 3/2 · bridge↓k0 · ↑2 | 12.000 | 48.7 | 1.18 | 141.8 | 3.38 |  |
| 32 → 176.4 | 3/2 · bridge↓k0 · ↑2 · ↑2 | 12.000 | 31.8 | 1.25 | 81.4 | 3.49 |  |
| 48 → 11.025 | bridge↓k0 · ↓2 · ↓2 | 4.134 | 285.0 | 1.85 | 841.0 | 5.11 | ⚑ |
| 48 → 22.05 | bridge↓k0 · ↓2 | 8.269 | 139.0 | 1.08 | 431.0 | 3.30 | ⚑ |
| 48 → 44.1 | bridge↓k0 | 16.538 | 58.0 | 0.60 | 184.0 | 1.92 |  |
| 48 → 88.2 | bridge↓k0 · ↑2 | 18.000 | 44.5 | 0.93 | 137.5 | 2.93 |  |
| 48 → 176.4 | bridge↓k0 · ↑2 · ↑2 | 18.000 | 33.8 | 1.05 | 100.2 | 3.27 |  |
| 96 → 11.025 | ↓2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 350.3 | 1.99 | 923.7 | 5.29 | ⚑ |
| 96 → 22.05 | ↓2 · bridge↓k0 · ↓2 | 8.269 | 171.7 | 1.22 | 476.7 | 3.50 | ⚑ |
| 96 → 44.1 | ↓2 · bridge↓k0 | 16.538 | 83.0 | 0.82 | 252.6 | 2.55 |  |
| 96 → 88.2 | bridge↓k1 | 33.075 | 58.0 | 0.30 | 184.0 | 0.96 |  |
| 96 → 176.4 | bridge↓k1 · ↑2 | 36.000 | 44.5 | 0.47 | 137.5 | 1.46 |  |
| 192 → 11.025 | ↓2 · ↓2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 428.7 | 2.02 | 1089.2 | 5.38 | ⚑ |
| 192 → 22.05 | ↓3 · 3/4 · bridge↓k0 · ↓2 | 8.269 | 228.3 | 1.25 | 601.5 | 3.69 | ⚑ |
| 192 → 44.1 | ↓2 · ↓2 · bridge↓k0 | 16.538 | 115.7 | 0.89 | 298.3 | 2.65 |  |
| 192 → 88.2 | ↓2 · bridge↓k1 | 33.075 | 83.0 | 0.41 | 252.6 | 1.28 |  |
| 192 → 176.4 | bridge↓k2 | 66.150 | 58.0 | 0.15 | 184.0 | 0.48 |  |
| 384 → 11.025 | ↓2 · ↓2 · ↓2 · bridge↓k0 · ↓2 · ↓2 | 4.134 | 550.6 | 2.04 | 1350.4 | 5.41 | ⚑ |
| 384 → 22.05 | ↓8 · bridge↓k0 · ↓2 | 8.269 | 293.6 | 1.18 | 829.4 | 3.57 | ⚑ |
| 384 → 44.1 | ↓3 · 3/4 · ↓2 · bridge↓k0 | 16.538 | 172.3 | 0.91 | 423.1 | 2.74 |  |
| 384 → 88.2 | ↓2 · ↓2 · bridge↓k1 | 33.075 | 115.7 | 0.45 | 298.3 | 1.33 |  |
| 384 → 176.4 | ↓2 · bridge↓k2 | 66.150 | 83.0 | 0.21 | 252.6 | 0.64 |  |

### 3.6 44.1 kHz family → 48 kHz family (45 ordered pairs)

| From → To | Chain | f_pass eco (kHz) | MAC/out eco | Latency eco (ms) | MAC/out tr | Latency tr (ms) | ⚑ |
|---|---|---|---|---|---|---|---|
| 11.025 → 8 | ↑2 · ↑2 · bridge↑k0 · ↓3 · ↓2 | 3.000 | 366.0 | 3.28 | 849.7 | 8.58 | ⚑ |
| 11.025 → 12 | ↑2 · ↑2 · bridge↑k0 · ↓2 · ↓2 | 4.134 | 253.7 | 2.82 | 585.5 | 7.22 | ⚑ |
| 11.025 → 16 | ↑2 · ↑2 · bridge↑k0 · ↓3 | 4.134 | 195.5 | 2.34 | 461.4 | 5.83 | ⚑ |
| 11.025 → 24 | ↑2 · ↑2 · bridge↑k0 · ↓2 | 4.134 | 115.3 | 1.95 | 261.2 | 4.68 | ⚑ |
| 11.025 → 32 | ↑2 · ↑2 · bridge↑k0 · 2/3 | 4.134 | 89.8 | 1.82 | 203.7 | 4.53 |  |
| 11.025 → 48 | ↑2 · ↑2 · bridge↑k0 | 4.134 | 50.2 | 1.68 | 120.1 | 4.29 |  |
| 11.025 → 96 | ↑2 · ↑2 · bridge↑k0 · ↑2 | 4.134 | 32.6 | 1.81 | 69.6 | 4.46 |  |
| 11.025 → 192 | ↑2 · ↑2 · bridge↑k0 · ↑2 · ↑2 | 4.134 | 20.8 | 1.85 | 44.3 | 4.55 |  |
| 11.025 → 384 | ↑2 · ↑2 · bridge↑k0 · ↑2 · ↑2 · ↑2 | 4.134 | 13.9 | 1.86 | 29.6 | 4.59 |  |
| 22.05 → 8 | ↑2 · bridge↑k0 · ↓3 · ↓2 | 3.000 | 334.3 | 2.33 | 762.9 | 5.81 | ⚑ |
| 22.05 → 12 | ↑2 · bridge↑k0 · ↓2 · ↓2 | 4.500 | 247.3 | 2.05 | 604.8 | 5.41 | ⚑ |
| 22.05 → 16 | ↑2 · bridge↑k0 · ↓3 | 6.000 | 190.7 | 1.57 | 475.8 | 4.01 | ⚑ |
| 22.05 → 24 | ↑2 · bridge↑k0 · ↓2 | 8.269 | 120.1 | 1.34 | 312.9 | 3.74 | ⚑ |
| 22.05 → 32 | ↑2 · bridge↑k0 · 2/3 | 8.269 | 105.3 | 1.24 | 261.9 | 3.24 |  |
| 22.05 → 48 | ↑2 · bridge↑k0 | 8.269 | 48.6 | 0.91 | 124.9 | 2.47 |  |
| 22.05 → 96 | ↑2 · bridge↑k0 · ↑2 | 8.269 | 31.8 | 1.04 | 73.0 | 2.67 |  |
| 22.05 → 192 | ↑2 · bridge↑k0 · 4/3 · ↑3 | 8.269 | 21.7 | 1.08 | 48.8 | 2.86 |  |
| 22.05 → 384 | ↑2 · bridge↑k0 · ↑8 | 8.269 | 14.9 | 1.01 | 38.5 | 2.74 |  |
| 44.1 → 8 | bridge↑k0 · ↓3 · ↓2 | 3.000 | 293.0 | 2.03 | 705.0 | 5.38 | ⚑ |
| 44.1 → 12 | bridge↑k0 · ↓2 · ↓2 | 4.500 | 205.0 | 1.58 | 489.0 | 4.03 | ⚑ |
| 44.1 → 16 | bridge↑k0 · ↓3 | 6.000 | 159.0 | 1.10 | 389.0 | 2.63 | ⚑ |
| 44.1 → 24 | bridge↑k0 · ↓2 | 9.000 | 99.0 | 0.87 | 255.0 | 2.36 | ⚑ |
| 44.1 → 32 | bridge↑k0 · 2/3 | 12.000 | 89.5 | 0.76 | 218.5 | 1.86 |  |
| 44.1 → 48 | bridge↑k0 | 16.538 | 38.0 | 0.43 | 96.0 | 1.09 |  |
| 44.1 → 96 | bridge↑k0 · ↑2 | 16.538 | 30.5 | 0.65 | 79.5 | 1.72 |  |
| 44.1 → 192 | bridge↑k0 · ↑2 · ↑2 | 16.538 | 22.8 | 0.72 | 50.2 | 1.82 |  |
| 44.1 → 384 | bridge↑k0 · ↑2 · 4/3 · ↑3 | 16.538 | 17.2 | 0.74 | 37.5 | 1.92 |  |
| 88.2 → 8 | ↓2 · bridge↑k0 · ↓3 · ↓2 | 3.000 | 375.7 | 2.18 | 809.7 | 5.57 | ⚑ |
| 88.2 → 12 | ↓2 · bridge↑k0 · ↓2 · ↓2 | 4.500 | 260.1 | 1.72 | 558.8 | 4.22 | ⚑ |
| 88.2 → 16 | ↓2 · bridge↑k0 · ↓3 | 6.000 | 200.3 | 1.24 | 446.9 | 2.85 | ⚑ |
| 88.2 → 24 | ↓2 · bridge↑k0 · ↓2 | 9.000 | 141.3 | 1.11 | 370.8 | 3.05 | ⚑ |
| 88.2 → 32 | ↓2 · bridge↑k0 · 2/3 | 12.000 | 121.2 | 1.00 | 305.3 | 2.55 |  |
| 88.2 → 48 | ↓2 · bridge↑k0 | 18.000 | 66.5 | 0.76 | 179.6 | 2.10 |  |
| 88.2 → 96 | bridge↑k1 | 33.075 | 38.0 | 0.22 | 96.0 | 0.54 |  |
| 88.2 → 192 | bridge↑k1 · ↑2 | 33.075 | 30.5 | 0.32 | 79.5 | 0.86 |  |
| 88.2 → 384 | bridge↑k1 · ↑2 · ↑2 | 33.075 | 22.8 | 0.36 | 50.2 | 0.91 |  |
| 176.4 → 8 | ↓2 · ↓2 · bridge↑k0 · ↓3 · ↓2 | 3.000 | 474.9 | 2.22 | 997.2 | 5.66 | ⚑ |
| 176.4 → 12 | ↓2 · ↓2 · bridge↑k0 · ↓2 · ↓2 | 4.500 | 326.3 | 1.76 | 698.5 | 4.31 | ⚑ |
| 176.4 → 16 | ↓2 · ↓2 · bridge↑k0 · ↓3 | 6.000 | 283.0 | 1.32 | 551.6 | 2.94 | ⚑ |
| 176.4 → 24 | ↓2 · ↓2 · bridge↑k0 · ↓2 | 9.000 | 196.4 | 1.18 | 440.6 | 3.15 | ⚑ |
| 176.4 → 32 | ↓2 · ↓2 · bridge↑k0 · 2/3 | 12.000 | 162.5 | 1.08 | 363.2 | 2.66 |  |
| 176.4 → 48 | ↓2 · ↓2 · bridge↑k0 | 18.000 | 108.7 | 0.88 | 295.4 | 2.44 |  |
| 176.4 → 96 | ↓2 · bridge↑k1 | 36.000 | 66.5 | 0.38 | 179.6 | 1.05 |  |
| 176.4 → 192 | bridge↑k2 | 66.150 | 38.0 | 0.11 | 96.0 | 0.27 |  |
| 176.4 → 384 | bridge↑k2 · ↑2 | 66.150 | 30.5 | 0.16 | 79.5 | 0.43 |  |

Count: 72 + 20 + 45 + 45 = 182 = 14 × 13 ordered pairs, every one covered
and measured; 38 carry ⚑. The generator, `rational/tools/coverage/matrix.py`
(committed at M4), is deterministic: the lattice of 2^a · 3^b multiples of
each family's base as intermediates (b ≤ 3; without a single 4th-band
stage, decision 6), the stage lengths from the numpy search on the
16384-point grid at each stage's design divisor (the same criterion as the
M2 pins; its `pins` mode prints `design.h`'s relaxation tables), the MACs
per output as `stage.h` trims its rows, the same selection (the MAC
minimum, then the fewest stages whose rational part is within 10 % of its
rational part around the same `bridge` stage, then latency), the same
flag rule. `matrix.py md` prints 3.3–3.6, `rows` writes
`tests/coverage/matrix_rows.h` (the 182 rows as an X-macro with every
stage's divisor and the exact MACs and latency per profile, which
`test_matrix.cpp` pins), `diff` lists the rows that differ from this
file's tables, `rule` the rows where the fewest-stages rule bit.

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
│   ├── stage.h             basic_stage<S, R>: table (per phase or per decimating branch, trimmed to the nonzero span;
│   │                       symmetry halving measured out after M6: section 6, the levers), the
│   │                       schedule, process/pull/flush/reset, the exact-rational latency
│   ├── chain.h             basic_chain<S, R...>: tap::dsp::chain over basic_stage, each stage at its design
│   │                       divisor (3.1); the 20 named multi-stage chains of 3.3 / 3.4 (up_2_up_2 … down_3_down_8_down_2)
│   └── converter.h         basic_converter<S, R> = basic_stage<S, R> under the family's converter name, with the
│                           float / Q15 / Q31 aliases (converter<R>, converter_q15<R>, converter_q31<R>)
├── tests/                  test_ratio, test_design, test_stage, test_chain, test_matrix, test_fixed_point,
│   │                       test_cross_validation, test_output_hash, test_capi, bare_metal_main.cpp
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
(`TAP_SR_RATIONAL_UP_2`, `TAP_SR_RATIONAL_UP_2_UP_2`,
`TAP_SR_RATIONAL_RATIO_2_3`, `TAP_SR_RATIONAL_DOWN_8_DOWN_2`, …; the set is
the union of the two tables' chain columns: 8 one-stage, 20 multi-stage,
the 20 `chain.h` names as types; v0.2 counted 34 before the union was
taken) — a
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
   of the 182 pairs: build the chain of 3.3–3.6 in `double` from the
   generated row table (`tests/coverage/matrix_rows.h`); pin MACs per
   output (the stages' trimmed-row counts scaled to the output rate — what
   the kernels execute, not an estimate) and latency exactly, as
   rationals, at all four profiles; check a within-family row is the named
   chain (its compile-time divisors are the table's); measure the promise
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
   `transparent`; worst passband deviation 0.0087 / 0.0081 / 0.0070 /
   0.00002 dB (about 1700 tones per profile, 24 s on a host; a host suite,
   excluded on the QEMU legs).

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
  ratcheted scenario" debt (`../async/PLAN.md` §4.1) is not inherited.

## 6. Milestones

| M | Deliverable | Acceptance | Waits for 2.2? |
|---|---|---|---|
| M0 | **DspTap substrate PR**: `tap/dsp/nyquist.h` (R4's designer and m-search), `tap/dsp/chain.h` (`sync_stage` concept, `chain<>`, composed `outputs_for` / `frames_needed` / latency; `flush` deferred to M4, when a stage with a defined flush exists), tests, README sections, per DspTap's "Adding a primitive" checklist. **Done** (tap/DspTap#48, 2026-10-01; this tree's `submodules/dsptap` pin at its merge) | DspTap CI green on every host and QEMU leg ✓; the L-th-band property test ✓ (exact centre and zeros, per-branch unity, the shifted responses summing to 1); `chain<>` of two `basic_decimator`s matches the two run in sequence bit for bit in every sample format ✓; DspTap's existing icount ratchet unmoved ✓ (every key within +0.9 % of baseline) | no |
| M1 | **Skeleton + ratio types + design**: `rational/` tree (section 4), `tap::sr::rational` target, the family tests extended to three engines, `ratio<L, M>` with its `static_assert`s, `profile`, `design_stage<R>`. **Done** (2026-10-01; three of the six public headers — `rational.h`, `ratio.h`, `design.h` — the header count pinned at 3 until M3/M4 add the rest) | Configure and build from the root and from `cmake -S rational` ✓; the four 4.2 checks and `VersionMacrosAgree` pass for all three engines ✓ (`check_includes.cmake` now takes both siblings); `ratio<5, 1>`, `ratio<4, 2>`, `ratio<2, 2>` fail to compile with the charter's message ✓ (`tests/compile_fail/`, a `try_compile` project run as `rational.Ratio.ChartersFailToCompileWithTheMessage`, on the cross legs with their toolchain file); `BadProfilesThrow` ✓; the M33 leg runs the battery in 134 s with the transparent search excluded | no |
| M2 | **Design spike**: `notebooks/design_spike.ipynb`, executed, pins N per (single-stage ratio ∈ {2, 3, 6, 8, 3/2, 2/3, 4/3, 3/4, 8/3, 3/8} × profile; the 4th-band row is measured for the record only, decision 6, and serves 4/3 and 3/4) by the ≥ 1 dB margin criterion on a fine grid, with measured worst stopband and ripple; `test_design.cpp` enforces the pins. **Done** (2026-10-02, v0.5: the notebook is the independent numpy leg from the same published math, `profile::taps_per_branch` carries the pins per band, `design_stage<R>` reads them, a custom profile without pins is searched on the same grid) | The table below filled ✓; every pinned N of the form 2mL − 1 ✓ (mixed: T = ⌈N / L⌉ per phase); measured A ≥ spec + 1 dB on 16384 points ✓ (−71.1 … −72.8 dB at 70 dB, −121.1 … −121.7 dB at 120 dB) with m − 1 missing it ✓; ripple ≤ 2.3's candidate ✓ (≤ 0.0025 / 0.00001 dB); the scipy `upfirdn` end-to-end check of the economy half-band decimator: a tone just past the stopband edge aliases at −72.1 dB, passband tones within 0.001 dB ✓ | no |
| M3 | **Single stages, float golden leg**: `basic_stage<S, R>` for ↑2 / ↓2 (half-band) and ↑3 / ↓3 (third-band) first, then the rest of the vocabulary; both call shapes; `flush`; scipy vectors committed. **Done** (2026-10-02, v0.6: the whole vocabulary at once, since the two machines are generic — the L-phase schedule machine with trimmed phase rows for interpolators and mixed ratios, the M-branch commutator for decimators, its nonzero branches summed under one finalize through DspTap's `accumulate_row` (tap/DspTap#51, the substrate addition M3 needed); MACs per output are the design's nonzero counts for every interpolator and decimator, the half-band decimator 23 of its 43 taps) | Scipy vectors sample-for-sample ✓ (16 vectors, every ratio at `economy` and the by-2 pair at `transparent`; float within 6e-8 of the float64 reference, the 3e-5 tolerance is bridge's); exhaustive phase sweeps ✓ (the impulse reproduces the table bit for bit from every input phase, every format; accounting exact from every position of every superblock); `PullMatchesProcessBitExact` ✓; chunking invariance ✓ (every format); `decimate.h` second-golden agreement at ↓2 / ↓3 / ↓6 within the documented design difference ✓; latency equals (N − 1)/2 at the higher rate by impulse ✓ (every ratio, the exact rational (N − 1)/(2M) at the output rate); DC gain exactly 1 in every format ✓; flush equals zero padding bit for bit ✓ | no |
| M4 | **Chains and the coverage matrix**: `chain.h`'s named chains, `tools/coverage/matrix.py` committed, `test_matrix.cpp` pinning (f_pass, A, δ, MACs/out, latency) for all 182 rows. **Done** (2026-10-02, v0.7: `basic_chain<S, R...>` constructs each stage at its design divisor (3.1) over DspTap's `chain<>`, whose `flush` landed for it (tap/DspTap#52); the relaxation tables in `design.h`; the generator re-run on the pinned lengths, section 3 regenerated — 35 rows changed chain, the ledger in S1 — and the 20 named multi-stage chains follow it) | Every row measured and pinned, the k > 0 rows included (2.2 had landed, so nothing was skipped) ✓; chain vectors against sequenced upfirdn ✓ (seven chains, float within 3e-5 and double within 1e-7 of the float64 reference); a chain equals its stages run in sequence bit for bit in every format ✓; `frames_needed` / `outputs_for` exact from every position of every named chain ✓; flush equals zero padding bit for bit ✓; the tone battery's numbers in section 5 | no |
| M5 | **Fixed-point profiles**: Q15 and Q31 datapaths through the traits; per-branch quantization; bit-pinned tables; cross-precision numbers. **Done** (2026-10-02, v0.8: the datapaths were M3's — the stage is format-generic over `tap::dsp::sample_traits` — so M5 is the battery that states each format's contract as numbers, `tests/test_fixed_point.cpp`, every ratio at every profile; the measured table and the two limits below) | Q31 within the format floor of float on the reference noise ✓ (worst \|Q31 − double\| 3.4e−9 against float's 5.1e−8, bridge's 5e−8); Q15 format-limited numbers stated per stage ✓ (the table below: RMS −85.6 … −95.8 dBFS, attained stopband per stage); exact-unity row sums ✓ (every phase row, every decimator's whole filter, both formats); wrap safety ✓ (±full-scale noise lands within 8.8e−4 / 1.6e−8 of the clamped double model; full-scale DC of either sign is exactly full scale from every phase); the Q15 half-band's zero taps absent from the dot ✓ (23 MACs of 43 taps at `economy`; no structural zero inside any interpolator's, decimator's or mixed-up stage's dot, Q15 MACs pinned per stage); bit-pinned tables ✓ (FNV-1a-64 per ratio, profile and format, 112 pins) | no |
| M6 | **C ABI, notebook, icount baselines**: `libtap_sr_rational_capi`, `tap_sr_rational_py.py`, `matrix.ipynb` executed through the C ABIs (bridge's for cross rows), `bench/icount/` with baselines on M33 / M55 / Hexagon, README icount table, `CApi.VersionIsBitPacked`. **Done** (2026-10-02, v0.9: `tap_sr_rational_create(chain, profile, channels)` takes one of the 28 named within-family chains of sections 3.3 / 3.4 as a stable `TAP_SR_RATIONAL_*` constant, never a rate (D12); `tap_sr_rational_create_stage(L, M, profile, divisor_num, divisor_den, channels)` is one stage of the vocabulary at the design divisor a matrix row states, the piece a chain through `bridge` composes (R16) — the caller still writes that chain; twelve icount workloads, the measured table below) | Notebook measures the shipping C++ and reproduces the pinned matrix numbers ✓ (all 182 rows × four profiles, 728 of 728 pairs reproduce both the MACs-per-output and the latency pins exactly, the cross rows through `bridge`'s C ABI; the tone battery passes 182 / 182 rows at `economy`, worst stopband −71.34 dB and ripple 0.0081 dB, and 182 / 182 at `transparent`, −121.57 dB and 0.00002 dB); every chain constant equals its named `basic_chain` bit for bit with the same latency, MACs and accounting ✓ (`CApi.EveryChainEnumeratorIsItsNamedChain`); ratchet green two-sided on three targets ✓ (re-run `--exact`: every count equal); `nm -D` symbol set recorded ✓ (exactly the sixteen `tap_sr_rational_*` functions — `create`, `create_stage`, `destroy`, `ratio`, `outputs_for`, `frames_needed`, `process`, `flush`, `flush_output_frames`, `reset`, `latency_output_frames`, `latency_seconds`, `macs_per_output`, `stages`, `stage_taps`, `version` — the library built with hidden visibility so no C++ leaks into the table); **family version 0.5.0** ✓ (all three umbrella headers and the root `project()`; `CApi.VersionIsBitPacked` 0x000500 in every engine) | no |

**The M2 table, measured** (N = 2mB − 1 per band × profile, the worst
stopband on the 16384-point grid in parentheses; the harris estimates the
v0.2 plan carried are struck through beside the first two rows and were
2–8 taps per branch low; `design.h`'s `profile` docstring repeats the
table as m):

| Ratio (band B) | super_economy | economy | balanced | transparent |
|---|---|---|---|---|
| ↑2 / ↓2 (2) | **35** / 19 nonzero (−72.8 dB); ~~27~~ | **43** / 23 (−71.9 dB); ~~35~~ | **51** / 27 (−71.5 dB); ~~43~~ | **123** / 63 (−121.7 dB); ~~95~~ |
| ↑3 / ↓3 (3) | **47** / 33 (−71.2 dB); ~~41~~ | **65** / 45 (−71.3 dB); ~~53~~ | **77** / 53 (−71.2 dB); ~~65~~ | **149** / 101 (−121.1 dB); ~~143~~ |
| ↑4 / ↓4 (4; not shipped as one stage, decision 6 — the design serves 4/3 and 3/4) | **63** / 49 (−71.4 dB) | **87** / 67 (−71.1 dB) | **111** / 85 (−72.3 dB) | **199** / 151 (−121.4 dB) |
| ↑6 / ↓6 (6) | **95** / 81 (−71.5 dB) | **143** / 121 (−72.1 dB) | **167** / 141 (−72.2 dB) | **299** / 251 (−121.3 dB) |
| ↑8 / ↓8 (8) | **127** / 113 (−71.5 dB) | **191** / 169 (−72.1 dB) | **223** / 197 (−72.1 dB) | **399** / 351 (−121.2 dB) |
| 3/2, 2/3, 4/3, 3/4, 8/3, 3/8 (T = ⌈N / L⌉ per phase over the band's design) | **16 / 24 / 16 / 21 / 16 / 43** | **22 / 33 / 22 / 29 / 24 / 64** | **26 / 39 / 28 / 37 / 28 / 75** | **50 / 75 / 50 / 67 / 50 / 133** |

The ↑ and ↓ designs of one ratio are the same prototype (a Nyquist filter
is its own transpose up to the gain convention), so one pin serves both —
unlike `bridge`, whose two directions are asymmetric
(`../bridge/tests/test_design.cpp:112-119`, `DirectionsAreAsymmetric`).
M2 verified that claim (`Design.UpAndDownOfOneBandAreTheSamePrototype`:
the vectors are bit-identical) before relying on it, and a mixed ratio's
design is its larger factor's band (2/3 and 3/2 share the third-band
design; `test_design.cpp` pins it). The mixed stages' MACs per output are
their trimmed rows' lengths over L — a 2/3 stage at `economy` costs 32.5,
not the 26 the v0.2 arithmetic in 2.1 estimated — and the matrix's figures
were regenerated from these counts at M4 (v0.7, section 3; risk S1).

**The fixed-point profiles, measured** (M5, `test_fixed_point.cpp`; the
reference noise, 480 frames at peak 0.9; Q15's attained stopband is the
quantized Q1.14 table's worst response on the 16384-point design grid;
Q31's equals the design's within 0.1 dB at every stage and profile, and its
worst deviation from double is 3.4e−9; Q15 / float MACs are the counted
MACs per superblock):

| Stage | Q15 stopband eco (dB) | Q15 RMS vs double eco (dBFS) | Q15 / float MACs eco | Q15 stopband tr (dB) | Q15 RMS tr (dBFS) | Q15 / float MACs tr |
|---|---|---|---|---|---|---|
| ↑2 | -72.3 | -95.8 | 23 / 23 | -77.3 | -93.4 | 55 / 63 |
| ↓2 | -72.3 | -97.5 | 23 / 23 | -77.3 | -95.7 | 55 / 63 |
| ↑3 | -70.2 | -93.5 | 45 / 45 | -76.1 | -91.2 | 89 / 101 |
| ↓3 | -70.2 | -96.8 | 45 / 45 | -76.1 | -95.7 | 89 / 101 |
| ↑6 | -71.5 | -92.4 | 121 / 121 | -78.5 | -90.7 | 219 / 251 |
| ↓6 | -71.5 | -98.6 | 121 / 121 | -78.5 | -98.3 | 219 / 251 |
| ↑8 | -71.7 | -92.3 | 169 / 169 | -78.2 | -90.2 | 303 / 351 |
| ↓8 | -71.7 | -97.9 | 169 / 169 | -78.2 | -99.5 | 303 / 351 |
| 3/2 | -70.2 | -93.4 | 45 / 45 | -76.1 | -91.1 | 89 / 101 |
| 2/3 | -69.7 | -91.0 | 65 / 65 | -73.9 | -90.6 | 125 / 149 |
| 4/3 | -71.7 | -93.6 | 67 / 67 | -77.2 | -90.6 | 131 / 151 |
| 3/4 | -72.8 | -91.0 | 87 / 87 | -75.8 | -89.2 | 167 / 199 |
| 8/3 | -71.7 | -92.2 | 169 / 169 | -78.2 | -90.4 | 303 / 351 |
| 3/8 | -71.2 | -87.9 | 189 / 191 | -69.6 | -87.1 | 325 / 399 |

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
  exact-at-DC multiply-back for ↓3 and ↓6. The table above is measured
  after it: every decimator attains its band's interpolator stopband (↓6
  −71.5 dB, ↓8 −71.7 dB at `economy`; −76 … −79 dB at `transparent`), the
  Q15 RMS deviation from double fell by 5–12 dB, full-scale DC is still
  exact, and ↓3 at `economy` moved from −72.4 to −70.2 dB (still the 70 dB
  tier). Q31 and float are unchanged. What remains format-limited in Q15
  is `transparent` (−76 … −79 dB, not 120; the outer taps of its long
  designs round to zero and are trimmed: Q15 MACs below float's) and the
  mixed ratios going down (−68.2 … −69.7 dB at the 70 dB tiers: their
  L-phase rows are normalized at the branch-spread level, the same
  coefficient-shrinking at a smaller factor), so at Q15 `economy` is the
  pairing, as `bridge` found (`../bridge/tests/test_converter_fixed_point.cpp`).
  The 70 / 120 dB promises of 2.3 and section 3 are float's and Q31's.
- **A mixed ratio going down multiplies its band's structural zeros.** 2/3,
  3/4 and 3/8 run the L-phase machine over a band-M design: the zeros stride
  by M, the rows by L, so every row crosses every M-th zero and the trimmed
  dot holds them — 20 of 65 MACs per superblock for 2/3 at `economy`, 20 of
  87 for 3/4, 22 of 191 for 3/8. Interpolators, decimators and the mixed
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

**The icount baselines, measured** (M6, `bench/icount/`: 2 s of stereo at
48 kHz streamed in 32-frame blocks, counted under QEMU by the family's
plugin, `bench/baselines.json`, as re-recorded after the levers; the
README carries the full table, the 2/3 workloads included):

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `down2_float_eco` | 366,459,676 | 24,433,741 | 72,735,414 |
| `down2_q15_eco` | 47,494,140 | 19,807,000 | 18,558,545 |
| `up2_float_eco` | 732,658,864 | 43,442,565 | 137,930,738 |
| `up2_q15_eco` | 67,354,778 | 34,150,590 | 28,133,275 |
| `up3_float_eco` | 1,407,620,075 | 75,330,937 | 254,903,952 |
| `up3_q15_eco` | 92,695,694 | 51,156,121 | 39,793,932 |
| `construct_q15_eco` | 777,433 | 34,522 | 193,048 |

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
- **A single 4th-band stage** (`ratio<4, 1>`, `<1, 4>`): two half-bands
  until a latency-first consumer asks (decision 6, R3).
- **Optimization levers** before M6: superblock codegen, committed trip
  counts and symmetry halving are inherited from `bridge`'s pattern at M3
  where they cost nothing (the stage reuses `phase_table.h`'s layout and
  `converter.h`'s dispatch); anything beyond is M7+, one lever per change,
  measured, as `bridge`'s campaign.

## 8. Risks

| # | Risk | Mitigation |
|---|---|---|
| S1 | The harris estimates mislead the factoring policy (R3): a pinned N lands on the other side of a stage-count decision | M2 pins before M4 chooses; `matrix.py` is re-run on the pinned Ns and the matrix table is regenerated from measured lengths, not estimates; a flip is recorded in this plan's ledger. **Ledger (M4, v0.7): 35 of the 182 rows changed chain** (`matrix.py diff` against the v0.6 tables). Within the 48 family, eleven ratios: 8/1 ↑2 · ↑2 · ↑2 → ↑2 · 4/3 · ↑3; 12/1 ↑2 · ↑2 · ↑3 → ↑2 · ↑6; 16/1 ↑2 · ↑2 · ↑2 · ↑2 → ↑2 · ↑8; 24/1 ↑3 · ↑8 → ↑2 · ↑6 · ↑2; 32/1 ↑2 · ↑2 · ↑8 → ↑2 · ↑8 · ↑2; 48/1 ↑2 · ↑3 · ↑8 → ↑2 · ↑8 · ↑3; 1/8 ↓2 · ↓2 · ↓2 → ↓3 · 3/4 · ↓2; 1/12 ↓3 · ↓2 · ↓2 → ↓6 · ↓2; 1/16 unchanged; 1/24 ↓8 · ↓3 → ↓2 · ↓6 · ↓2; 1/32 ↓8 · ↓2 · ↓2 → ↓2 · ↓8 · ↓2; 1/48 ↓8 · ↓3 · ↓2 → ↓3 · ↓8 · ↓2 (22 rows). Within the 44.1 family, 8/1 ↑2 · ↑2 · ↑2 → ↑3 · 8/3, 16/1 ↑2 · ↑2 · ↑2 · ↑2 → ↑2 · ↑8 and 1/8 ↓2 · ↓2 · ↓2 → 3/8 · ↓3 (5 rows). Cross-family, eight rows: 192 → 22.05 (↓2 · ↓2 → ↓3 · 3/4 ahead of `bridge`), 384 → 11.025 (↓8 → ↓2 · ↓2 · ↓2), 384 → 22.05 (↓6 · 3/4 → ↓8), 384 → 44.1 (↓2 · ↓2 · ↓2 → ↓3 · 3/4 · ↓2), 11.025 → 384 (↑8 → ↑2 · ↑2 · ↑2 after `bridge`), 22.05 → 192 (↑2 · ↑2 → 4/3 · ↑3), 22.05 → 384 (4/3 · ↑6 → ↑8), 44.1 → 384 (↑2 · ↑2 · ↑2 → ↑2 · 4/3 · ↑3). The cause is 2.1's: a relaxed half-band saturates at m = 7 while a relaxed third-band stage is m = 5. The rules were not changed; a maintainer who prefers integer-factor chains at a few per cent adds a tie-break to `best_chain` and regenerates |
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
