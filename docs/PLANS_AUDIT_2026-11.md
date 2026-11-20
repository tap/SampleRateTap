# Adversarial audit of the IIR plan and the ×4 / ×8 / ×16 plan, 2026-11-19

Audited: the IIR plan at v7 and the ×4 / ×8 / ×16 plan at v5, against SampleRateTap `main`
3528c88, DspTap 084f2f6 and TapTools `main` (its DspTap pin 2137d86). Four lenses (DSP,
code, family rules, red team) each read both plans against the three trees; a skeptic per
lens tried to refute every finding; a completeness critic looked for what no lens covered.
The plans' next versions, `docs/IIR_PLAN.md` (v8) and `docs/X4_X8_PLAN.md` (v6), apply
every confirmed finding and gap with the maintainer's answers.

The evidence paths below (`audit/dsp/…`, `audit/redteam-verify/…` and the like) were the
audit's scratch programs in the session's working space, not files in this tree. The
numbers the plans keep from them are re-measured, and their programs committed, by the
plans' spikes (X-0 under `rational/tools/spikes/x4x8/`, IIR-0 under
`rational/tools/spikes/iir/`).

92 findings, 85 confirmed, 6 refuted, 1 uncertain; 12 gaps. Severity is the skeptic's, where it changed the auditor's.

## The IIR plan (31 confirmed)

### code-14: I6's window_frames() contradicts DspTap's flush contract, which pins exact silence

**major** · I6

> *Recommended:* `window_frames()` is the frames after which the impulse response's tail stays below the profile's stopband attenuation, computed from the largest pole radius at design time, so `flush()` keeps its meaning

**Problem.** tap::dsp::chain defines window_frames() as 'the count of zero input frames after which its output is silence', and flush() as equal to zero padding, 'and every padded output beyond it is silence'. Both DspTap's and rational's batteries assert that bit for bit. A tail that is only below A after window_frames() is not silence, so any chain containing an IIR stage would break the documented and tested contract. The plan claims flush keeps its meaning and lists no change to DspTap's chain.

**Evidence.** DspTap chain.h:46-52 (header contract); DspTap tests/test_chain.cpp:324-350, FlushEqualsZeroPaddingBitForBit, `ASSERT_EQ(b[i], sample_traits<sample>::silence())`; rational/tests/test_chain.cpp:369-390, check_flush, `ASSERT_EQ(yz[n], tap::dsp::sample_traits<S>::silence())` after flush_output_frames().

**Fix.** Define window_frames() as the stated count after which the state is exactly zero, the number I9's SilenceDecaysToExactZero already pins (with the guard in float, and the limit-cycle-free decay in Q15/Q31). Or make the change to DspTap's flush contract and its test part of IIR-a, and say so.

**Skeptic.** Reproduced. DspTap chain.h:46-56 defines window_frames() as 'the count of zero input frames after which its output is silence' and says 'every padded output beyond it is silence'. DspTap's FlushEqualsZeroPaddingBitForBit and rational's check_flush (test_chain.cpp:369-390) both ASSERT_EQ silence() after the flush. I6's recommendation, a tail that only 'stays below the profile's stopband attenuation', is not silence, so 'flush() keeps its meaning' is false for any IIR stage in a tap::dsp::chain (I10). I9's SilenceDecaysToExactZero count, with the guard, is the definition that would satisfy the contract, but I6 does not use it. Major stands.

### dsp-6: The fixed-point plan gives no headroom or scaling rule; internal nodes exceed full scale

**major** · I8

> Q15 samples with Q31 coefficients and states in the allpass sections (every aₖ is in (0, 1)), 64-bit accumulation, one rounding point per section and saturation at the output, as numbers in the header

**Problem.** An allpass keeps energy, not peak level. Inside a path, the state after each section can exceed the input's full scale for worst-case input such as a clipped or square-wave signal at full scale. States in Q31 (range ±1) can therefore overflow. The guard band biquads have high-Q poles: their recursive parts and badly ordered partial cascades gain up to +22 dB. I8 names no structure (DF1 or TDF2), no section order and no gain distribution, and saturation at the output alone does not protect intermediate nodes. The cited Jackson 1969 is about limit cycles, not scaling. Q15 and Q31 are in the first release.

**Evidence.** audit/dsp/l1.py: worst-case peak gain (L1 norm) after each section. Economy path 0 (0.0688, 0.5060): 1.14, 2.11. Path 1 (0.2522, 0.8134): 1.50, 2.69. Transparent paths up to 3.06 and 3.71. audit/dsp/sosgain.py, Chebyshev II at 16 kHz, economy, orders 5/7/9: each section's 1/A(z) peaks at +9.0 … +22.1 dB. Partial cascades ordered by decreasing pole radius peak at +5.7 / +8.8 / +11.1 dB; ordered by increasing pole radius they stay at or below 0 dB.

**Fix.** Have I8 state, as numbers per format, the state headroom (at least 2 guard bits at the 70 dB profiles and 2 to 3 at transparent, or a Q15 sample carried in a 32-bit state with stated integer bits). For the biquads, state the structure, the section order (increasing pole radius), the gain distribution and the scaling norm, citing the scaling literature. Add a test that drives full-scale square and clipped input through every profile with no internal saturation.

**Skeptic.** Reproduced. The L1 norm of one section (a + z^-1)/(1 + a z^-1) is 1 + 2a: 1.14 (a = 0.0688) and 1.50 (a = 0.2522), matching l1.py. Cascades reach 2.11/2.69 at economy and 3.06/3.71 at transparent. A full-scale DC step through a single a = 0.8134 section overshoots to 1.15. My square-wave simulation (audit/verify-dsp/sq.py) drives a full-scale square wave through economy's paths: an internal node reaches 1.93 at steady state. I8's 'Q15 samples with Q31 … states', with 'saturation at the output' only, leaves no headroom for those nodes. sosgain.py confirms the biquad partial-cascade peaks, +5.7 … +11.1 dB with decreasing-radius order and ≤ 0 dB with increasing-radius order. I8 names no structure, order or scaling rule. Jackson is cited correctly for limit cycles, but nothing covers scaling. Q15 ships in the first release, so this is a substantive design gap (the Q31-sample path with 64-bit states has headroom).

### red-10: I6's flush window breaks DspTap's pinned rule that the output after flush is silence

**major** · I6

> *Recommended:* `window_frames()` is the frames after which the impulse

**Problem.** DspTap's chain contract defines `window_frames()` as "the count of zero input frames after which its output is silence", and says flush() "equals zero-padding the chain's input, bit for bit … every padded output beyond it is silence". Tests in both repositories assert exact silence after the window. A window that ends when the tail falls below the stopband attenuation leaves nonzero output after flush(). Every chain holding an IIR stage would then violate a pinned contract, and the guard band filter is meant to sit in FIR chains too.

The plan already has the number that keeps the contract: I9's `SilenceDecaysToExactZero` (through flush-to-zero or the per-section flush) and I8's limit-cycle test both pin decay to exact zero within a stated count.

**Evidence.** - /home/user/DspTap/include/tap/dsp/chain.h:44-55.
- /home/user/DspTap/tests/test_chain.cpp:326-352 (FlushEqualsZeroPaddingBitForBit, `ASSERT_EQ(b[i], …silence())`) and :355-378 ("Drained: the next window of zeros produces silence only").
- rational/tests/test_chain.cpp:394 and test_stage.cpp:608.

**Fix.** Define an IIR stage's `window_frames()` as that pinned decay-to-exact-zero count, per format and per target flush mode. Run FlushEqualsZeroPaddingBitForBit over chains holding IIR stages and the guard band filter. Otherwise, change DspTap's contract explicitly in IIR-a and say so.

**Skeptic.** DspTap chain.h:44-55 defines window_frames() as the zero-input count 'after which its output is silence', and says flush() equals zero-padding bit for bit with silence beyond. test_chain.cpp's FlushEqualsZeroPaddingBitForBit and FlushOutputFramesIsExact… assert exact silence. rational's test_chain.cpp:369-390 does too. I6's recommended window (the tail below the stopband attenuation) leaves nonzero output after flush, yet the plan asserts that 'flush() keeps its meaning'. I8 and I9 already pin a decay to exact zero that could serve as the window.

### red-11: The cascade costs assume per-stage relaxed designs that nothing in the plan provides

**major** · 2.4 / I3 / I4 / I10

> Each stage of a half-band cascade is designed at its own rate, with

**Problem.** Section 2.4's economy costs for IIR cascades (8, 16 and 24 MACs at ×4, ×8 and ×16), and I12's comparison built on them, assume each stage is relaxed to its design divisor (N5, N3). rational's basic_chain does this for FIR stages. The IIR plan gives IIR stages no relaxation:
- I3 and I4 pin and tabulate the named profiles only;
- I10 has no IIR chain type, and `tap::dsp::chain` computes no divisors;
- "relaxed" appears only in 2.4's prose.

As specified, the first consumers' power-of-2 cascades run the full-profile N9 (4 coefficients) at every stage: 12 MACs at ×4, 28 at ×8 and 60 at ×16, that is 1.5, 1.75 and 2.5 times the table.

**Evidence.** rational/include/tap/sr/rational/chain.h:96-102, 124-125 and 166 (`p.relaxed(k_divisors[I])`). Arithmetic: 4·2 + 4 = 12; 4·4 + 4·2 + 4 = 28; 4·8 + 4·4 + 4·2 + 4 = 60. Plan text: IIR_PLAN.md lines 184-203, 242-246, 262-268 and 418-422.

**Fix.** Add relaxed IIR designs per divisor: a `relaxed(d)` for IIR stages, with compile-time tables at the divisors power-of-2 cascades use. Add a way to compose with those divisors: an IIR chain that computes them, or a documented `relaxed(d)` for callers. State the cascade numbers both with and without relaxation.

**Skeptic.** Section 2.4's cascade orders (N5·N9, …) and costs (8, 16, 24) assume each stage is designed at its own divisor. Searching the IIR plan for 'relax' or 'divisor' finds only 2.4's prose. I3 and I4 pin and tabulate named profiles only, near-linear has committed tables per named profile, and I10 composes through tap::dsp::chain, which computes no divisors (rational's basic_chain does, chain.h:96-102 and :166, over FIR ratios only). Unrelaxed N9 at every stage gives 12, 28 and 60 MACs, which I checked: 1.5, 1.75 and 2.5 times the table. That undermines the costs the first consumers and I12's bar rely on.

### rules-13: I6's window_frames() breaks DspTap's flush contract (silence after the window)

**major** · I6

> window. *Recommended:* `window_frames()` is the frames after which the impulse

**Problem.** DspTap's chain contract defines window_frames() as "the count of zero input frames after which its output is silence". It says flush() "equals zero-padding the chain's input, bit for bit … every padded output beyond it is silence". DspTap's and rational's FlushEqualsZeroPaddingBitForBit tests pin both. I6's "tail stays below the profile's stopband attenuation" leaves non-zero output after a mixed chain's flush, so the contract fails. DspTap's rules call any change to a documented contract point a breaking change for every consumer.

**Evidence.** DspTap include/tap/dsp/chain.h:44-55; DspTap tests/test_chain.cpp:325-350 (`FlushEqualsZeroPaddingBitForBit`, ASSERT_EQ silence); rational/tests/test_chain.cpp:369-394; DspTap CLAUDE.md "Fixed numeric contracts".

**Fix.** Define window_frames() as the exact-zero decay count that I8 and I9 already pin (SilenceDecaysToExactZero, the limit-cycle count), so the contract holds unchanged. Otherwise change DspTap's chain contract in IIR-a as a documented breaking change, with its tests.

**Skeptic.** DspTap chain.h:44-55 defines window_frames() as zero input frames "after which its output is silence", and flush() as zero-padding bit for bit, with "every padded output beyond it is silence". tests/test_chain.cpp:326-350 (FlushEqualsZeroPaddingBitForBit) ASSERT_EQs silence beyond the flush, and rational has the same test. I6 defines window_frames() as the point where the tail stays below the profile's stopband, so padded output beyond it is non-zero and the documented contract fails for any chain holding an IIR stage. The plan does not acknowledge this. Its own I8/I9 exact-zero decay counts give a contract-preserving fix.

### rules-3: rational's charter (Nyquist stages only, two machines, L ≠ M) changes, but no PR updates it

**major** · 1, I2, I7, IIR-b/IIR-c

> 3. **IIR-b, `rational`'s ↑2 / ↓2 stages**, both variants (I2–I6, I8, I9), with the golden

**Problem.** rational's charter is load-bearing:
- "realised as chains of Nyquist (L-th-band) stages";
- R2: "A stage is one Nyquist filter";
- rational/CLAUDE.md: "One Nyquist filter per stage (R2, R4)", "Two machines, chosen by the ratio at compile time", and "L ≠ M. Nothing else compiles" (ratio_1_1.cpp pins "no conversion").
The IIR half-bands are neither Nyquist FIR filters nor one of the two machines. The guard band filter is a 1:1 stage. IIR-b/IIR-c update only rational's README, the book's Part VII and R18. That leaves rational/PLAN.md section 1 and R2, rational/CLAUDE.md's charter constraints, the root README's rational row and the root CLAUDE.md ("chains of Nyquist stages") contradicting the code. This is the charter erosion the family names as risk R8 and rational as S8.

**Evidence.** rational/PLAN.md:59-60, :107 (R2); rational/CLAUDE.md:59, :72, :76; README.md:18 ("as chains of Nyquist (L-th-band) stages"); CLAUDE.md "What this is" ("chains of Nyquist stages"); ratio.h:82 ("L/M == 1 is no conversion"); IIR_PLAN.md:498-500.

**Fix.** Add a charter amendment to IIR-b and IIR-c: rational's stages are Nyquist FIR stages or two-path IIR half-bands, and a 1:1 guard band stage is admitted beside the ratios (ratio<L, M>'s static_assert unchanged). Update rational/PLAN.md section 1 and R2, rational/CLAUDE.md, the root README's rational row (with a note in the profile table on which profiles each IIR variant offers) and the root CLAUDE.md paragraph.

**Skeptic.** rational/PLAN.md section 1 says "realised as chains of Nyquist (L-th-band) stages", and R2 says "A stage is one Nyquist filter". rational/CLAUDE.md has "One Nyquist filter per stage (R2, R4)" and "Two machines, chosen by the ratio at compile time". The root README's rational row says "as chains of Nyquist (L-th-band) stages". The IIR half-bands are a third machine and are not nyquist.h FIR designs. IIR-b/IIR-c list only the README, the book's Part VII and R18, and a grep of the plan finds no mention of CLAUDE.md or the charter. One correction: the guard band filter is its own type, not ratio<1,1>, so R1's L ≠ M static_assert is not touched. The rest is a settled decision (R2) reversed without being reopened.

### code-15: I4's compile-time, libm-free rule cannot cover I7's linear-phase form, and it reverses DspTap's documented runtime-design stance

**minor, auditor said major** · I4 / I7

> Its designer is the spike's choice: DspTap's Kaiser designer (a few more taps than equiripple) or an equiripple designer, which would be new substrate.

**Problem.** I4 decides that 'the nonlinear variant and the guard band filter design with `constexpr` functions in DspTap': the named profiles get static constexpr tables, no libm, and the same bits everywhere, and the Q15 tables round from the same constexpr doubles. v7 adds the linear-phase guard band form and proposes DspTap's Kaiser designer for it. That designer is runtime double over libm, and DspTap documents this as deliberate for every designer it has. So the linear form, as proposed, gets none of I4's guarantees. I4 also departs from DspTap's written design note without acknowledging it.

**Evidence.** kaiser.h:14-22 (design note: 'all design math here is plain runtime double precision', constexpr rejected as needing 'hand-rolled constexpr transcendentals before C++26'); kaiser.h:64,95,131,265,277,317-327 (std::pow, std::sin, std::cos, std::sqrt); nyquist.h:54 ('Runtime double, like every designer here (kaiser.h's design note)').

**Fix.** Settle in I4/I7 whether the linear form gets committed tables (as the near-linear variant does), a constexpr window design, or runtime libm with tolerance pins. Have IIR-a amend kaiser.h's design note to state the new constexpr designers' scope: a handful of coefficients, not 12K-taps tables.

**Skeptic.** kaiser.h:14-22 says 'all design math here is plain runtime double precision', and nyquist.h:54 says 'Runtime double, like every designer here'. I4's rule, written in v5, covers 'the nonlinear variant and the guard band filter'. v7's I7 then adds a linear guard band form whose designer may be the runtime, libm-based Kaiser designer, or an iterative equiripple design that I4's own 'closed form' scope excludes. So the plan is now internally inconsistent about the linear form. The design note's reasoning concerns 12K-33K-tap tables, so a constexpr designer for a few coefficients does not really reverse it, but nyquist.h's sentence would need amending. Downgraded to minor: the item is explicitly left open ('the spike's choice').

### code-16: srdif_trig is the wrong precedent for constexpr floating sqrt, sin and cos

**minor** · I4

> It uses constexpr sqrt, sin and cos of its own and no libm, as DspTap's `srdif_trig` already does for the FFT's tables.

**Problem.** srdif_trig evaluates sin and cos in 64-bit integer fixed point (Q0.64 polynomials, mul_hi) and has no sqrt (the √½ constant is a literal). Its output conversion to_sample is not constexpr, and the FFT's tables are built at construction into heap storage. They are not compile-time constant tables. The prototype uses double-precision Newton and Taylor series in constexpr, a different mechanism with different bit-stability grounds. Likewise, I4's 'same bits on every compiler and target, as the FFT's tables are' rests on the FFT's integer arithmetic plus -ffp-contract=off, which a constexpr double design does not share.

**Evidence.** submodules/dsptap/include/tap/dsp/fft/srdif.h:183-337 (namespace srdif_trig: uint64 mul_hi, normalized, reciprocal_q64, k_cos_coeff/k_sin_coeff; `template <std::floating_point Sample> Sample to_sample(wide_real v) noexcept`, not constexpr); srdif.h:550-556 (heap_bytes, k_sqrt_half literal); DspTap tests/CMakeLists.txt:233-236 ('the engine's tables come from integer arithmetic (no libm), so without contraction its output bits are a function of the source'); scratchpad ftz/constexpr_halfband.cpp:15-43 (double Newton sqrt, series sin).

**Fix.** Drop the srdif_trig comparison, or reword it: 'without libm, like srdif_trig (which uses integer arithmetic at construction); unlike it, evaluated at compile time in double'. Pin the compile-time tables' bits per compiler in the spike.

**Skeptic.** srdif.h:183-337 computes sin and cos in Q0.64 integer arithmetic (mul_hi, polynomial coefficients) with constexpr helpers. to_sample (:306-307) is not constexpr, k_sqrt_half (:555) is a literal with no sqrt, and the tables are filled at construction (:610-616). The prototype (ftz/constexpr_halfband.cpp) uses double Newton and Taylor series instead. The two share 'no libm' and nothing else. DspTap's bit-stability claim rests on integer arithmetic plus -ffp-contract=off (tests/CMakeLists.txt:213-236). A custom profile that calls the same functions at run time would be subject to g++ contraction, so its bits could differ from the compile-time tables. The precedent is loose. Minor.

### code-17: The plan relies on bit-pinned float output hashes in rational that do not exist and would not hold across CI hosts

**minor, auditor said major** · I9 / 4 / 6

> FTZ changes results only for subnormal values, so the bit-pinned output hashes are recorded with the guard on.

**Problem.** rational's output-hash test deliberately pins nothing: a float hash 'depends on the compiler's contraction'. SampleRateTap passes -ffp-contract=off nowhere, and DspTap documents that g++ contracts C++ in every mode. Several parts of the plan assume such pins: §4's 'Every FIR output pin unchanged: ... output hashes', I9's test-order argument ('the bit-pinned hashes included') and §6's 'contraction move[s] the bit-pinned outputs'. If IIR-b adds float bit pins for the recursive stages, they will differ between x86 and the arm64/M55 legs, where FMA contraction applies.

**Evidence.** rational/tests/test_output_hash.cpp:12-16 ('The hashes are deliberately NOT pinned: ... a float hash depends on the compiler's contraction'); `git ls-files | xargs grep -l fp-contract` in SampleRateTap finds only PLAN.md (a G7 flag-diff note); DspTap tests/CMakeLists.txt:213-227 (fp-contract off only for its own fingerprint targets; 'g++ contracts C++ in every mode').

**Fix.** Pin Q15/Q31 outputs bit for bit and float/double against the golden leg within a tolerance. Or, if float bit pins are wanted, add -ffp-contract=off (and icx -fp-model=precise) to rational's IIR test targets in IIR-b, and say so. Correct §4's list of existing FIR pins.

**Skeptic.** rational/tests/test_output_hash.cpp:12-16 says the hashes are 'deliberately NOT pinned' because 'a float hash depends on the compiler's contraction'. Outside submodules, only PLAN.md mentions fp-contract in SampleRateTap, and DspTap's CMake notes that 'g++ contracts C++ in every mode'. I9's 'the bit-pinned output hashes are recorded with the guard on' only makes sense for float, since FTZ does not touch fixed point. Such pins would differ between x86 and the FMA-capable Arm legs. §4's 'output hashes' as existing FIR pins is loose too, though a same-job A/B comparison of the printed hashes remains valid. §4's fixed-point pins are fine. Minor: one sentence, and fixed by choosing flags or tolerances.

### code-18: The FIR ↑2 column is 0.5 MAC low at every profile

**minor** · 2.2

> | `economy` | 70 dB | 9 | 4 | 2 / 4 | 11 | 11 / 23 |

**Problem.** rational's interpolator counts its centre phase (one tap, a copy) as a MAC, so ↑2 costs (2m + 1) / 2 per output frame: 9.5 / 11.5 / 13.5 / 31.5, not 9 / 11 / 13 / 31. The ↓2 column (19 / 23 / 27 / 63) is right.

**Evidence.** audit/codebase-fidelity/band4.cpp (shipping headers, g++-13) prints up_2 macs/out 9.50, 11.50, 13.50, 31.50 and down_2 19, 23, 27, 63; matrix_rows.h ROW(8000, 16000, (up_2), 19, 2, ..., 23, 2, ..., 27, 2, ..., 63, 2, ...); rational/PLAN.md 3.3, row 2/1 '↑2 | 11.5'.

**Fix.** Use 9.5 / 11.5 / 13.5 / 31.5 in the FIR ↑2 column.

**Skeptic.** Measured with the shipping headers (audit/fidelity-verify/check.cpp, g++-13). up_2 costs 9.5, 11.5, 13.5 and 31.5 MACs per output across the four profiles, and down_2 costs 19, 23, 27 and 63. The table's FIR ↑2 column (9 / 11 / 13 / 31) is 0.5 low, and matrix_rows.h pins 19/2, 23/2, ... for the 2/1 rows. Nit.

### code-19: A relaxed FIR half-band does not stop at m = 7

**minor** · 2.4

> a relaxed FIR half-band stops getting cheaper at m = 7 taps per branch, while the 4th-band stage relaxes further.

**Problem.** The pinned relaxation tables show the economy half-band at m = 7 only at divisors 2 to 4. It drops to m = 4 at divisors 6 to 9 and to m = 3 at 12 and 16. The floor at 7 holds only inside ×8; in a ×16 half-band cascade the top stage, at divisor 8, is m = 4. rational/PLAN.md 2.1's 'however wide the transition' carries the same overstatement, and the IIR plan inherits it.

**Evidence.** design.h:50-61 (economy band 2: 7, 7, 7 at divisors 2, 3, 4; 4, 4, 4 at 6, 8, 9; 3, 3 at 12, 16). audit/codebase-fidelity/relax.cpp prints down_2 macs/out 15.0 at divisors 2-4, 9.0 at 6-9 and 7.0 at 12-16.

**Fix.** Say 'stays at m = 7 at the design divisors a ×8 chain uses (2 to 4)', and note that the half-band relaxes further at ×16's divisor 8.

**Skeptic.** Measured: the economy down_2 costs 23 MACs per output at divisor 1, 15 (m = 7) at divisors 2-4, 9 (m = 4) at 6-9 and 7 (m = 3) at 12-16, and design.h's economy band-2 column agrees (7, 7, 7, 4, 4, 4, 3, 3). The 'stops at m = 7' sentence holds only inside ×8 (divisors up to 4) and is inherited from rational/PLAN.md:171-172 ('however wide the transition'), which the pins contradict. The IIR plan's ×8 argument survives. Nit.

### code-20: IIR-b omits family rules that a DspTap 0.3.0 dependency and new headers trigger

**minor** · 5 (IIR-b)

> **IIR-b, `rational`'s ↑2 / ↓2 stages**, both variants (I2–I6, I8, I9), with the golden leg, the README, the book's Part VII and R18.

**Problem.** IIR-b depends on IIR-a's DspTap 0.3.0 kernels and guard. Under A9 that raises TAP_SR_DSPTAP_MINIMUM. The macro is defined token-identically in all three umbrella headers and checked by family.VersionMacrosAgree, and raising it updates the root README's 'Tested pairs' in the same PR. New rational headers for the IIR stage and the guard band filter would also move rational.Family.HeaderCountIsPinned, which pins 6. Separately, DspTap 0.3.0 is to be shared with the ×4/×8/×16 plan's scaffold (X-c), and 'a commit that moves submodules/dsptap names the tag it pins'. So IIR-b cannot pin until both IIR-a and the scaffold have landed and been tagged, unless the versions split.

**Evidence.** rational.h:49, bridge/include/tap/sr/bridge/ratio.h:52 and async/include/tap/sr/async/async.h:44 each `#define TAP_SR_DSPTAP_MINIMUM TAP_DSP_VERSION_ENCODE(0, 2, 0)`; README.md:197 '### Tested pairs'; tests/CMakeLists.txt:18 `set(_header_count_rational 6)` and :76 HeaderCountIsPinned; SampleRateTap CLAUDE.md (A9, Q4); IIR §5 ('IIR-a's kernels and guard share 0.3.0 with that plan's round-trip scaffold').

**Fix.** Add to IIR-b: raise the floor to 0.3.0 in all three umbrella headers, update the README's tested pairs, and update the header-count pin. Decide whether DspTap tags 0.3.0 at IIR-a and the scaffold becomes 0.4.0, or IIR-b waits for X-c.

**Skeptic.** TAP_SR_DSPTAP_MINIMUM is ENCODE(0, 2, 0) in rational.h:49, bridge ratio.h:52 and async.h:44, tests/CMakeLists.txt:18 pins `_header_count_rational 6`, and README.md:197 has 'Tested pairs'. IIR-b depends on IIR-a's 0.3.0 kernels, so under A9 it raises the floor in all three headers, updates the tested pairs, and re-pins the header count if it adds headers. None of that is listed. The 0.3.0 tag-sharing with X-c (which lands after X-b in SampleRateTap's order) also sequences the tag that IIR-b must name. Scope detail, minor.

### code-21: The IIR plan still uses spellings and reasoning that X-a, which lands first, removes

**minor** · I2 / I5 / I12

> a `static_assert` that R is `up_2` or `down_2` (the message names ↑3 / ↓3 as planned)

**Problem.** §5 has this plan land after the ×4/×8/×16 plan's 0.8.0 and 'take X0's spelling'. Three passages still describe the pre-0.8.0 code: I2's static_assert names `up_2` / `down_2`, which X0 removes with no aliases (D7); I12 says 'Otherwise ×4 stays two half-bands, as decision 6 has it for the FIR stages', but X3 reopens decision 6, and ×4 then stays two half-bands only because the generator measures it cheapest; and I5's worked example is the ↓3 · 3/4 · ↓2 matrix chain, which X3 retires.

**Evidence.** X4_X8_PLAN.md X0 ('`down3`, `up8` (today `down_3`, `up_8`)'; 'Under D7 the old spellings are removed, not aliased'); X3 ('Retirements (D7): ... presumably all four of today's ×8 chains'); IIR_PLAN.md §5 ('This plan's stages then take X0's spelling').

**Fix.** Write `up2` / `down2` in I2. Rest I12's ×4 sentence on the regenerated matrix rather than decision 6. Use a surviving chain (e.g. ↓8 · ↓2, 207/16 frames) as I5's example.

**Skeptic.** Partly handled. §5 already says 'This plan's stages then take X0's spelling', which covers I2's `up_2` / `down_2` static_assert wording, and I5's ↓3 · 3/4 · ↓2 example correctly describes today's R7. I12's 'as decision 6 has it for the FIR stages' is genuinely stale once X3 reopens decision 6, which v7's ordering makes certain: ×4 would stay two half-bands only because the generator measures it cheapest. Nit.

### code-22: A mixed chain's double latency has no home, and a raw tap::dsp::chain does not relax its FIR stages

**minor** · I5 / I10

> A chain's `latency_output_frames()` stays exact and compiles only for chains of FIR stages; a mixed chain reports a double. The DspTap `chain` concept needs no change for that (it asks latency only of chains that call it).

**Problem.** rational's basic_chain holds only basic_stage<S, Ch, Rs> for ratio types, so a chain with an IIR stage must be a raw tap::dsp::chain (I10). That class's only latency is the exact latency_output_frames(), whose per-stage helper static_asserts an exact or integer report. Something new must therefore produce 'a double': a member of DspTap's chain (IIR-a) or a free function in rational. Neither is listed. A raw tap::dsp::chain also does not apply design divisors, so the caller must construct each FIR stage at profile.relaxed(d) by hand; I10 does not say so.

**Evidence.** rational chain.h:115-117 (`class basic_chain : public tap::dsp::chain<basic_stage<S, Ch, Rs>...>`) and :6-19 (a chain through bridge is composed 'from tap::dsp::chain with profile::relaxed'); DspTap chain.h:150-158 (static_assert 'a chain's latency needs every stage to report latency_output_frames() or latency_input_samples()') and :251-256.

**Fix.** Name the API and where it lives, e.g. `group_delay_output_frames(chain, f)` and a nominal latency in rational, or a new tap::dsp::chain member in IIR-a. In I10, state that mixed chains are tap::dsp::chain with stages relaxed by the caller (or add a rational helper that does it).

**Skeptic.** DspTap chain.h:146-160 static_asserts that every stage reports latency_output_frames() as an exact_ratio or latency_input_samples(), and the chain's only latency API is the exact latency_output_frames() (:250-256). rational's basic_chain derives from tap::dsp::chain<basic_stage<S, Ch, Rs>...> (chain.h:115-116) and relaxes each stage by its design divisor in its constructor, while a raw tap::dsp::chain does not (chain.h:17-19 says callers compose bridge chains 'with profile::relaxed'). I5's 'a mixed chain reports a double' therefore has no named API home, and I10 does not say that callers must relax the FIR stages themselves. API gap, minor.

### dsp-13: The type II FIR and the Chebyshev II are sized to different passband specs

**minor** · I7 (the linear-phase form's sizes)

> | 7.61 kHz | order 5, about 10 MACs | 38 taps: 19 MACs folded, 18.5 samples (1.16 ms) |

**Problem.** The Chebyshev II reach comes from cheb2ord with rp = 0.01, a monotone passband with at most 0.01 dB total deviation. The remez lengths reproduce only with ±0.01 dB, which is 0.02 dB peak to peak. At the same spec the FIR is 2 to 4 taps longer, so the cost ratio is about 2.0, 1.7–1.8 and 1.8 rather than 1.9, 1.6 and 1.7.

**Evidence.** audit/dsp/typeii.py, minimal even-length remez meeting 70 dB from the Chebyshev's edges at 16 kHz. ±0.01 dB: 38 / 46 / 62 taps (reproduces the plan). 0.01 dB peak to peak: 40 / 48–50 / 64 taps (20 / 24–25 / 32 MACs; 19.5 / 23.5–24.5 / 31.5 samples). Also checked: audit/dsp/cheb.py reproduces 2.3's reach table and edges (7.92, 7.61, 7.25, 6.95, 6.73 kHz).

**Fix.** Compare at one passband spec, and update the taps, MACs and delays (or state the FIR's ±0.01 dB).

**Skeptic.** Ran typeii.py. With ±0.01 dB the minimum even lengths are 38/46/62, which reproduces the plan, but their actual peak-to-peak deviation is 0.0145–0.0183 dB. With 0.01 dB peak to peak, as cheb2ord's rp = 0.01 gives the Chebyshev, they are 40/48–50/64. So the two columns use different passband specs. The effect is 2–4 taps (5–7 %); 'about twice the MACs' still holds. Arguably a nit.

### dsp-14: A section sticks at a small multiple of the smallest subnormal, not at the smallest

**minor** · I9 (subnormals)

> on any target, a first-order section with |a| > 1/2 sticks at the smallest subnormal: under round-to-nearest, a times it rounds back to itself, so the state never reaches zero.

**Problem.** The conclusion (it never reaches zero) is right, but the mechanism as stated is off. The state k·s stays put whenever round(a·k) = k, which holds for every k ≤ 1/(2(1 − a)). So the state stops above the smallest subnormal s: at 2s for economy's a = 0.8134, and up to 6s for transparent's a = 0.9188.

**Evidence.** audit/dsp/ftz_m33.cpp on QEMU M33 with FZ = 0: float stays at 2.8026e-45 = 2 · 2^-149 and double at 9.88e-324 = 2 · 2^-1074 for a = 0.8134.

**Fix.** Write "sticks at k times the smallest subnormal, k ≤ 1/(2(1 − a)), for 1/2 < a < 1".

**Skeptic.** My QEMU M33 run with FZ = 0 matches. a = 0.8134 sticks at 2.80e-45 = 2·2^-149 (float) and 9.88e-324 = 2·2^-1074 (double). a = 0.9188 sticks at 8.41e-45 = 6·2^-149 and 2.96e-323 = 6·2^-1074. That agrees with k ≤ 1/(2(1−a)): 2.68 gives 2, and 6.16 gives 6. The plan's conclusion (never reaches zero for |a| > 1/2) is right; only the stated sticking value, the smallest subnormal, is imprecise. A nit-level wording fix.

### dsp-15: The floating-point environment test fails on whole-register equality when the host has FTZ set

**minor** · I9 (pinned tests)

> the caller's control register is unchanged after a call (`IirStage.LeavesTheFloatingPointEnvironmentAsItFoundIt`).

**Problem.** MXCSR and FPSCR also hold sticky exception flags. When the bits are already set, the conditional guard writes nothing, so the inexact flag raised by the stage's own arithmetic stays set. When it does write, the restore erases any flags raised during the call. The two paths therefore leave the caller's flags differently, and a test comparing the whole register passes in one host state and fails in the other.

**Evidence.** audit/dsp/mxcsr.cpp (the plan's conditional guard). Host FTZ|DAZ off: MXCSR 0x1f80 before and 0x1f80 after. Host on: 0x9fc0 before and 0x9fe0 after (bit 5, PE, set). The control bits are equal in both cases.

**Fix.** Compare only the control bits in the test (MXCSR & ~0x3F; FPSCR or FPCR without the cumulative flags and NZCV). Have the restore path rewrite only the control bits, so both paths leave the caller's flags the same way.

**Skeptic.** Rebuilt and ran mxcsr.cpp. With the host's FTZ|DAZ off, MXCSR reads 0x1f80 before and after, because the restore erases the flags raised in the call. With them on, it reads 0x9fc0 before and 0x9fe0 after: no write happens, and PE stays set. The control bits are equal in both cases. So a whole-register comparison passes in one host state and fails in the other, and the two branches treat the caller's sticky flags differently. It is a test-design caveat, fixed by comparing (and restoring) only the control bits.

### dsp-2: The IIR group delays come from an ordinary elliptic low-pass, not from the two-path half-band

**minor, auditor said major** · 2.2 (and I5, which quotes it)

> Passband group delay, nonlinear variant, in high-rate samples: `economy` 2.5 … 5.9 (DC to the 3/8 edge) against the FIR's flat 21; `transparent` 4.2 … 13.4 against 61.

**Problem.** These numbers belong to a filter the structure cannot realize: an elliptic low-pass with its stopband at exactly A, about −13 dB at a quarter of the rate, which is not bireciprocal and cannot be split into two allpass paths. The plan's own closed-form design (I4, the prototype's coefficients) delays less: about 16 % less at the economy passband edge and 11 % less at transparent. I5 repeats "2.5 to 5.9 high-rate samples at economy" as the latency story.

**Evidence.** audit/dsp/gdcheck.py with audit/dsp/hb_iir.py. Economy's two-path design (0.0688, 0.2522, 0.5060, 0.8134, the prototype's coefficients bit for bit, −77.6 dB) has group delay 2.40 … 4.96 high-rate samples from DC to 3/16 of the high rate, checked with scipy.signal.group_delay on the full transfer function. Transparent N17 (−127.9 dB) has 4.16 … 11.92. The plan's figures match scipy.signal.ellip(9, rp, 70, 0.375): 2.50 … 5.86, and ellip(17, rp, 120, 5/12): 4.19 … 13.44. Those filters are −13.24 and −15.34 dB at a quarter of the rate, where a half-band is −3 dB.

**Fix.** Use 2.4 … 5.0 (economy) and 4.2 … 11.9 (transparent) from the two-path design, or mark them for IIR-0 to pin from the real structure. Update I5's "2.5 to 5.9" to match.

**Skeptic.** Reproduced independently (audit/verify-dsp/gd_indep.py, analytic per-section group delay). The constexpr prototype's economy coefficients (0.06883, 0.25222, 0.50600, 0.81339) give a two-path H with group delay 2.40 … 4.96 high-rate samples from DC to 3/16 fs, and −3.01 dB at fs/4. gdcheck confirms the plan's 2.50 … 5.86 and 4.19 … 13.44 come from scipy.signal.ellip(9/17, …), which is −13.2 / −15.3 dB at a quarter of the rate, so not a half-band. The real transparent N17 gives 4.16 … 11.92. I5 repeats '2.5 to 5.9'. Downgraded: section 2.2 marks these as estimates for IIR-0 to pin. The error overstates the delay by 4 to 19 %, in the favourable direction, and no decision depends on the exact numbers.

### dsp-4: The FTZ guard does nothing for the double profile on M33 and M4F, where double is soft-float

**minor, auditor said major** · I9

> Every float target the family builds for has a flush-to-zero bit:

**Problem.** The guard's choice is keyed on whether the target has an FZ bit. On Cortex-M33 (`-mcpu=cortex-m33`, single-precision FPU) and M4F, double arithmetic is libgcc soft-float, which FPSCR.FZ does not govern. So the double IIR stages (double ships in the first release, section 1) get neither FTZ nor the per-section flush. Their state sticks in the subnormal range for good, on soft-float paths that are already slow. `IirStage.SilenceDecaysToExactZero`, planned for every build, would fail on the M33 legs (SampleRateTap) and the M4F leg (DspTap, where the guard lives).

**Evidence.** arm-none-eabi-g++ -mcpu=cortex-m33 -mfloat-abi=hard compiles a double multiply to `bl __aeabi_dmul`; for cortex-m55 it is `vnmul.f64`. SampleRateTap/cmake/arm-cortex-m33-mps2.cmake: "single-precision FPU only, so all double arithmetic is soft-float". audit/dsp/ftz_m33.cpp on qemu-system-arm -M mps2-an505, one-multiplier section with a = 0.8134 and zero input. With FPSCR.FZ = 1: float reaches 0 after 423 samples; double is still 9.88e-324 after 20000. With FZ = 0: float stays at 2.8e-45 and double at 9.88e-324.

**Fix.** Choose the mechanism per (target, sample type). Use the guard only where the bit governs that type's arithmetic (on 32-bit Arm, double only when `__ARM_FP & 8`); use the per-section flush otherwise (double on M33, M4F and the M4 soft leg). Add Hexagon's double path to IIR-0's checks, and run the silence test for double on the M33 leg.

**Skeptic.** Reproduced on QEMU mps2-an505 with my own build (audit/verify-dsp/ftz_v.cpp, same flags as cmake/arm-cortex-m33-mps2.cmake). A double multiply compiles to bl __aeabi_dmul on cortex-m33 and cortex-m4, and to vmul.f64 on cortex-m55. With FPSCR.FZ = 1, float reaches 0 after 419 samples (a = 0.8134) and 1011 samples (a = 0.9188). Double stays at 9.88e-324 and 2.96e-323 after 30000 samples. libgcc's soft-float ignores FPSCR, so I9's 'every float target has a flush-to-zero bit' does not cover double on M33 or M4F. The pinned SilenceDecaysToExactZero would fail there for double. Downgraded: double on M33/M4F is a non-production configuration (the toolchain file says fixed point is the intended format). The plan's own every-build test would expose it at once in IIR-a's CI, and the fix is a compile-time choice per (target, type).

### dsp-5: A guard band filter before the decimator does not give "the guard band the same, at twice the cost"

**minor, auditor said major** · 2.3 ("Why after the decimator") and I11

> the filter before the decimator (at the high rate) against after it. This measures 2.3's argument: the passband identical, the guard band the same, at twice the cost before;

**Problem.** 2.3's folding argument for filtering after the decimator is right, but the comparison the demonstration notebook promises is wrong. The same order placed before the decimator is designed at twice the rate, so the bilinear warping widens its transition. It leaves the content that folds into the guard band almost untouched. Matching the after-filter's guard band needs a much higher order at twice the rate: about 6 times the MACs, not 2. 2.3's "would remove the same band at twice the rate" understates it the same way. A notebook built to this text will contradict it.

**Evidence.** audit/dsp/prepost.py, economy at a 16 kHz output (0.01 dB to 6 kHz, 70 dB). Order 5 after the decimator holds −70 dB from 7.605 kHz, with its zero at 8 kHz. The same order and passband designed at 32 kHz reaches 70 dB only at 13.2 kHz. It is −0.3 dB at 7.6 kHz, −0.6 at 8 kHz, −1.2 at 8.4 kHz (which folds to 7.6) and −10.7 at 10 kHz. cheb2ord at 32 kHz for 70 dB from 7.605 kHz gives order 14, so 15 odd: 7 × 4 + 2 = 30 MACs per high-rate sample, 60 per output, against 10 after.

**Fix.** In 2.3 and I11, say that the same filter before the decimator leaves the guard band (with these numbers), and that matching it needs order 15 at twice the rate, about 6 times the MACs. Have the notebook show both configurations.

**Skeptic.** Reproduced independently (audit/verify-dsp/prepost_v.py, cheb2ord bisection). At 16 kHz, orders 3/5/7/9 reach 70 dB at 7917/7605/7248/6954 Hz, matching 2.3's table. The same order 5 with the same 0.01 dB passband to 6 kHz, designed at 32 kHz, reaches 70 dB only at 13.2 kHz. It is −0.27 dB at 7.6 kHz, −0.59 at 8, −1.23 at 8.4 and −10.7 at 10 kHz, so it leaves the band that folds into the guard band. Matching needs order 14 at 32 kHz: about 28–30 MACs per high-rate sample, 56–60 per output against 10, so about 6×, not 2×. I11's 'guard band the same, at twice the cost' and 2.3's 'the same band at twice the rate' are wrong. Downgraded: the error only strengthens the plan's choice of after-the-decimator, and it lives in an argument and a notebook item, not a decision.

### red-12: Where the linear-phase guard filter's coefficients come from is undecided, and either named designer contradicts I4

**minor, auditor said major** · I4 / I7

> Its designer is the spike's choice: DspTap's Kaiser designer (a few more taps than

**Problem.** I4 (decided in v5) says the guard band filter is designed by constexpr functions, with its named settings as `static constexpr` tables: no design at construction, no libm, the same bits on every compiler and target. v7 added the linear form without updating I4, and either designer named for it breaks that rule:
- DspTap's Kaiser designer runs at construction with libm, as its documented design note says.
- An equiripple (Remez) designer is the iterative optimization that I4 itself rules out of compile time.

So the linear form's coefficient source is undecided: compile time, committed tables as for near_linear, or construction with libm. So is its bit reproducibility and the origin of its Q15/Q31 tables. I7's sizes (38/46/62 taps and the 37/2 latency) are Remez numbers, and the Kaiser design would be longer.

**Evidence.** /home/user/DspTap/include/tap/dsp/kaiser.h:15-22 ("all design math here is plain runtime double precision"); IIR_PLAN.md lines 262-274 (I4) and 306-324 (I7).

**Fix.** Decide the linear form's source in I4: committed tables generated by a tools/ script and pinned (the near-linear approach), or a constexpr Kaiser with its own I0 and sin, which IIR-0 would prototype. Restate I7's size table for the chosen designer.

**Skeptic.** I4, decided, has 'the guard band filter' design with constexpr functions and static constexpr tables: no libm, the same bits everywhere. DspTap's kaiser.h:15-22 states its design math is runtime double precision with libm. An equiripple designer is iterative, which I4 itself keeps off compile time. I7 leaves the linear form's designer to the spike without reconciling that with I4. Downgraded: the plan flags it as the spike's choice and already notes that Kaiser needs more taps than the Remez sizes.

### red-13: The decision to ship both guard filter forms is missing from the tests, the ratchet, the offering table and the C ABI

**minor** · section 4 / I10 / I11 / IIR-c

> - **The guard band filter**: its zero at Nyquist, its stopband from its stated edge, that

**Problem.** v7 decided both forms, but section 4's tests, the ratchet's "with and without the guard band filter", I10's offering table and I11's notebook and C ABI still describe one filter. Nothing pins the linear form's exact 37/2 latency inside an all-FIR chain, which is the property that justified it. Nothing pins its symmetry or its Q15/Q31 tables, and the plan does not say which form the ratchet runs. Section 1 and the first sentence of I7 still write `guard_band_filter<S, Ch>` without the phase parameter.

**Evidence.** IIR_PLAN.md lines 39, 296, 319-322, 422, 425-427, 476-479, 483-485 and 500.

**Fix.** Per form, add tests (latency exact inside an all-FIR chain, linear phase, the zero at Nyquist, fixed-point tables), a ratchet workload, a C ABI entry and a row in the offering table. Update the type spelling in section 1 and I7.

**Skeptic.** Section 1 (line 39) and I7's first sentence still write guard_band_filter<S, Ch> without the phase parameter. Section 4's guard tests, the ratchet's 'with and without the guard band filter', I10's offering table and I11's C ABI each describe one filter. Nothing pins the linear form's exact 37/2 latency in an all-FIR chain, its symmetry, or its Q15/Q31 tables. Only the demonstration notebook names the linear form.

### red-19: Filtering before the decimator costs about five times as much, not twice

**minor** · I11 / 2.3

> measures 2.3's argument: the passband identical, the guard band the same, at twice

**Problem.** With the same edges in Hz at the high rate, the transition is a much smaller share of the band, so the order roughly triples. Order times rate comes to 4.6 to 5.6 times the cost of filtering after the decimator, not two. An odd-order Chebyshev II at the high rate also puts its zero at the high rate's Nyquist, not at the crossover (the low Nyquist), which 2.3 gives as the reason for an odd order. Section 2.3's "would remove the same band at twice the rate" implies the same factor of two.

**Evidence.** audit/redteam/prefilter.py, output in prefilter.txt, using scipy.signal.cheb2ord with a 0.01 dB passband and 70 dB stopband. At 16 kHz, passband to 6 kHz: order 5 reaches 70 dB at 7.605 kHz, and the same edges at 32 kHz need order 14 (cost ratio 5.6). Order 7 (from 7.248 kHz) becomes order 16 (ratio 4.6). At 48 and 96 kHz the results are identical.

**Fix.** State the measured ratio in 2.3 and I11. Say that the pre-decimator filter is a different design, without the zero at the crossover.

**Skeptic.** Reproduced with cheb2ord (0.01 dB, 70 dB): at 16 kHz, order 5 reaches 7605 Hz, and the same edges at 32 kHz need order 14, a cost ratio of 5.6. Order 7 becomes 16 (4.6). 48/96 kHz gives the same. I11 says 'at twice the cost before', which is wrong by 2.3–2.8×. At the high rate, an odd-order zero sits at the high Nyquist, not at the crossover.

### red-20: Reading FPSCR through CMSIS functions would make DspTap headers depend on CMSIS-Core

**minor** · I9

> Cortex-M with an FPU, and AArch32: FPSCR.FZ (bit 24), through CMSIS's `__get_FPSCR` /

**Problem.** tap::dsp is plain C++20 with the standard library only. CMSIS-Core headers come only with the vendored CMSIS-DSP and reach consumers' include paths only under TAP_DSP_FFT_CMSIS, which is on for the M55 Helium leg alone. arm-none-eabi-g++ on its own has no cmsis_gcc.h. The guard as written therefore fails to compile on DspTap's M4F and M33 legs and on all of SampleRateTap's QEMU legs. The compiler's own `__builtin_arm_get_fpscr` and `__builtin_arm_set_fpscr` produce the same vmrs and vmsr with no header.

**Evidence.** /home/user/DspTap/CMakeLists.txt:139-148 (the CMSIS include directories are added only in the TAP_DSP_FFT_CMSIS branch). `#include <cmsis_gcc.h>` with arm-none-eabi-g++ gives "No such file or directory". audit/redteam/fpscr.cpp, compiled with -mcpu=cortex-m33, disassembles to `vmrs r0, fpscr; lsls; bmi; orr.w; vmsr fpscr, r3`.

**Fix.** Name the compiler builtins (ACLE) for AArch32 and Cortex-M, and _mm_getcsr/_mm_setcsr and mrs/msr fpcr elsewhere, with no CMSIS dependency.

**Skeptic.** DspTap's CMakeLists adds the CMSIS include directories only under TAP_DSP_FFT_CMSIS. `#include <cmsis_gcc.h>` fails with arm-none-eabi-g++. My fpscr2.cpp with __builtin_arm_get_fpscr/__builtin_arm_set_fpscr compiles for -mcpu=cortex-m33 to vmrs/vmsr with no header. Naming CMSIS's __get_FPSCR would add a dependency that tap::dsp's std-only rule forbids.

### red-21: Text about the Chebyshev II and the 1:1-stage test ended up inside the linear form's bullet in v5

**minor** · I7

> which would be new substrate. Odd orders only. DspTap's `chain` already takes a 1:1 stage

**Problem.** Through v4b, "Odd orders only", the 1:1-stage test and the declined alternative ("an option on each stage") closed I7's Chebyshev II sentence. v5 inserted the linear-form bullet in front of them, so they now sit inside the type II FIR's designer bullet. A reader applies "odd orders only" to the even-length FIR, and loses two points: that IIR-a adds the test for a 1:1 stage, and that the declined alternative concerns the filter as a whole.

**Evidence.** IIR_PLAN.v4b.md:244-248 against IIR_PLAN.md:323-328 (and v5:304).

**Fix.** Move "Odd orders only (the Chebyshev II)", the 1:1-stage test sentence and the declined alternative back under I7's main paragraph.

**Skeptic.** In v4b:244-248, 'Odd orders only', the 1:1-stage test sentence and the declined alternative close I7's Chebyshev paragraph. From v5 (line 304) through v7 (lines 323-328) they are appended to the linear-form designer bullet, where 'Odd orders only' reads as applying to the type II FIR. This is an editing artifact.

### red-22: The IIR plan still uses names and decisions that the x4 plan's X-a, which lands first, changes

**minor** · I2 / I5 / I12

> a `static_assert` that R is `up_2` or `down_2` (the message names ↑3 / ↓3

**Problem.** Section 5 now puts this plan after X-a, but its text predates that:
- I2 spells `up_2` and `down_2`, which become `up2` and `down2`. X0's new retired-word patterns would fail this plan once it moves to docs/IIR_PLAN.md, a path the gate scans.
- I5's worked example, "The matrix's ↓3 · 3/4 · ↓2 reports 117/8", is a chain X-a retires.
- I12's "as decision 6 has it for the FIR stages" cites the decision X-a reopens.
- If I12's four-path stage ships, I2's static_assert must also admit R = down4/up4, which exist after X3.

**Evidence.** IIR_PLAN.md lines 236-237, 276-278, 460-461 and 504-508; scripts/check_retired_words.py (docs/ is not in HISTORY); pertier.txt's retired list.

**Fix.** Respell under X0. Change I5's example to a chain that survives X-a. Restate I12 against reopened decision 6, and let the static_assert name the ratios I12 may add.

**Skeptic.** I2 (line 236) spells up_2 and down_2, which X0 retires and which the new gate patterns would flag once the plan is at docs/IIR_PLAN.md (docs/ is not in HISTORY). I5's example ↓3·3/4·↓2 (117/8) is a chain X3 retires. I12 cites decision 6, which X-a reopens, though its ×4 conclusion still holds. Section 5 does say the stages 'take X0's spelling', so the intent is there and the text is stale.

### red-8: A mixed chain cannot report a double latency without changing DspTap's chain, and IIR-a does not include that change

**minor, auditor said major** · I5 / section 1 / I10

> The DspTap `chain` concept needs no change for that (it asks latency only of chains

**Problem.** Section 1 and Q3 promise that a chain holding an IIR stage reports a nominal latency, how far its passband delay strays from it, and the delay at any frequency. I10 makes `tap::dsp::chain` the only way to compose: there are no named IIR chains, rational's basic_chain takes ratios only, and ratio<L, M>'s charter forbids 1:1, so the guard band filter cannot enter basic_chain either.

DspTap's chain has one latency member, `latency_output_frames()`, returning an exact_ratio; `latency_seconds()` calls it. A stage that reports neither latency form trips its static_assert. A near-linear stage that reports its exact delay path through `latency_output_frames()` would make the chain report an exact latency, the opposite of "compiles only for chains of FIR stages". So the double latency, the deviation and `group_delay_output_frames(f)` at chain level need new chain members and a new stage concept in DspTap. IIR-a's scope ("the design math and kernels") leaves them out.

**Evidence.** - /home/user/DspTap/include/tap/dsp/chain.h:135-160 (the reports_latency_* concepts and their static_assert) and :251-262 (latency_output_frames and latency_seconds).
- rational/include/tap/sr/rational/chain.h:115-116 (basic_chain over rational_ratio...).
- IIR_PLAN.md lines 63-65, 283-289, 418-420 and 577-584.

**Fix.** Add to IIR-a a stage concept that reports group delay, plus chain members for the nominal latency (double), the passband deviation and group_delay_output_frames(f). State that IIR stages never provide `latency_output_frames()`. Otherwise, drop the chain-level promise from section 1 and Q3.

**Skeptic.** DspTap chain.h has only latency_output_frames() (an exact_ratio) and latency_seconds(), with a static_assert requiring every stage to report an exact form. Nothing provides the double chain latency, the deviation, or group_delay_output_frames(f) that Q3 decides, and IIR-a's scope ('design math and kernels') leaves them out. If a near-linear stage exposes its exact delay path as latency_output_frames(), it breaks I5's 'compiles only for chains of FIR stages'. Downgraded: chain::stage<I>() is public and each stage carries k_up/k_down, so a free function in rational could compute the double with no DspTap change. The gap is an unscheduled API, not necessarily a DspTap break.

### rules-14: Using CMSIS for the flush-to-zero guard breaks DspTap's standard-library-only rule and the M4F/M33 builds

**minor, auditor said major** · I9

> - Cortex-M with an FPU, and AArch32: FPSCR.FZ (bit 24), through CMSIS's `__get_FPSCR` /

**Problem.** DspTap is header-only and standard-library-only. Its vendored CMSIS-Core is on the include path only under TAP_DSP_FFT_CMSIS, which defaults to ON only for MVE-F (the M55). On the M4F and M33 legs, and for any consumer elsewhere, `__get_FPSCR` is unavailable, and cmsis_gcc.h is GCC-only. The session's own prototype used inline `vmrs`/`vmsr` instead. GCC's builtins compile to the same two instructions for the M33 with no CMSIS.

**Evidence.** DspTap CLAUDE.md lines 3-6 ("standard library only, no frameworks"); DspTap CMakeLists.txt:109-115, :140-148; third_party/cmsis-dsp/VENDOR.md ("compiled only when TAP_DSP_FFT_CMSIS … GCC-only compiler shims"); cmsis_gcc.h:851, :872; scratchpad ftz/guard_arm.cpp:8-9 (inline asm); `arm-none-eabi-g++ -mcpu=cortex-m33 -mfloat-abi=hard` on audit/rules/fpscr.cpp gives `vmrs r0, fpscr` / `vmsr fpscr, r0` from __builtin_arm_get_fpscr/__builtin_arm_set_fpscr.

**Fix.** Specify the guard with compiler builtins or inline asm per target (`__builtin_arm_get_fpscr`/`set_fpscr`, `mrs/msr fpcr` on AArch64, `_mm_getcsr`/`_mm_setcsr` on x86), with no CMSIS include. Build its test on the M4 (no FPU, so the empty guard), M4F, M33 and M55 legs.

**Skeptic.** I9 names "CMSIS's __get_FPSCR / __set_FPSCR". DspTap's CMakeLists puts cmsis-core/Include on the include path only under TAP_DSP_FFT_CMSIS, which defaults ON only for MVE-F (M55), and DspTap is standard-library only. On the M4F and M33 those functions are unavailable. With arm-none-eabi-g++ -mcpu=cortex-m33 -mfloat-abi=hard (audit/verify-rules-2/fpscr.cpp), __builtin_arm_get_fpscr/__builtin_arm_set_fpscr compile to the FPSCR read and write (emitted as mrc/mcr p10,7, the VMRS/VMSR encoding) with no CMSIS. On M4 soft-float the builtin is undeclared, so it needs an __ARM_FP gate. The session's own prototype (ftz/guard_arm.cpp) used inline asm. Downgraded to minor: the access path is a one-line detail that fails at once on the M4F and M33 legs.

### rules-15: The new stage types omit A12, K12, D17 and the pinned header count

**minor, auditor said major** · I2, 4

> - **The family's rules**: channels in the type, both builds (exceptions and RTTI off),

**Problem.** The plan's family-rule list for `basic_iir_stage` and the guard band filter leaves out:
- A12: process, pull and flush out of line (TAP_SR_RATIONAL_NOINLINE), each the block for its channel count, with the state in a cursor. This is why the ratchet's two builds count within tolerance.
- K12/R17: count-independent state and cold code in a base built once per format and ratio.
- D17: a noexcept validate() stating the rejection in the constructor's words, the C ABI validating before it constructs, and tests through TAP_SR_EXPECT_REJECTED. The custom profiles and guard settings designed at run time (I4, I7) can be rejected.
- The dependency rule's pinned public header count (`_header_count_rational 6`), which new headers move.

**Evidence.** rational/CLAUDE.md:85 (K12), :92 (A12); PLAN.md:51 (D17); README.md:92-94 (`rational::basic_stage<…>::validate(profile)`); tests/CMakeLists.txt:18; IIR_PLAN.md:234-236, :482-485.

**Fix.** Add to I2 and section 4: the IIR stages and the guard band filter follow A12 (out-of-line blocks with a cursor), K12 (an iir_stage_base<S, R, P>) and D17 (validate(profile), rejection tests through TAP_SR_EXPECT_REJECTED). Each PR that adds a public header updates `_header_count_rational`.

**Skeptic.** Section 4's family-rules list (channels in the type, both builds, QEMU, ratchet, ceilings) omits several rules: A12 (out-of-line process/pull/flush with a cursor, rational/CLAUDE.md), K12 (D18 itself requires count-independent state built once per format), D17's validate() and constructor pair with rejection tests through TAP_SR_EXPECT_REJECTED (relevant to custom profiles and guard settings), and the pinned _header_count_rational 6 (tests/CMakeLists.txt:18). These rules bind regardless, and the header count fails CI on its own, so this is an incompleteness, minor.

### rules-2: I2 contradicts the family plan's "IIR tiers are profiles" without superseding it

**minor, auditor said major** · I2; status

> `iir_converter_linear<Ch, R>`. *Alternative:* an IIR profile of `basic_stage`; declined,

**Problem.** The family plan, section 2 ("Not engines"), says "Minimum-phase and IIR tiers are profiles." I2 recommends the opposite: a stage template of its own, with an IIR profile declined. The plan never cites that line or names it as superseded. The "family decision beside D18" it promises is never stated. PLAN.md is the family plan whose settlements are not to be re-derived, so the two would contradict each other.

**Evidence.** PLAN.md:70 ("- **Minimum-phase and IIR tiers** are profiles."); IIR_PLAN.md:24-25, :234-241. Grepping IIR_PLAN.md for "tiers" or "PLAN.md:70" finds only the unrelated line 179.

**Fix.** Name the family decision, e.g. "D19: IIR stages are their own stage type, not a profile of basic_stage; supersedes PLAN.md section 2's line", with I2's reasoning. Edit PLAN.md:70 in IIR-b.

**Skeptic.** PLAN.md:70 ("Not engines … Minimum-phase and IIR tiers are profiles") is real. I2 declines an IIR profile of basic_stage and chooses a stage template of its own. A grep of IIR_PLAN.md finds no reference to that line; the only nod is "a family decision beside D18" (line 25), which is never stated. The spirit of the line (IIR is not a separate engine) is kept, so the fix is one supersession sentence. Downgraded to minor.

### rules-24: I12 cites decision 6, which X-a (landing first) reopens

**minor** · I12

> half-bands, as decision 6 has it for the FIR stages, and the result is recorded with its

**Problem.** The IIR plan sequences itself after X-a (section 5), and X-a reopens decision 6 (X3), admitting the 4th-band stage. After X-a, ×4 stays two half-bands only because R3's cost choice picks them (53 against 67 MACs at `economy`), not because decision 6 rules it out. When I12 is carried out, this reference will be stale.

**Evidence.** IIR_PLAN.md:504-506; X4_X8_PLAN.md:274-279, :93-98.

**Fix.** Write: "Otherwise ×4 stays two half-bands, as R3's cost choice makes it for the FIR stages after the ×4/×8/×16 plan's X3".

**Skeptic.** IIR I12 says "×4 stays two half-bands, as decision 6 has it for the FIR stages". After X-a (which lands first, IIR section 5), decision 6 is reopened and the 4th-band stage is admitted. ×4 then stays two half-bands only by R3's cost choice (53 against 67 at economy, X 3.1), so the reference goes stale. A wording nit.

## The ×4 / ×8 / ×16 plan (37 confirmed)

### code-3: CHAIN_COUNT 'stays the count of distinct chains' conflicts with never reusing retired values

**major** · X7 / X3

> There is no new exported symbol, so `rational.Family.ExportedSymbolsArePinned` is unchanged, and `TAP_SR_RATIONAL_CHAIN_COUNT` stays the count of distinct chains.

**Problem.** X3 retires chain values and never reuses them, so new chains are numbered from 28 up and the value range has gaps. Today CHAIN_COUNT serves both as the count and as the exclusive upper bound that create() checks, and three places assume that. Once there are gaps, a count of distinct chains is smaller than the largest valid value + 1, so create() would reject the newest valid chains. The test that create(CHAIN_COUNT) returns NULL would fail, and the binding's `CHAINS.index(name)` would map names to the wrong constants.

**Evidence.** rational/capi/tap_sr_rational_capi.cpp:441 `if (chain < 0 || chain >= TAP_SR_RATIONAL_CHAIN_COUNT ...) return nullptr`; rational/tests/test_capi.cpp:185 `EXPECT_EQ(TAP_SR_RATIONAL_CHAIN_COUNT, 28)` and :234 `EXPECT_EQ(tap_sr_rational_create(TAP_SR_RATIONAL_CHAIN_COUNT, 0, 1), nullptr)`; rational/notebooks/tap_sr_rational_py.py:30 (`CHAINS = [... in value order`) and :201 (`CHAINS.index(name)`). X3 says '**C ABI:** new chains take new constant values; retired values are never reused.'

**Fix.** Decide what CHAIN_COUNT means after X3. Either it is one past the largest value ever assigned, with retired values rejected by the switch (and test_capi.cpp:185/:234 updated), or a new TAP_SR_RATIONAL_CHAIN_END is added alongside it. Change the binding from list positions to an explicit name-to-value dict, and say all of this in X7.

**Skeptic.** Reproduced. capi.cpp:441 rejects `chain >= TAP_SR_RATIONAL_CHAIN_COUNT`, test_capi.cpp:185 pins it at 28 and :234 expects create(CHAIN_COUNT) to return NULL, and the binding resolves names by `CHAINS.index(name)` over a list 'in value order' (tap_sr_rational_py.py:30, :201). X3 retires about four chains with values never reused and adds new ones from 28 up, so the values have gaps. X7's 'stays the count of distinct chains' would then reject the newest valid chains, and the list-index binding would map names to the wrong constants. The plan never says what CHAIN_COUNT means after X3. That is a public C ABI decision, not an inventory fix, so major stands. Tests would catch the breakage.

### code-4: Admitting the 4th-band stage leaves decision 6 stated, and pinned by a test, in places X8 does not list

**major** · X3 / X8

> the book's Part VII, *The named chains and the matrix* (`book/src/part7/chain.md:125-139`), including the "would be a lookup" sentence;

**Problem.** X3 makes `down4` / `up4` named single stages, but the C ABI refuses 4/1 and 1/4 today: make_single's switch has no case for them and falls to `default` (tap_sr_rational_capi.cpp:369-400). A test pins that refusal (test_capi.cpp:238: `EXPECT_EQ(tap_sr_rational_create_stage(4, 1, 0, 1, 1, 1), nullptr); // no single 4th-band stage (decision 6)`), and the header documents the stage set as 'one of the vocabulary's fourteen' (tap_sr_rational_capi.h:117-118). create_stage is 'what a chain through bridge composes, as the coverage-matrix test builds it'. If X-0's diff moves any cross-family row onto ↓4 (the plan expects 384 → 44.1 might move), the ABI and matrix.ipynb cannot build that row. The plan does not decide whether create_stage takes ratio 4. Decision 6 is also stated as current fact in places X8 omits: book charter.md:46-49 ('No 4'), :152 ('There is no `ratio_4_1` alias for a stage, because none ships'), :172 (the design table row); book chain.md:57-73 ('That is why no 4th-band stage ships'); ratio.h:30-32 (the file header, besides :137); rational/CLAUDE.md:65; rational/README.md:138; test_ratio.cpp:35; matrix.py:32 and :72 (`NO_SINGLE_STAGE = {4}`); and docs/ROADMAP.md:351.

**Evidence.** A grep for 'two half-bands|decision 6|4th-band stage|by_4' across tracked files returns each of the lines above. Read capi.cpp:367-400 and test_capi.cpp:238.

**Fix.** Add to X3's C ABI bullet: create_stage accepts 4/1 and 1/4, the header says sixteen ratios, and test_capi.cpp:238 is reversed into a test that accepts the stage. List every location above in X8 and in X-a's PR scope.

**Skeptic.** Reproduced. make_single (capi.cpp:367-400) has no 4/1 or 1/4 case, test_capi.cpp:238 pins the refusal with the comment '(decision 6)', and capi.h:117 documents 'one of the vocabulary's fourteen'. X3 makes down4 and up4 named single stages but says nothing about create_stage. My grep finds decision 6 stated as current fact where X8 does not list it: charter.md:47 ('No 4.') and :172 (the design-table row), chain.md:57-73 (outside the cited :125-139), ratio.h:30-32, rational/CLAUDE.md:65, rational/README.md:138, test_ratio.cpp:35 and docs/ROADMAP.md:351 (matrix.py is covered by X3). No gate catches stale prose, and the ABI change is a decision the plan has not made. Major is fair.

### red-2: A chain per tier changes 47 of the 182 rows and adds 14 chains; neither X-0 nor X-a is sized for it

**major** · X4

> **It changes the matrix's `transparent` columns,** which today run the `economy` chains:

**Problem.** X4 is decided and folded into X-a without a size, and the plan names only the 4 alias chains. Choosing the cheapest chain at transparent as well changes the chain in 47 rows: 32 within the families across 14 ratios (12/1, 24/1, 48/1, 8/3, 8/1, 16/1, 32/1, 3/8, 1/12, 1/8, 1/24, 1/16, 1/48, 1/32) and 15 across them. 14 new chains are used only at transparent, for example 3/8 and 8/3 as one-stage chains, ↓3·↓2·↓2, ↓4·↓3·3/8 and 8/3·↑3·↑4. With X3 that is 18 new chain constants and 6 retired; the plan names 4.

The generator holds one chain per row:
- md() and rows_header() cost the economy chain at all four profiles, and matrix_rows.h stores "(num, den) per profile".
- diff() walks economy_chains() only, so "X-0's diff lists every row that moves" cannot show any of X4's moves as X-0 is specified.
- used_divisors() and pins() derive the relaxation tables from the economy chains, so transparent's pins change too.

test_matrix.cpp, matrix.ipynb (728 pairs through the C ABIs), the PLAN.md tables and the book's tables all change shape as well.

**Evidence.** audit/redteam/pertier.txt reports:
- "X3+X4: rows whose transparent chain differs from their economy chain: 47"
- "distinct within-family chains today: 28", "X3 economy: 26", "X3+X4 union: 40"
- the list of new transparent-only chains.

In matrix.py: economy_chains() at :339-341, diff() at :490-512 (economy only), the rows header comment at :445.

**Fix.** Give X-0 a per-tier mode first: best_chain at transparent, and a second chain column in md, rows and diff. Report these counts before X-a, and state the new chain and C ABI constant counts in X3, X4 and X7. Then either land the per-tier regeneration as its own PR inside 0.8.0, or limit it to the rows the `_transparent` aliases need and say so.

**Skeptic.** I reproduced it independently: with band 4 admitted, 47 rows choose a different chain at transparent than at economy, 32 of them within the families. Within-family chains go from 28 today to 26 under X3 alone and 40 with X3 and X4 (pertier.txt matches). Per matrix.py, diff() walks economy_chains() only. md(), rows_header() and used_divisors()/pins() cost or derive from the economy chain alone. So X-0 as specified (band 4 plus diff plus the 'by' mode) cannot show X4's moves. X4 itself names only the four alias chains and gives no size.

### red-3: C ABI: "CHAIN_COUNT is the count of distinct chains" plus "retired values are never reused" makes new chains unreachable; create_stage still refuses band 4

**major** · X7

> `TAP_SR_RATIONAL_CHAIN_COUNT` stays the count of distinct chains.

**Problem.** `tap_sr_rational_create_format` rejects `chain >= TAP_SR_RATIONAL_CHAIN_COUNT`, and a test pins CHAIN_COUNT itself as an invalid argument. X3 retires values and appends new ones. With 40 distinct chains after X3 and X4, values run to 45 with six holes. A count of 40 rejects the six highest new chains, and value 40 is then both a live chain and the pinned invalid argument.

Separately, X3 puts single ↓4 and ↑4 stages beside `bridge` in cross-family rows, for example 192 → 11.025 becomes ↓4·bridge·↓4. The notebook and binding build cross rows with `tap_sr_rational_create_stage`, which refuses (4, 1) and (1, 4) today; a test pins that refusal "(decision 6)". X3's C ABI bullet does not mention it, so matrix.ipynb cannot reproduce the moved cross rows.

**Evidence.** - rational/capi/tap_sr_rational_capi.cpp:441 holds the validation, and its make_stage switch has no case for 4.
- rational/tests/test_capi.cpp:185 pins the count at 28; :234 expects `create(CHAIN_COUNT)` to be null; :238 expects `create_stage(4, 1, …)` to be null "// no single 4th-band stage (decision 6)".
- rational/notebooks/tap_sr_rational_py.py:214 and matrix.ipynb ("from `tap_sr_rational_create_stage` stages at the row's design divisors").
- Chain counts from pertier.txt.

**Fix.** Decide the count's meaning: either one past the largest value, with retired holes rejected through a lookup table, or a new `TAP_SR_RATIONAL_CHAIN_END` beside the count of live chains. Restate the header comment to match. In X-a, add the (4, 1) and (1, 4) cases to make_stage and flip the pinned refusal.

**Skeptic.** capi.cpp:441 rejects chain >= TAP_SR_RATIONAL_CHAIN_COUNT, and test_capi.cpp pins COUNT == 28 and create(CHAIN_COUNT) == nullptr. With 6 values retired and never reused and 18 appended, live values run 0..45 with holes, and a 'count of distinct chains' (40) rejects live chains. create_stage(4,1,…) is pinned to fail '(decision 6)', and make_stage has no band-4 case. matrix.ipynb and tap_sr_rational_py.py build cross rows through create_stage_format, so the moved cross rows (↓4·bridge·↓4, for example) cannot be reproduced. X3 and X7 say neither.

### rules-6: Retired C ABI values are never reused, but CHAIN_COUNT, create() and the binding assume dense values

**major** · X3, X7

> **C ABI:** new chains take new constant values; retired values are never reused.

**Problem.** Once retired values are never reused, the chain constants stop being dense, yet three parts of the ABI assume they are:
- `tap_sr_rational_create_format` rejects `chain >= TAP_SR_RATIONAL_CHAIN_COUNT`;
- the C ABI tests pin COUNT == 28 and create(COUNT) == NULL;
- the ctypes binding maps names by `CHAINS.index(name)` over a list "in value order".
X7 keeps CHAIN_COUNT as "the count of distinct chains". Retire today's four ×8 chains (values 10, 11, 21, 22) and add X3's two and X4's four, and there are 30 distinct chains with values up to 33. create() would then return NULL for the newest valid constants, 30 to 33.

**Evidence.** rational/capi/tap_sr_rational_capi.cpp:441; rational/tests/test_capi.cpp:185, :234; rational/notebooks/tap_sr_rational_py.py:30, :201; capi.h:48-79 (values 0-27, COUNT 28); X4_X8_PLAN.md:286, :360-361.

**Fix.** Decide one of two in X3/X7, with the C ABI tests that pin it:
- CHAIN_COUNT becomes one past the highest value ever assigned, retired values return NULL (each pinned), and the binding keeps placeholders;
- or the values are renumbered densely, an ABI break the 0.8.0 already carries.

**Skeptic.** tap_sr_rational_create_format rejects chain >= TAP_SR_RATIONAL_CHAIN_COUNT (capi.cpp:441). test_capi pins COUNT == 28 and create(COUNT) == NULL. The ctypes binding maps names by CHAINS.index over a list "in value order". X3 says retired values are never reused, and X7 says CHAIN_COUNT "stays the count of distinct chains". Retiring today's ×8 chains (values 10, 11, 21, 22) and adding X3's and X4's chains makes the values sparse, so a count-of-distinct COUNT would make create() refuse the newest valid constants, and an index-based binding would break. The tests would catch it during implementation, but it is a public-ABI contract the decided text gets wrong in a breaking release, so it needs a decision.

### code-1: X0's rename inventory misses files inside and outside rational, one of them built by CI

**minor, auditor said major** · X0

> What it renames, in `rational` only:

**Problem.** Removing today's aliases (D7) breaks code the inventory does not list. async/examples/pico2_cyccnt/main.cpp:293-295 uses `tap::sr::rational::up_2`, `down_2` and `ratio_2_3`, and CI's pico2-build job compiles it twice (ci.yml:1005-1008 and :1032), so X-a would turn that job red. The list also leaves out: the root README.md:77 (`rational::converter<2, down_2>`); all of rational/bench (icount_main, callmax_main, cmp_main, host_main and probe_main.cpp, plus compare/bench_compare.cpp, 34 lines in all); rational/examples/multirate_chain.cpp; rational/tools/reference/make_reference_vectors.py:106-118 and its generated, DO-NOT-EDIT rational/tests/reference/reference_vectors.h (`k_up_2`, `chain_ratio_3_8_down_3`, ...), which only a scipy regeneration can rename; rational/CLAUDE.md; and three records: docs/CHANNELS_PLAN.md:546-549, docs/CHANNELS_PLAN_AUDIT.md:31 and docs/channels/multicount/CMakeLists.txt:5,31-32. That last file is gate 0's probe. Its header says 'every A13 PR re-runs it', and it compiles against the old reference headers as well as the new ones, so renaming it would break the reference side. Finally, the generator writes the matrix_rows.h names from an f-string (matrix.py:471, f"up_{L}" / f"ratio_{L}_{M}"), which no grep for the old tokens finds, so a regeneration brings the old spellings back.

**Evidence.** Simulated the gate with the proposed patterns (audit/codebase-fidelity/gate_sim.py, gate_sim.txt): 757 hits in tracked files outside HISTORY, including README.md (1), async/examples/pico2_cyccnt/main.cpp (3), docs/CHANNELS_PLAN.md (4), docs/CHANNELS_PLAN_AUDIT.md (1), docs/channels/multicount/CMakeLists.txt (7) and rational/bench/** (34). Read ci.yml:1005-1034, matrix.py:466-471, reference_vectors.h:1-5,78-1403 and make_reference_vectors.py:106-118.

**Fix.** Add every file above to X0's list. Rename the pico2 example in X-a. Regenerate reference_vectors.h with its generator, and say in the PR that the change is spelling only. Change the f-string at matrix.py:471. Add the three docs/ records to check_retired_words.py's HISTORY instead of rewriting them, since the multicount probe has to keep compiling against the reference headers.

**Skeptic.** Reproduced. async/examples/pico2_cyccnt/main.cpp:293-295 uses rational::up_2, down_2 and ratio_2_3, and ci.yml:1005-1008 and :1032-1036 build it twice. My own grep for the old tokens also hits rational/bench (icount, probe, cmp, callmax, host and compare: about 34 lines), rational/examples/multirate_chain.cpp, rational/tools/reference/make_reference_vectors.py, the generated reference_vectors.h (k_up_2, k_chain_ratio_3_8_down_3, ...), README.md:77, and docs/CHANNELS_PLAN.md, CHANNELS_PLAN_AUDIT.md and channels/multicount/CMakeLists.txt. X0 lists none of the bench, example, pico2 or docs files, and its scope line says 'in rational only'. The multicount probe really cannot be renamed: one probe.cpp uses rat::up_2 against the TODAY reference tree as well as the variant, so it belongs in HISTORY. Two parts of the finding overreach. 'the tests' arguably covers reference_vectors.h, and 'matrix.py's names' covers the f-string at :471. Downgraded to minor: nothing would go wrong silently. A compile (pico2-build, the ratchet builds) or the vocabulary gate would surface every omission, so this is scope underestimation, not a wrong outcome.

### code-10: Several file:line citations are off

**minor** · 2, 6.1, X1

> The matrix chains for these ratios** (`chain.h:188-223`)

**Problem.** Four citations point at the wrong lines or only part of the text. (1) The ×16 chains are outside the cited chain.h range: `down_8_down_2` is at chain.h:227. (2) 'The band-4 pins exist at every divisor (`design.h:38-64`)' covers super_economy, economy and half of balanced; the relaxation tables run to :85, and transparent's band-4 pins are at :74-85. (3) 'its latency is one frame (`stft.h:96`)': line 96 is hop(); latency() is line 97. (4) X1 rewrites only ratio.h:137, but the file header at ratio.h:30-32 states the same thing.

**Evidence.** chain.h:194-233 (`down_8_down_2` at :227, `up_2_up_8` at :205); design.h:38-85; TapTools stft.h:96 `int hop() const`, :97 `int latency() const { return m_fftsize; }`; ratio.h:30-32 ('ratio<4, 1> and ratio<1, 4> are valid ratios (the chain by 4 is named by them) but no single 4th-band stage is shipped').

**Fix.** Cite chain.h:194-227, design.h:38-85 and stft.h:97, and have X1 rewrite ratio.h:30-32 as well as :137.

**Skeptic.** Every citation checked at HEAD; rational/ is unchanged since the plan's base, 3528c88. down_8_down_2 sits at chain.h:227, outside the cited 188-223. design.h:38-64 spans super_economy, economy and the start of balanced; transparent's table runs from :74 to :85. TapTools stft.h:96 is hop() and :97 is latency(). ratio.h:30-32 repeats what X1 rewrites at :137. Nit.

### code-11: No ratchet workload runs a ×8 chain, so there is nothing to re-record and the new ↓4 stage would go unratcheted

**minor** · X3

> The ratchet's workloads on those chains are re-recorded, and the ceilings with them.

**Problem.** No rational workload, on any target, runs a ×8 chain. The only chain workload is down2_down2_q15_eco, and the ceilings measure up_2 / down_2 probes. X3's sentence suggests the retired and new ×8 chains are already gated. In fact nothing moves, and down4 / up4 and down4_down2 would have no instruction-count, size or stack gate unless X-a adds workloads.

**Evidence.** rational/bench/baselines.json, keys across hexagon/host/m33/m55: callmax_down2_*, callmax_up2_*, construct_q15_eco, down2_down2_q15_eco, down2_float_*, down2_q15_eco, down3_*, ratio23_*, up2_*, up3_*. probe_main.cpp:45-46 (the ceilings' probes are up_2 and down_2 stages).

**Fix.** Replace the sentence with a decision: either add workloads for the 4th-band stage and the new ×8 chain (e.g. down4_down2_q15_eco and callmax_down4_*) with fresh baselines, or state that the ×8 chains stay unratcheted.

**Skeptic.** baselines.json holds only callmax_down2/up2, construct_q15_eco, down2_down2_q15_eco, down2_*, down3_*, ratio23_*, up2_* and up3_* on every target, and probe_main.cpp:45-46 uses up_2 and down_2 stages. No ×8 chain is ratcheted, so X3's 're-recorded' is moot. The new ↓4 stage and the ↓4 · ↓2 chain would go ungated unless X-a adds workloads. Section 5's 'every ... ratchet count stays identical' agrees with this.

### code-12: The proposed TapTools SCENARIO names break TapTools' naming convention

**minor** · 6.5

> `ProcessedBinsAreAliasFreeToTheProfile`;

**Problem.** TapTools' Catch2 SCENARIOs are named as lowercase English sentences stating the promise. The plan gives its three TapTools SCENARIOs GoogleTest-style CamelCase names (ProcessedBinsAreAliasFreeToTheProfile, ModificationImagesStayBelowTheStopband, FullCancellationReachesTheRippleFloor).

**Evidence.** TapTools tests: every SCENARIO("...") name starts with a lowercase letter, e.g. 'the analog release closes 95 percent at the knob time and ends at exactly zero', 'record off freezes the tape bit-exactly'. A grep for SCENARIO("[A-Z] returns nothing.

**Fix.** Rename them as sentences, e.g. 'processed bins are alias-free to the profile', 'a modification's images stay below the stopband', 'full cancellation reaches the ripple floor'.

**Skeptic.** All 313 SCENARIO names in TapTools' tests are lowercase English sentences, and none starts with an uppercase letter. The three SCENARIOs in 6.5 are CamelCase, which is GoogleTest's style. A convention nit.

### code-2: The proposed retired-word patterns miss every chain name and C constant but hit legitimate text

**minor, auditor said major** · X0

> `scripts/retired_words.txt` gains them, as patterns (`\b(up|down)_[2368]\b`, `\bratio_[0-9]_[0-9]\b`) and the old chain names, so the vocabulary gate fails any that remain.

**Problem.** The gate compiles each pattern case-sensitively (check_retired_words.py:60, re.compile(line)). `_` is a word character, so `\b` never matches inside an underscore-joined name. As a result, the patterns match none of the 20 chain names (`up_2_up_2`, `down_3_ratio_3_4_down_2`, ...), none of the 28 C constants (`TAP_SR_RATIONAL_UP_2`, ...), and none of the prose copies of those constants (book chain.md:138, charter.md:154-155, rational/README.md:211). They do match text that is legitimate: the compile-fail cases rational/tests/compile_fail/CMakeLists.txt:25-28 (`ratio_5_1.cpp`, `ratio_4_2.cpp`, `ratio_2_2.cpp`, `ratio_1_1.cpp`, rejected ratios that are not aliases) and ratio_2_2.cpp:4; book charter.md:152-153 (`ratio_5_1`, 'it would not compile'); the three docs/ records above; and any future 'today `down_3`' sentence outside a HISTORY file. As written, the gate would fail X-a's own PR and still pass stale chain names and C constants.

**Evidence.** Tested in Python: `\b(up|down)_[2368]\b` and `\bratio_[0-9]_[0-9]\b` against 'up_2_up_2<float,1>', 'down_3_ratio_3_4_down_2', 'TAP_SR_RATIONAL_UP_2' and 'TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2' all give False; against '"ratio_5_1.cpp|of the form"' and '`ratio_4_1`' they give True. gate_sim.txt lists rational/tests/compile_fail/CMakeLists.txt (4) and ratio_2_2.cpp (1).

**Fix.** Use `(?<![A-Za-z0-9])(up|down)_[2368](?![0-9])` and `(?<![A-Za-z0-9])ratio_[0-9]_[0-9](?![0-9])`, which also catch names inside chains, and add `TAP_SR_RATIONAL_(UP|DOWN|RATIO)_[0-9]`. Either rename the compile-fail cases or add an ALLOWED regex for `ratio_[0-9]_[0-9]\.cpp`. Add the records to HISTORY. Run the gate on X-a's tree before committing the list.

**Skeptic.** Partly confirmed. Re-ran the regexes in Python. `\b(up|down)_[2368]\b` and `\bratio_[0-9]_[0-9]\b` miss 'TAP_SR_RATIONAL_UP_2' and 'TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2'. They do match the compile-fail case list at rational/tests/compile_fail/CMakeLists.txt:25-28 ('ratio_5_1.cpp|...') and the comment in ratio_2_2.cpp, which the gate scans because .txt and .cpp are in TEXT_SUFFIXES and it compiles each line with re.compile. Prose copies of the C constants (book chain.md:138, charter.md:155, rational/README.md:211) would stay stale with no hit. The chain-name half of the finding is refuted: X0 adds 'the old chain names' to retired_words.txt as explicit entries beside the patterns, so chain names are covered. Minor: the false positives fail X-a's own PR loudly, and the missed C constants are prose only.

### code-5: A chain per tier changes the generated row format and its consumers, not just a regeneration

**minor, auditor said major** · X4

> It changes the matrix's `transparent` columns, which today run the `economy` chains: the generator chooses per tier, the tables and `matrix_rows.h` are regenerated, and the C ABI gains the new chains' constants. That rides X-a's regeneration

**Problem.** A matrix row today holds exactly one chain: the economy choice, costed under all four profiles. A transparent chain different from the 70 dB chain cannot be expressed without changing the ROW macro's shape and the code that consumes it. That code includes test_matrix.cpp's builder for the 182-row pin, the generator's selection API, its `md` tables (PLAN.md 3.3 says 'the `transparent` columns use the same chains'), and scripts/book_figures.py. book_figures.py imports `mx.economy_chains()` by name and draws book/src/img/coverage-matrix.svg, which CI does not regenerate. X1/X2's test `ByAliasesAreTheirMatrixChains` relies on the new shape for the `_transparent` aliases.

**Evidence.** matrix.py:339-341 `def economy_chains(): ... best_chain(s, d, "economy")`; matrix.py:455-478 writes one `(types)` and `(divs)` per row and loops `cost_chain(c[3], s, d, prof)` over PROFILES; test_matrix.cpp:248-250 `#define ROW(SRC, DST, FLAG, DESCS, DIVS, ...) out.push_back(make_row<TAP_SR_UNPAREN DESCS>(...))`; scripts/book_figures.py:498-510 (`allc = mx.economy_chains()`); book_figures.py:28-30 ('The SVGs are committed. CI does not regenerate them').

**Fix.** Add to X-a's scope: a second chain/divisor tuple per row in the ROW format (or separate transparent rows), test_matrix.cpp's builder, matrix.py's per-tier selection and its md/rows writers, PLAN.md 3.3-3.6's table headers, book_figures.py, and a regenerated coverage-matrix.svg.

**Skeptic.** Reproduced the core point. matrix.py's economy_chains() calls best_chain(s, d, 'economy'), and the rows writer (:455-478) emits one types tuple per row and costs it under every profile with cost_chain. test_matrix.cpp:248-250 builds each row through make_row<DESCS> from that single tuple, and rational/PLAN.md:373-374 and :472 say the transparent columns 'use the same chains'. A per-tier chain therefore needs a new ROW shape (or separate rows), a new builder, and new PLAN.md tables, not just a regeneration. One part is misattributed: book_figures.py draws economy MACs only, so per-tier selection does not change it. X3's own economy changes at 1/8 and 8/1 are what stale the committed coverage-matrix.svg. Downgraded to minor: the decision is sound, and its cost is understated.

### code-6: At super_economy ×4 is an exact MAC tie, which the generator's tie-break gives to the single 4th-band stage

**minor** · 3.1 / X4

> **×4 is the cheapest chain at every tier,** so `down_by4` and `up_by4` serve all four profiles and have no `_transparent` twin.

**Problem.** At super_economy, ↓2 · ↓2 and one ↓4 cost exactly the same (49 = 49 down, 49/4 = 49/4 up). matrix.py's best_chain breaks MAC ties by stage count, and so does R3's fewer-stages rule, so a generator choosing per profile would pick the single 4th-band stage at super_economy. The claim holds only if the 70 dB tier's chain is chosen at economy. The plan says 'per (ratio, attenuation tier)' without naming the selecting profile.

**Evidence.** audit/codebase-fidelity/tie.cpp (g++-13 against the shipping headers) prints: down2.down2 macs 49/1; down4 macs 49/1; up2.up2 49/4; up4 49/4. matrix.py:283 `minimal = min(per_n.values(), key=lambda r: (r[0], r[2], r[1]))` puts MACs first, then stage count; matrix.py:295-300 is the fewer-stages rule.

**Fix.** State in X4 that the 70 dB tier's chain is selected at `economy` (as matrix.py selects today), and replace 'cheapest at every profile' in 3.1 with 'cheapest or tied (super_economy)'.

**Skeptic.** Reproduced with the shipping headers. At super_economy, one ↓4 costs 49.000 MACs per output and one ↑4 costs 12.25, against 15 × 2 + 19 = 49 for ↓2 · ↓2 and 12.25 for ↑2 · ↑2. best_chain breaks ties on (MACs, stage count, latency), so it picks the single stage. The plan's own table records 'a tie', so the 3.1 sentence 'the cheapest at every profile' contradicts the table under it. X4 says 'every tier' without naming the selecting profile. A nit: at economy, as matrix.py selects today, ↓2 · ↓2 wins clearly (53 against 67).

### code-7: '22 named chains' is 20, and X0's example constant names a chain that X3 retires in the same release

**minor** · X0

> the 22 named chains (`chain.h`);

**Problem.** chain.h defines 20 `basic_chain` aliases: 18 for the 48 family plus the two the 44.1 family's lattice chooses. chain.h:190-193's comment ('the 48 kHz family's twenty, plus the two') is itself wrong, and the plan's 22 appears to inherit it. Separately, X0's worked example renames `TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2` 'every value unchanged', but X3, in the same 0.8.0, retires that chain ('presumably all four of today's ×8 chains'). The renamed constant would never ship, and readers of the example get the wrong idea about which old spellings are renamed and which are retired.

**Evidence.** chain.h:194-233 has 20 `using ... = basic_chain<...>`; tap_sr_rational_capi.h:48-49 ('the eight one-stage chains, then the twenty multi-stage ones'); book chain.md:131 ('Twenty aliases'); rational/CLAUDE.md ('the 20 named multi-stage chains').

**Fix.** Say 20, and fix chain.h:190-193's comment in X-a. In X0, choose an example that survives X3 (e.g. `TAP_SR_RATIONAL_DOWN_8_DOWN_2` → `TAP_SR_RATIONAL_DOWN8_DOWN2`), and list separately the chains X3 retires, whose old names go straight into retired_words.txt.

**Skeptic.** chain.h has 23 `= basic_chain<` lines. Three are the chain, chain_q15 and chain_q31 templates, which leaves 20 named chains (:195-233), matching capi.h ('the twenty multi-stage ones') and the book. The plan's '22' matches the miscount in chain.h:190-193's comment ('the 48 kHz family's twenty, plus the two'). The worked example renames TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2, which X3 presumably retires in the same 0.8.0. That is a nit, since it echoes the maintainer's own example spelling.

### code-8: TapTools' DspTap pin predates version.h, chain.h and nyquist.h, so rational cannot build on it

**minor, auditor said major** · X11

> *Decided (v5):* SampleRateTap as a header-only TapTools submodule (`rational/include`), built on TapTools' own DspTap pin. `TAP_SR_DSPTAP_MINIMUM`'s `static_assert` guards the floor,

**Problem.** TapTools pins DspTap at 2137d86, a commit from before v0.1.0. That tree has no tap/dsp/version.h, chain.h or nyquist.h, so rational's headers fail on their includes before the floor's static_assert can fire. X-c therefore has to bump TapTools' DspTap pin past v0.2.0 (21 commits to v0.2.0, more to the 0.3.0 the scaffold needs), and TapTools' own kernels must build against the result. The plan does not list that bump. The static_assert also lives only in the umbrella rational.h, so a TapTools source that includes chain.h directly gets no floor check. And TapTools CI checks out with `submodules: recursive`, which would initialize SampleRateTap's nested dsptap that X11 says stays uninitialized.

**Evidence.** `git -C /home/user/TapTools submodule status` gives -2137d86... submodules/dsptap. In /home/user/DspTap: 2137d86 is an ancestor of v0.1.0 (merge-base --is-ancestor); `git ls-tree 2137d86 include/tap/dsp/` lists no chain.h, nyquist.h or version.h (chain.h and nyquist.h were added in a938556, version.h in dfd766b); `git log 2137d86..v0.2.0` has 21 commits. rational.h:34 includes tap/dsp/version.h, and :49-50 define and assert TAP_SR_DSPTAP_MINIMUM. TapTools .github/workflows/build.yml:23 `submodules: recursive`.

**Fix.** Make 'bump TapTools' DspTap pin to the 0.3.0 tag' the first step of X-c in TapTools, with its own test run. Change build.yml's checkout to initialize the named submodules. Have TapTools include rational.h (or assert TAP_DSP_VERSION itself) so the floor is checked.

**Skeptic.** Reproduced. TapTools pins DspTap at 2137d86 on both main and origin/main. That commit is an ancestor of v0.1.0, and `git ls-tree` shows no version.h, chain.h or nyquist.h in it; 21 commits lead to v0.2.0. rational.h:34 includes tap/dsp/version.h, so the build fails on includes before the static_assert at :50 can fire, and only the umbrella header carries that assert. X11's sentence 'built on TapTools' own DspTap pin ... the static_assert guards the floor' misdescribes this, and the bump is not listed. Two things soften it. X10 puts the scaffold in DspTap 0.3.0, which forces TapTools to bump anyway. And X11 already says TapTools' build changes from --recursive to a named submodule init, so the CI-checkout part of the finding is handled. Minor.

### code-9: The CMSIS-Helium acceleration claim does not apply to the double scaffold X9 picks first

**minor** · 6.1

> The M55's CMSIS-Helium backend stops at 4096 (`fft.h:258`), so on that target the ×8 and ×16 bands with 4096 points accelerate and full-band 32768- and 65536-point FFTs would not.

**Problem.** X9 makes double the first format, since TapTools and stft.h are double. The CMSIS-Helium backend serves the float profile only, and the double profile runs srdif on every target. So for the scaffold as decided, no FFT size is accelerated on the M55; the stated advantage arrives only with the later float format.

**Evidence.** fft.h:258 ('CMSIS-DSP (float, Cortex-M55) 32 … 4096'); TapTools stft.h:13-15 ('the double profile, which runs DspTap's srdif engine on every target — the vDSP / CMSIS-Helium backends serve the float profile only').

**Fix.** Qualify the sentence: 'in the float profile (X9's later format); the double scaffold runs srdif at every size'.

**Skeptic.** fft.h:258 lists 'CMSIS-DSP (float, Cortex-M55) 32 … 4096', and TapTools stft.h:13-15 states that the double profile runs srdif on every target. X9 makes double the first format, so for the scaffold as decided nothing is Helium-accelerated. The sentence holds only for the later float profile; its cost table even measures real_fft32. A qualification is missing. Nit.

### dsp-10: DspTap's Subband tests cannot detect a wrong T or decimation phase

**minor** · 6.5 (DspTap tests)

> `Subband.IdentityOpIsTheInputDelayedBitForBit`;

**Problem.** With Δ ≡ 0, the output is x[n − T] bit for bit for any T and any stages, so the identity test passes even when T is wrong. LatencyIsTheExactSumOfItsParts checks the formula against itself, not against the signal path. A misaligned T (an off-by-one, or a decimation-phase convention) would pass DspTap's whole battery and the scaffold would be tagged in 0.3.0. It would surface only later in rational's or TapTools' removal tests.

**Evidence.** audit/dsp/demo_var.cpp: the 6.6 demo with T + 1 removes 205 Hz to only −31.46 dB instead of −64.05 dB; the identity output stays bit for bit with any T.

**Fix.** Add a DspTap test that removes an in-band tone (Δ = −X in its bins, or the whole low band) through the test-local nyquist.h stages and pins the residual at those stages' ripple floor. That pins T against the actual signal path.

**Skeptic.** Rebuilt and ran demo_var. With T exact, the identity output is bit-exact and 205 Hz goes to −64.05 dB. With T + 1 (applied to the delay line, line 102), the identity output is still bit-exact, but 205 Hz goes only to −31.46 dB. With Δ ≡ 0 the identity test is independent of T. LatencyIsTheExactSumOfItsParts as named checks a formula, and BandsComposeByAddition does not test alignment. So none of the five DspTap tests pins T against the signal path. The rational and TapTools removal tests would catch it later, which keeps this minor.

### dsp-11: The transparent removal floor is −110 dB, not ≤ −120 dB; the measurement sampled only four in-band frequencies

**minor** · 6.3

> −60 to −92 dB at `economy` and ≤ −120 dB at `transparent`, measured.

**Problem.** `split` samples the passband at 0.1, 0.3, 0.375 and 0.4 r_low only. A dense sweep finds the transparent round trip's worst in-band residual 10 dB above the stated bound, near the band edge. A TapTools ripple-floor test pinned at −120 dB would fail there. The economy range holds on a dense sweep.

**Evidence.** audit/dsp/ripple.cpp (20000-point sweep of the product of stage responses), ×8 transparent ↓2³/↑2³: worst −110.0 dB at 0.4084 r_low. audit/dsp/removal.cpp, time domain with the shipping basic_chain<double, 1, down_2, down_2, down_2> and its up twin, 48 → 6 kHz: −109.4 dB at 2450.4 Hz, −118.9 at 2400 Hz, −126.5 at 1800 Hz. Economy worst: ×4 −65.4, ×8 with X3 −61.3, ×16 −62.5 dB (time domain at 3 kHz: −63.7 … −74.5 dB).

**Fix.** Write "down to −109 dB at transparent" from a dense sweep, and pin TapTools' FullCancellationReachesTheRippleFloor per profile from a dense sweep.

**Skeptic.** Independent check with my own least-squares projection at f (audit/verify-dsp/rt_v.cpp, double basic_chain<down_2 ×3> / <up_2 ×3>, transparent, dense sweep). The worst in-band residual is −110.0 dB at 0.4084 r_low, inside 5/12. Even within the decided 0.375 r_low band the worst is −114.8 dB (at 0.26 r_low), with −113.0 at 0.38. The original split.cpp (float, four points) read −120.3 … −128.3 because it sampled only 0.1/0.3/0.375/0.4. So '≤ −120 dB at transparent, measured' is false, and a TapTools test pinned there would fail. The economy range is roughly right.

### dsp-12: The ×8 column of the wider-band table measures today's chains, not the decided ↓4 · ↓2

**minor** · 6.2 (wider usable band)

> | 0.40, 70 dB | 80 % | 28.5 | 26.6 |

**Problem.** `custom` never builds the 4th-band chain. Its ×8 figures are ↓3 · 3/4 · ↓2 at p = 0.40 and three half-bands at p = 0.45. Under X3, `down_by8` and `up_by8` are ↓4 · ↓2 and ↑2 · ↑4, which cost about 16 to 20 % less at these profiles.

**Evidence.** x4x8/custom output: 26.6 = 13.62 + 12.96 (↓3·3/4·↓2 + ↑2·4/3·↑3); 35.8 = 17.88 × 2 (three half-bands each way). audit/dsp/custom4.cpp: ↓4·↓2 + ↑2·↑4 costs 22.25 at p = 0.40 and 28.75 at p = 0.45 (21.25 at 0.375, matching 3.4).

**Fix.** Re-measure the ×8 column with the decided chain (22.25 and 28.75), or label which chain it measures.

**Skeptic.** I ran x4x8/custom. At p = 0.40 the ×8 entries are ↓3·3/4·↓2 13.62 and ↑2·4/3·↑3 12.96, summing to 26.6. At p = 0.45, three half-bands each way cost 17.88 × 2 = 35.8. custom never builds ↓4·↓2. My custom4 run gives the decided ↓4·↓2 + ↑2·↑4 at 21.25 (p = 0.375, matching 3.4), 22.25 (p = 0.40) and 28.75 (p = 0.45). The ×8 column measures today's chains, not X3's.

### dsp-16: "Best with a 4th-band stage" at 1/32 is not the best

**minor** · 3.3

> | 1/32 | ↓2 · ↓8 · ↓2: 277 | ↓4 · ↓4 · ↓2: 285 | stays |

**Problem.** `sixteen` tried four hand-picked chains per ratio. A four-stage chain with the 4th-band stage costs less than both the listed 285 and the matrix's 277. R3's verdict still stands (277 is within 10 % of 269 with fewer stages), but the column is wrong as labelled.

**Evidence.** audit/dsp/more32.cpp, economy: ↓2·↓2·↓4·↓2 costs 269 MACs per output (latency 13.469 output frames). For comparison, ↓2·↓2·↓3·↓2 costs 203 at 1/24, against the matrix's 209 (no band 4).

**Fix.** Write 269 (↓2 · ↓2 · ↓4 · ↓2), or label the column "best of the candidates tried", and leave the verdicts to X-0's generator run.

**Skeptic.** Ran more32: ↓2·↓2·↓4·↓2 costs 269 MACs per output at economy. Hand-checked from design.h's pins: B2 at divisor 16 is m3, 7 taps × 16 = 112; B2 at divisor 8 is m4, 9 × 8 = 72; B4 at divisor 2 is m5, 31 × 2 = 62; B2 at divisor 1 is m11, 23. Total 269. That is below both the column's 285 (↓4·↓4·↓2) and the matrix's 277. The column is mislabelled. The verdict 'stays' holds: 277/269 = 1.03, with fewer stages.

### dsp-17: The M55 acceleration and FFT costs are float figures, but the first round trip is double

**minor** · 6.1

> The M55's CMSIS-Helium backend stops at 4096 (`fft.h:258`), so on that target the ×8 and ×16 bands with 4096 points accelerate and full-band 32768- and 65536-point FFTs would not.

**Problem.** The CMSIS-DSP backend is float-only. X9 decides double first because TapTools' stft.h is double, so nothing accelerates on the M55 in the first consumer, and the FFT cost table (measured with real_fft32) is not the cost of the double scaffold.

**Evidence.** submodules/dsptap/include/tap/dsp/fft.h:258: "CMSIS-DSP (float, Cortex-M55) 32 … 4096". x4x8/custom.cpp times tap::dsp::real_fft32. X9: "double first (TapTools and `stft.h` are double), float later".

**Fix.** Say that the acceleration applies to the later float profile, and either measure the double FFT for the table or label the table as float.

**Skeptic.** fft.h:258 lists 'CMSIS-DSP (float, Cortex-M55) 32 … 4096'. TapTools stft.h uses tap::dsp::real_fft, the double profile, and X9 decides double first. x4x8/custom.cpp times real_fft32. So 6.1's acceleration remark and its FFT cost table describe the later float profile, not the first double consumer. The first consumer is also a desktop Max object, where the M55 is moot. A labelling fix.

### dsp-9: T is not an integer for every chain measured

**minor** · 6.3, X10, X12

> Every term is exact, and T is an integer for every factoring measured.

**Problem.** The claim fails for the 44.1 family's ×8 chains that 3.1 measured. Their latencies carry a factor of 3 in the denominator, so x[n − T] cannot be an exact delay. X10's scaffold is generic over sync_stage and states no rule for this. With X3 the aliases' chains are all integer-factor, so the rule matters for any other chain.

**Evidence.** rational/tests/coverage/matrix_rows.h. 88200 → 11025 (ratio_3_8, down_3), economy latency 295/24 output frames, times 8 = 98⅓ full-rate samples. 11025 → 88200 (up_3, ratio_8_3), latency 295/3 = 98⅓. So T = 590/3 for a zero-latency op. 3.1 lists these as 12.292 and 98.3.

**Fix.** Say T is an integer for chains of integer-factor stages, whose delays are integers at each stage's higher rate. Make the scaffold reject a non-integer D · τ_down + τ_up at compile time, or give it a fractional delay.

**Skeptic.** matrix_rows.h line 180 has 88200 → 11025 (ratio_3_8, down_3) at economy latency 295/24 output frames, so 8 × 295/24 = 98⅓ full-rate samples. Line 143 has 11025 → 88200 (up_3, ratio_8_3) at 295/3 = 98⅓. T = 196⅔ + 8·L_op, not an integer. super_economy and balanced are fractional too (223/3, 343/3); transparent is integer (229). 3.1 measured these chains (12.292, 98.3), so 'every factoring measured' is false as written, though true for 3.4's round-trip rows. X3 retires these chains, so the aliases become integer. X10's scaffold is generic over sync_stage, and X12 promises an integer T, so a rule is needed.

### red-1: X3 moves more rows and retires more chains than the plan says, including the README quick start's chain

**minor, auditor said major** · X3

> Expected to move, *est.*: the 1/8 and 8/1 rows in both families (6 + 4) and whatever

**Problem.** X3 is decided, and this list sizes X-a, but it is short. With band 4 admitted, the shipping generator also moves the 1/48 and 48/1 rows, so 12 within-family rows move, not 10, plus 14 cross-family rows. Section 3.3, which claims to check the other rows, leaves both out. Six named chains lose every row, not "presumably all four": `up_2_up_8_up_3` and `down_3_down_8_down_2` also go. Both are load-bearing outside the matrix:
- the rational README's quick start, which `rational.Family.ReadmeQuickStartCompiles` compiles;
- the book's C ABI example constant;
- the Q15/Q31 chain-floor pins;
- the footprint pins.

D7 removes these names in X-a, but the plan schedules the README update for X-b (X8). Last, "The ratchet's workloads on those chains are re-recorded" describes nothing: no ratchet workload runs a ×8 chain, so X-a's ratchet measures nothing new and no ↓4 or ↑4 workload is added.

**Evidence.** I ran rational/tools/coverage/matrix.py, imported read-only, with NO_SINGLE_STAGE emptied (audit/redteam/pertier.py, output in pertier.txt):
- "X3 alone: economy chains that move: 26": 12 within the families, 14 across them.
- 384 → 8: ↓3·↓8·↓2 → ↓4·↓6·↓2, 373 → 353 MACs per output; 8 → 384: ↑2·↑8·↑3 → ↑2·↑6·↑4, 7.77 → 7.35 (rows48.txt).
- Retired: 3/8·↓3, ↑2·4/3·↑3, ↑2·↑8·↑3, ↑3·8/3, ↓3·3/4·↓2, ↓3·↓8·↓2.

Where the two extra retirements are used:
- rational/README.md:64: `down_3_down_8_down_2<float, 1> to_8k; // 384 -> 8 kHz: the matrix's 1/48 chain, 373 MACs per output`
- book/src/part7/chain.md:138 names `TAP_SR_RATIONAL_DOWN_3_DOWN_8_DOWN_2`.
- rational/tests/test_fixed_point.cpp:680-683 (chain floors) and test_footprint.cpp:243,254.
- rational/README.md:335 cites `up_2_up_8_up_3`.

rational/bench/baselines.json keys are only up2, down2, up3, down3, ratio23, down2_down2, construct_q15_eco and callmax_up2/down2.

**Fix.** Replace the estimate with the generator's numbers: 12 within-family and 14 cross-family rows at economy. Add 1/48 and 48/1 to section 3.3. Name the six retirements and the two new three-stage chains, ↓4·↓6·↓2 and ↑2·↑6·↑4. In X-a itself, rewrite:
- the README quick start and its comment;
- the book's example constant;
- the Q15/Q31 chain floors and their PLAN.md section 6 numbers;
- the footprint pins.

Replace the ratchet sentence with a new down4_down2 or up2_up4 workload, or say plainly that the ratchet does not cover X3.

**Skeptic.** I re-ran matrix.py, imported read-only, with NO_SINGLE_STAGE emptied (audit/redteam-verify/recheck.py and recheck.txt). At economy 26 rows move: 12 within the families and 14 across them. They include 384→8, where ↓3·↓8·↓2 at 373 MACs becomes ↓4·↓6·↓2 at 353, and 8→384, where 7.77 becomes 7.35. So the plan's '6 + 4' leaves out the 1/48 and 48/1 rows, and section 3.3 leaves out 1/48. Six chains retire, not four. down_3_down_8_down_2 and up_2_up_8_up_3 are cited in rational/README.md:64 and :335, book chain.md:138, test_fixed_point.cpp:680-683 and test_footprint.cpp. baselines.json has no ×8-chain workload, so the ratchet sentence describes nothing. I downgraded it because the plan marks the list *est.* and hands the count to X-0's generator diff, which would surface all of this. X0 also already rewrites the quick starts in X-a, though without knowing their chain retires.

### red-15: The alias tests and the C ABI were not updated for X4's `_transparent` aliases

**minor** · section 5 / X2 / X3 / X7

> The `transparent` gap (X4) is a pinned number, not an exception.

**Problem.** This is v4 text from when X4 meant one chain per alias; under v5 the `_transparent` aliases close that gap. Other stale points:
- `ByAliasesAreTheirMatrixChains` covers "(×4, ×8 and ×16)" only, pinning each "against its row", but a row now has two chains.
- X2 still calls each alias "the matrix's chain for its ratio".
- X7 defines six `_BY` constants and none for `down_by8_transparent` and the others, and `ByConstantsAreTheirChainsConstants` follows X7.
- X3 still says "the ×16 aliases can land without X3", but `down_by16_transparent` is ↓4·↓2·↓2, which needs X3.
- `ByAliasesAreTheCheapestFactoring` is named for a promise that is false at ×16: down_by16 is ↓8·↓2 at 165 MACs against ↓2·↓4·↓2 at 157, kept by R3's fewer-stages rule, as section 7 itself says.

**Evidence.** X4_X8_PLAN.md lines 267-270 (X2), 292-294, 304-309 (X4), 356-362 (X7), 382-392 (section 5), 572-574 (limits). x16.txt: "economy|1/16|down_8 . down_2|165" and "economy|1/16|down_2 . ratio<1, 4> . down_2|157".

**Fix.** Extend X2, X7 and section 5 to the four `_transparent` aliases and their C ABI constants. Pin each alias against its own tier's chain. Rename the factoring test to its real promise (the cheapest within R3's rule) and pin the ×16 difference. Delete the stale sentences.

**Skeptic.** X2 says each alias is 'the matrix's chain for its ratio', which no longer holds under X4's per-tier rows. X7 lists six constants and none for the _transparent aliases. Section 5's tests cover only '×4, ×8 and ×16' against 'its row'. X3 says the ×16 aliases can land without X3, but down_by16_transparent is ↓4·↓2·↓2. x16.txt shows economy 1/16 as down_8·down_2 at 165 against down_2·ratio<1,4>·down_2 at 157, so 'ByAliasesAreTheCheapestFactoring' names a false promise at ×16, which section 7 itself qualifies.

### red-16: The rename is sized as 22 named chains, there are 20, and its worked example is a chain X-a retires

**minor** · X0

> - the 22 named chains (`chain.h`);

**Problem.** chain.h has 20 named chain aliases; 22 comes from a comment in chain.h that double-counts the 44.1 kHz family's two. The rename's worked example, `TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2` becoming `TAP_SR_RATIONAL_DOWN3_RATIO3OVER4_DOWN2`, and X0's motivating name `down3_ratio3over4_down2` are both a chain that X3 retires in the same X-a. That constant ships under neither spelling.

**Evidence.** `grep -c "= basic_chain<S, Ch" rational/include/tap/sr/rational/chain.h` gives 20. The C ABI header says "the eight one-stage chains, then the twenty multi-stage ones", book/src/part7/chain.md:131 says "Twenty aliases", and rational/CLAUDE.md says "the 20 named multi-stage chains". The comment at chain.h:186-193 says "the 48 kHz family's twenty, plus the two". ↓3·3/4·↓2 appears in pertier.txt's retired list.

**Fix.** Say 20. Use a chain that survives X-a as the example, for example `down_2_down_6_down_2` becoming `down2_down6_down2`.

**Skeptic.** `grep -c "= basic_chain<S, Ch"` on chain.h gives 20. The C ABI comment says 'the twenty multi-stage ones', book chain.md:131 says 'Twenty aliases', and the chain.h comment's 'twenty, plus the two' double-counts. ↓3·3/4·↓2 is in the retired set I regenerated, so the example TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2 → …DOWN3_RATIO3OVER4_DOWN2 ships under neither spelling.

### red-17: The demo-shaped LowBand tests cannot run in the QEMU legs' memory, and the plan does not size or exclude them

**minor, auditor said major** · 6.6

> Its claims are pinned by tests in `rational/tests/`:

**Problem.** `tap_sr_rational_tests` is the one-shot battery on the M33 and M55 QEMU legs, filtered only by the names in bare_metal_main.cpp. The demo these tests pin works offline on 12 s of double audio at 48 kHz: 4.6 MB per full-rate buffer, and it holds at least three such buffers (x, back, out, about 13.8 MB) plus a 65536-point measurement FFT. The M55 image has 2 MB for data, bss and heap, and the M33 has 4 MB.

Even a minimal ×16 test needs one 4096-point frame at 3 kHz, which is 65536 full-rate samples or 512 KiB per double buffer, and an overlap-4 STFT in steady state needs several frames more. Double on the M33 is soft-float. The plan does not size these tests, stream them, or name them in the bare-metal filter. DspTap's checklist asks that the leg running each battery be named.

**Evidence.** 576,000 × 8 B = 4.61 MB, from len = 12 * 48000 in examples/low_band_fft_48k_div16.cpp:80. platform/mps3_an547/mps3_an547.ld:17 (DATA, 2M); platform/mps2_an505/mps2_an505.ld:15 (DATA, 4M); rational/tests/CMakeLists.txt:73-87; cmake/arm-cortex-m33-mps2.cmake:3-4 ("all double arithmetic is soft-float").

**Fix.** Specify the tests as streaming in blocks within about 1 MB of total state. Otherwise exclude them by name in bare_metal_main.cpp with the reason, and state which legs run them.

**Skeptic.** The demo uses len = 12*48000 doubles, 4.6 MB per buffer, with several buffers and a 65536-point measurement. The M55 linker script has DATA 2M and the M33 4M. bare_metal_main.cpp excludes only named suites, and the plan neither sizes the LowBand tests nor names them in the filter. Even the minimal ÷16 resolution test needs 65536 full-rate samples per buffer. Downgraded: the existing filter already shows how to exclude host-measurement suites with a reason, so this is a test-design detail.

### red-18: The identity result and its test hold for any T and any chain

**minor** · 6.6 / 6.3

> | an identity process | the output is the input delayed by T, bit for bit |

**Problem.** For the identity process the demo's change() returns an all-zero delta; a zero-primed interpolator turns zeros into exact zeros, so out[n] = x[n − T] + 0 by construction. That is true for any T and any chain. The "measured" row and `rational.LowBand.IdentityChangeIsTheInputDelayedBitForBit` at ×4, ×8 and ×16 test neither T, the chains, nor the alignment; only the removal test does. A reviewer will read the identity row as evidence that T is right.

**Evidence.** low_band_fft_48k_div16.cpp:51-53 (`if (identity) return delta;`, all zeros) and the output loop at :100-101.

**Fix.** Present the identity property as a structural guarantee, not a measurement. Pin T with a test that depends on it, for example cancelling a band to the ripple floor, with T ± 1 required to fail.

**Skeptic.** In the demo, change() returns an all-zero delta for identity. A zero-primed interpolator yields exact zeros, so out[n] = x[n−delay] + 0 for any delay and any chain. The identity row and test therefore check only the delay line against the scaffold's own T, not alignment. 6.3 does say 'because Δ is exactly zero', so the plan half-admits this is structural, but 6.6 lists it as 'measured'. Only the removal test depends on T.

### red-24: X-a mixes a mechanical rename with three numeric changes in one PR, so the numeric diff cannot be reviewed

**minor, auditor said major** · 8 (X-a)

> 2. **X-a, `rational`, decision 6 reopened and X0's spelling** (decided, v5):

**Problem.** X-a bundles:
- the rename: 689 matching lines in 33 files for the alias patterns alone, plus 20 chain names and 28 C ABI constants;
- X3: 26 rows move at economy, 6 chains retire, 4 are added, plus band-4 pins at new divisors, Q15/Q31 tables, chain floors and footprints;
- X4: 47 transparent rows and 14 more chains;
- the C ABI renumbering, the ratchet and ceilings, and 0.8.0.

The plan's own gate, "Every unmoved row, output hash and ratchet count stays identical", can only be checked against a base that is not also renamed. In one diff, the numeric changes to matrix_rows.h (176 lines the rename also touches), design.h and the pins hide inside rename noise.

**Evidence.** The rename scan (red-4); pertier.txt (red-1, red-2); X4_X8_PLAN.md lines 237-238 and 584-592.

**Fix.** Split X-a into X-a1, a pure rename with identical outputs, hashes, counts and row pins that a script can check; X-a2 for X3; and X-a3 for X4. Tag 0.8.0 only after the last, so consumers still break once.

**Skeptic.** X-a does bundle all of these, in one PR per section 8: the rename (689 matching lines in 33 files by my scan), X3 (26 rows), X4 (47 rows and 14 chains), the C ABI, the ratchet and 0.8.0. That is a real review risk. But 'the numeric diff cannot be reviewed' is overstated: output hashes, ratchet counts and row numbers do not depend on names, and a name map makes matrix_rows.h diffable. Splitting the PR is sensible process advice rather than a defect.

### red-26: The `_transparent` names bring back the name/profile mismatch the plan gives as its reason to decline a tier parameter

**minor** · X4

> - *Declined:* a tier in the type (`down_by8<S, Ch, tier>`). It duplicates the run-time

**Problem.** The decided aliases put the tier in the type's name instead, and "either runs at any profile". So `down_by8_transparent<float, 1>(profile::economy())` compiles and runs at economy: the name and the run-time profile disagree, which is the reason the tier parameter was declined. Under A4 a profile word means (A, passband edge); here it means "the chain cheapest at that tier".

**Evidence.** X4_X8_PLAN.md lines 302-305, 312-314 and 335-336; the A4 rule in /home/user/SampleRateTap/CLAUDE.md.

**Fix.** Either say that this disagreement is accepted and documented in the docstrings, or name the aliases by the tier's number (v3's `down_by16_120db`), or have validate() reject a profile from the other tier.

**Skeptic.** X4 declines a tier template parameter because it 'duplicates the run-time profile and lets the two disagree', then decides `_transparent` aliases that 'run at any profile'. So down_by8_transparent<float,1>(profile::economy()) has the same disagreement, carried in the name. A4's one-meaning-per-profile-word rule adds a vocabulary question. This is an inconsistency in the rationale, not a functional bug.

### red-27: TapTools pins a DspTap that predates the headers rational includes, and its CI checks out submodules recursively

**minor** · X11

> `submodules/dsptap` stays uninitialized, so TapTools' build changes from `--recursive` to a

**Problem.** TapTools pins DspTap at 2137d86, 17 commits before v0.1.0. That commit has no tap/dsp/version.h, chain.h or nyquist.h, and rational's headers include all three. So TapTools' first step is a DspTap bump across 21 commits, with changes to fft.h, decimate.h, sample_traits.h and expects.h, before TAP_SR_DSPTAP_MINIMUM's static_assert can even be reached; it is not in the PR list. TapTools' CI also checks out with `submodules: recursive`, which initializes the nested dsptap anyway, and TapTools-Max picks TapTools up transitively.

**Evidence.** In /home/user/TapTools, `git submodule status` gives 2137d86. In DspTap, `git describe --contains 2137d86` gives v0.1.0~17, and `git show 2137d86:include/tap/dsp/chain.h` fails. `git diff --stat 2137d86 v0.2.0 -- include/` touches 14 files. TapTools .github/workflows/build.yml:23 has `submodules: recursive`.

**Fix.** List TapTools' DspTap bump (to ≥ 0.3.0, for the scaffold) as the first X-c step. Change TapTools' and TapTools-Max's checkout steps, or drop the claim that the nested submodule stays uninitialized.

**Skeptic.** TapTools' submodule status shows 2137d86. In DspTap, `git describe --contains` gives v0.1.0~17, and `git show 2137d86:include/tap/dsp/{chain,version,nyquist}.h` fails, though rational includes all three. So with X11's 'built on TapTools' own DspTap pin', compilation fails at an #include before TAP_SR_DSPTAP_MINIMUM's static_assert is reached. The 21-commit bump touches 14 headers, fft.h among them, and is not listed (it is only implied by needing 0.3.0 for the scaffold). Partly refuted: X11 already says TapTools' build changes from --recursive to a named submodule init, which covers build.yml's `submodules: recursive`.

### red-4: The rename is not "in rational only": it breaks the CI-built async firmware example, and the new patterns fail on files outside the plan's list

**minor, auditor said major** · X0 / X8

> What it renames, in `rational` only:

**Problem.** The `pico2-build` CI job builds async/examples/pico2_cyccnt/main.cpp, which spells `tap::sr::rational::up_2`, `down_2` and `ratio_2_3`, so it stops compiling at X-a. X8's "a check the PR records, not an edit" is wrong for it.

The vocabulary gate skips only its HISTORY list and submodules, so the two new patterns also fail on:
- the root README.md:77;
- book/src/part7/charter.md:151-153 (X0 and X8 name only chain.md);
- docs/CHANNELS_PLAN.md, docs/CHANNELS_PLAN_AUDIT.md and docs/channels/multicount/CMakeLists.txt;
- the charter's compile-fail fixtures: `\bratio_[0-9]_[0-9]\b` matches the file names ratio_5_1.cpp, ratio_4_2.cpp, ratio_2_2.cpp and ratio_1_1.cpp listed in their CMakeLists.txt. These are rejected ratios, not retired aliases.

The IIR plan, once it moves to docs/IIR_PLAN.md, fails the gate on its own `up_2` and `down_2` (I2).

**Evidence.** - .github/workflows/ci.yml:1005-1034 (job `pico2-build`); async/examples/pico2_cyccnt/main.cpp:293-295.
- scripts/check_retired_words.py: the HISTORY tuple and the skip rule.
- rational/tests/compile_fail/CMakeLists.txt:25-28.
- I replicated the gate's file rules over `git ls-files` with both patterns: 689 matching lines in 33 files. 670 are in rational/ (matrix_rows.h 176, test_stage.cpp 118, test_fixed_point.cpp 69, …); 19 are outside it: docs 12, async 3, book 3, README.md 1.

**Fix.** Add to X0's scope:
- the async example, edited in X-a;
- the root README line and charter.md;
- the docs/ records, either added to HISTORY or rewritten;
- the compile-fail fixtures, renamed or given an ALLOWED entry, or tighten the pattern so file names do not match.

Correct X8's sentence to name the async edit, and respell the IIR plan before it moves to docs/.

**Skeptic.** async/examples/pico2_cyccnt/main.cpp:293-295 spells rational::up_2, down_2 and ratio_2_3, and ci.yml's pico2 job builds it. I replicated the gate's file rules with both new patterns: 689 lines in 33 files, 19 of them outside rational (README.md:77, charter.md, docs/CHANNELS_PLAN*.md, docs/channels/multicount/CMakeLists.txt, the async example). The compile-fail fixtures' names (ratio_5_1.cpp and the others) match \bratio_[0-9]_[0-9]\b. The IIR plan's own up_2/down_2 would hit the gate once it is in docs/. Partly handled: X0's 'the book's Part VII' covers charter.md. Downgraded because the gate and CI report every hit with file:line and each fix is mechanical.

### rules-18: X11's floor guard only works through the umbrella header, and TapTools' side is unplanned

**minor** · X11

> `TAP_SR_DSPTAP_MINIMUM`'s `static_assert` guards the floor, and the nested

**Problem.** TAP_SR_DSPTAP_MINIMUM and its static_assert live only in the umbrella rational.h. chain.h, stage.h, design.h and converter.h do not include it. A TapTools header that includes `<tap/sr/rational/chain.h>` against TapTools' own DspTap pin skips the guard X11 relies on. X11 also changes TapTools' layering. Its CLAUDE.md says general-purpose signal primitives go to DspTap and TapTools reaches DspTap only. TapTools' install exports DspTap's headers, and TapTools-Max consumes only include/ through TAPTOOLS_KERNEL_DIR. The plan names neither the TapTools CLAUDE.md change nor how the installed kernel and TapTools-Max reach rational/include.

**Evidence.** rational/include/tap/sr/rational/rational.h:49-50 (the only definition; grep of #include in the other five headers shows none of them includes rational.h); TapTools CLAUDE.md:14; TapTools CMakeLists.txt:9, :88-101; TapTools .gitmodules (dsptap only).

**Fix.** Require consumers to include `<tap/sr/rational/rational.h>`, or put the floor check in a header every public header includes, with the family test updated. Add to the TapTools step: the CLAUDE.md and README layering note, an install rule for rational/include, and the TapTools-Max include path.

**Skeptic.** TAP_SR_DSPTAP_MINIMUM and its static_assert are only in rational.h:49-50, and none of chain.h, stage.h, design.h, converter.h or ratio.h includes rational.h. A TapTools header that includes <tap/sr/rational/chain.h> skips the guard X11 relies on. TapTools' CLAUDE.md layering (DspTap only), its .gitmodules (dsptap only), its install rules (include/ and dsptap's headers only) and the TapTools-Max TAPTOOLS_KERNEL_DIR path are not addressed by X11.

### rules-19: "Re-record the ratchet on those chains": no workload runs them, and a re-record risks the midpoint rows

**minor** · X3, X-a

> The ratchet's workloads on those chains are re-recorded, and the ceilings with them.

**Problem.** No ratchet workload or ceiling probe runs a ×8 chain. The only chain workload in rational/bench/baselines.json is `down2_down2_q15_eco` (1/4, which X3 leaves alone), and the ceilings measure the up_2/down_2 probe. Under the two-sided ±3 % rule nothing should move. Re-recording anyway, with --update from one build, would overwrite the midpoint-gated rows (M55 `ratio23_q15_eco`, bridge's five Q15 rows), which the root CLAUDE.md says are never re-recorded from one build.

**Evidence.** baselines.json keys per target (only down2_down2_q15_eco is a chain); rational/bench/icount/probe_main.cpp:4-5, :45-46; ceilings.json (size/f32|q15|q31 probe figures); CLAUDE.md:99-102.

**Fix.** State that X-a moves no count and no ceiling, since renaming aliases does not change the compiled code and no ×8 workload exists, so baselines.json and ceilings.json stay byte-identical. If X3 should be ratcheted, add a ×8 workload (e.g. down4_down2_q15_eco), recorded at the midpoint of the two builds.

**Skeptic.** rational/bench/baselines.json has no ×8 workload on any target: the only chain workload is down2_down2_q15_eco (1/4). ceilings.json measures the up_2/down_2 probe (probe_main.cpp:45-46). "The ratchet's workloads on those chains are re-recorded" therefore refers to nothing, and section 8's "the ratchet and ceilings" in X-a invites a re-record that should be a no-op. A one-build --update would hit the midpoint-gated rows the root CLAUDE.md protects. Minor wording fix.

### rules-25: rational has 20 named chains, not 22

**minor** · X0

> - the 22 named chains (`chain.h`);

**Problem.** chain.h defines 20 named chains, as rational/CLAUDE.md ("the 20 named multi-stage chains") and the C ABI ("the twenty multi-stage ones", constants 8 to 27) say. The 22 comes from chain.h's own comment ("the 48 kHz family's twenty, plus the two"), which is itself wrong.

**Evidence.** `grep -c '^    using .* = basic_chain<S, Ch' rational/include/tap/sr/rational/chain.h` = 20; capi.h:48-49; rational/CLAUDE.md ("the 20 named multi-stage chains").

**Fix.** Write "the 20 named chains", and fix chain.h's comment as part of X0.

**Skeptic.** grep counts 20 `using … = basic_chain<S, Ch` named chains in chain.h, matching rational/CLAUDE.md ("the 20 named multi-stage chains") and the C ABI (8 one-stage plus 20 = 28). chain.h:191's "twenty, plus the two" is the source of the plan's 22, and that comment is itself inconsistent with the count. A nit.

### rules-4: X0's "in rational only" rename breaks async's Pico example, which CI builds

**minor, auditor said major** · X0, X8

> What it renames, in `rational` only:

**Problem.** The retired spellings are used outside rational:
- async/examples/pico2_cyccnt/main.cpp:293-295 instantiates `tap::sr::rational::up_2`, `down_2` and `ratio_2_3`;
- CI's pico2-build job builds that example unconditionally, with and without exceptions;
- the root README.md:77 spells `rational::converter<2, down_2>`.
X-a as listed therefore goes red in CI and fails its own vocabulary gate. X8's check that the bridge and async READMEs cite no named chain is true, but it misses the async example.

**Evidence.** `git ls-files | xargs grep -nE '\b(up|down)_[2368]\b|\bratio_[0-9]_[0-9]\b'` outside rational/; .github/workflows/ci.yml:1005-1009 ("Build pico2_cyccnt") and :1032 ("without exceptions"), pico2-build job with no path filter; gate simulation audit/rules/x0_gate.py.

**Fix.** Add async/examples/pico2_cyccnt/main.cpp and README.md:77 to X0's rename list and to X8's A4 note. Run the gate with the new patterns over the whole tree in X-a.

**Skeptic.** async/examples/pico2_cyccnt/main.cpp:293-295 instantiates rational::up_2, down_2 and ratio_2_3. ci.yml's pico2-build job (no path filter; on pull_request) builds that example twice. Root README.md:77 spells rational::converter<2, down_2> in prose, not in a quick start. X0's list covers none of them, and X8's check covers READMEs and named chains only. Downgraded to minor: the first CI run fails to compile and the fix is three identifiers. It is still a gap in the plan's list.

### rules-5: X0's retired-word patterns hit names that are not retired and miss the retired C constants

**minor** · X0

> them, as patterns (`\b(up|down)_[2368]\b`, `\bratio_[0-9]_[0-9]\b`) and the old chain

**Problem.** I ran check_retired_words.py's own logic with only these two patterns: 757 hits in 33 tracked files. Besides the intended aliases, they hit:
- the charter's rejected-ratio cases in rational/tests/compile_fail (`ratio_5_1.cpp`, `ratio_4_2.cpp`, `ratio_2_2.cpp`, `ratio_1_1.cpp`), which are not retired;
- records that are not in the gate's HISTORY list: docs/CHANNELS_PLAN.md, docs/CHANNELS_PLAN_AUDIT.md and docs/channels/multicount/CMakeLists.txt. Editing those to pass the gate would rewrite history.
The patterns also match no retired C ABI constant: they are lowercase, and `\b` does not fire inside `TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2`. So "the vocabulary gate fails any that remain" is false for C sources. The listed old chain names cover the chains.

**Evidence.** x0_gate.py output: "total hits 757 files 33"; rational/tests/compile_fail/CMakeLists.txt:25-28, ratio_2_2.cpp:4; docs/CHANNELS_PLAN.md:546-550; docs/CHANNELS_PLAN_AUDIT.md:31; docs/channels/multicount/CMakeLists.txt:5-38; HISTORY tuple in scripts/check_retired_words.py:24-37. A regex test shows 'TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2' and 'up_2_up_8' match neither pattern, while 'ratio_5_1.cpp' matches.

**Fix.** In X-a:
- add the three docs/ records to HISTORY;
- rename the compile_fail cases to X0's spelling, or add an ALLOWED entry for them;
- add an uppercase pattern for the retired constants, e.g. `\bTAP_SR_RATIONAL_(UP|DOWN|RATIO)_[0-9]`, which X0's new spellings and the `_BY` macros do not match.

**Skeptic.** I re-ran check_retired_words.py's own scan with the two proposed patterns: 757 hits in 33 tracked files (audit/verify-rules-2/gate.py). The hits include the compile_fail charter cases (CMakeLists.txt lines naming ratio_5_1.cpp, ratio_4_2.cpp, ratio_2_2.cpp, ratio_1_1.cpp, and ratio_2_2.cpp's own comment) and three records outside HISTORY: docs/CHANNELS_PLAN.md, docs/CHANNELS_PLAN_AUDIT.md and docs/channels/multicount/CMakeLists.txt. A regex check confirms that 'TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2' and 'up_2_up_8' match neither pattern, while 'ratio_5_1.cpp' matches. Leftover uppercase constants in code would fail to compile anyway, so the gap is limited to prose, and the severity stays minor.

### rules-7: Decision 6 is reversed in X-a, but its record is updated in X-b or not at all

**minor, auditor said major** · X3, X8, PR order

> - `rational/PLAN.md` decision 6, R3, the S1 ledger and the generated tables (with X3);

**Problem.** X3 ships `down4`/`up4` and the regenerated matrix in X-a, but the decision-record update belongs to X8, which is X-b's. Between the two PRs, rational/PLAN.md's R3 and decision 6 and rational/CLAUDE.md say "↑4 is two half-bands, never one 4th-band stage" while the code ships one. Several statements X3 or X4 contradict are on no list at all:
- rational/CLAUDE.md:65;
- rational/PLAN.md:75 ("never one stage: decision 6, R3") and :1191 (non-goal "A single 4th-band stage");
- rational/PLAN.md:373-374 ("The `transparent` columns cost the same chains at `transparent`, not a second search", which X4 reverses);
- the book: part7/charter.md:47 ("No 4") and :172, and chain.md:57-73;
- ratio.h:31 and rational/README.md:138.
rational/CLAUDE.md is the file agents are told to follow without re-deriving.

**Evidence.** grep for "4th-band|two half-band|second search" in rational/README.md, rational/CLAUDE.md, the book's part7, ratio.h and matrix.py (hits listed); X4_X8_PLAN.md:372, :593 (X8 in X-b).

**Fix.** Move the decision-record and charter-text updates into X-a, the PR that changes the decision, and add the missing locations to the list.

**Skeptic.** Partly handled. X8 lists "rational/PLAN.md decision 6, R3, the S1 ledger and the generated tables (with X3)", and line 16 says a reopened decision 6 goes to rational/PLAN.md on approval, so the decision record itself is covered. Several statements that X-a falsifies are on no list: rational/CLAUDE.md:65 ("R3: ↑4 is two half-bands, never one 4th-band stage"), rational/PLAN.md:75, the non-goal at :1191, :374 (transparent "not a second search", which X4 reverses), book part7/charter.md:47 ("No 4") and :172, chain.md:57-73 (X8 cites only :125-139), ratio.h:31 and rational/README.md:138. The plan also leaves ambiguous whether X8's docs land in X-a or X-b. Downgraded to minor: these are documentation edits.

### rules-9: The `_transparent` suffix puts a tier in the type, the arrangement X4 declined

**minor** · X4

> 120 dB tier's chains get aliases of their own, named by the family's profile word:

**Problem.** A4 says a profile name means one thing across the family. In `down_by8_transparent` the profile word selects a chain, while the profile itself stays a run-time argument ("either runs at any profile"). So `down_by8_transparent` built at `economy` is legal and reads as transparent. X4 declined `down_by8<S, Ch, tier>` because a tier in the type "duplicates the run-time profile and lets the two disagree"; the decided suffix has the same property. Under D7 a later fix would be a retirement in another breaking release.

**Evidence.** X4_X8_PLAN.md:303-305, :312-314, :335-336; README.md:32-38 (A4: "a profile name means one thing across the family").

**Fix.** Either name the tier by its number (v3's `down_by16_120db` spelling), or keep `_transparent` and state its meaning in the root README's profile section in X-b, as A4 requires. In either case, pin with a test that each such alias, run at `transparent`, is its matrix row.

**Skeptic.** X4 declines `down_by8<S, Ch, tier>` because it "duplicates the run-time profile and lets the two disagree". The decided `down_by8_transparent` also runs at any profile (lines 312-314), so the name and the run-time profile can disagree in the same way. The plan's rationale is inconsistent with its decision. The naming itself is the maintainer's choice, and the docstring states the tier, so this is a wording and test fix, not a defect in the design.

## Both plans (17 confirmed)

### dsp-3: The 0.57° phase budget belongs to the whole round trip but is assigned to each stage, and restated in the wrong units

**major** · IIR question 5; x4x8 6.3

> Decided (v7): the near-linear variant's passband phase error is at most 0.57°, the ×4 / ×8 / ×16 plan's round-trip condition, so a change cancels to −40 dB (0.034 high-rate samples at 2.25 kHz on 48 kHz).

**Problem.** The round trip's phase error is the sum of every IIR stage's error at that frequency, down and up. A half-band's decimator and interpolator have the same H(z), so the same phase error, which adds. An IIR ×8 is three half-bands each way. If each stage meets 0.57°, the last pair alone gives 1.15° (−34 dB), and six stages can give up to 3.4° (−24.5 dB). Separately, "0.034 high-rate samples" is a phase-delay error at the 48 kHz host. The stage that dominates is ×8's last half-band at 12 → 6 kHz, where 2.25 kHz is its own 3/8 edge and the same 0.707 µs is 0.0085 samples. A spike that reads 0.034 samples at the stage's own high rate would allow 2.3° (−28 dB). A bound on group-delay deviation also does not bound phase error: the phase error is the deviation's integral from DC.

**Evidence.** Residual 2 sin(φ/2): 0.573° gives −40.0 dB, 1.146° gives −34.0 dB, 3.44° gives −24.5 dB. Δt = 0.01 rad / (2π · 2250 Hz) = 0.707 µs, which is 0.034 samples at 48 kHz and 0.0085 samples at 12 kHz. Same-filter phase error: the down path is x → H(z) → ↓2 and the up path is ↑2 → H(z), so the in-band round trip is H(e^{jω})² at the stage's rate.

**Fix.** State the condition on the round trip: the summed |phase error| of every IIR stage in the down and up chains is at most 0.57° for f ≤ f_pass. Give IIR-0 a per-stage target in degrees over the stage's own passband, with the allocation stated (about 0.1° per stage at ×8 if split evenly), or have it measure the summed round trip. Drop the group-delay restatement, or give it as phase delay at a named rate.

**Skeptic.** The core point holds. Question 5 assigns 0.57° to 'the near-linear variant's passband phase error'. But x4x8 6.3 derives 0.57° for the whole round trip: residual 2 sin(φ/2), with φ the total phase error. A two-path stage uses the same H(z) decimating and interpolating, so the round trip's in-band response per half-band is H², and the phase errors add. One ×2 round trip at 0.57° per stage gives 1.15°, or −34 dB. Six stages at ×8 can give up to 3.44°, or −24.5 dB. That decided number sets the shipped section count, so a per-stage reading under-designs by at least 2×. Two sub-claims are weaker. (a) 'A bound on group-delay deviation does not bound phase error' is wrong: |τg − T| ≤ δ on [0, ω] gives |φ_err(ω)| ≤ δω, a conservative but valid bound. (b) The units are stated explicitly ('on 48 kHz'), so they are correct, though easy to misread at a stage whose own high rate is 12 kHz (0.0085 samples there).

### red-5: Sharing DspTap 0.3.0 makes the IIR stages and the round-trip scaffold wait for each other

**major** · X13 / IIR section 5

> In DspTap the scaffold and the IIR plan's kernels share 0.3.0.

**Problem.** Both plans decide that one DspTap minor carries IIR-a and X-c's scaffold. Two rules then force serial work:
- SampleRateTap moves submodules/dsptap only to a named tag, and raising TAP_SR_DSPTAP_MINIMUM updates the root README's tested pairs in the same PR (CLAUDE.md, A9).
- DspTap tags the commit that bumps its version.

So IIR-b, which needs IIR-a's kernels, waits for v0.3.0, and v0.3.0 waits for the scaffold. The scaffold comes after X-b, and its API is still a placeholder: `subband_delta`, its op interface, and T for chains that are not all-FIR (red-9). In the other direction, the TapTools round trip (X-c onward) waits for IIR-a, IIR-0's spike and I4's untried constexpr Chebyshev II. Two independent features become serial through one version number. IIR-b's PR list also leaves out the floor raise and the README tested-pairs update that A9 requires.

**Evidence.** - IIR_PLAN.md lines 496-508 (IIR-a "DspTap 0.3.0"; "IIR-a's kernels and guard share 0.3.0 with that plan's round-trip scaffold").
- X4_X8_PLAN.md lines 501-506 (X13) and 595-596 (X-c).
- /home/user/SampleRateTap/CLAUDE.md, "The DspTap floor (A9)"; /home/user/DspTap/CLAUDE.md, "Consumers & release flow".
- rational.h:49 sets TAP_SR_DSPTAP_MINIMUM to 0.2.0; DspTap is at v0.2.0 (084f2f6).

**Fix.** Give each feature its own DspTap minor: whichever is ready first is 0.3.0 and the other 0.4.0, as DspTap's own cadence allows. Add the TAP_SR_DSPTAP_MINIMUM raise and the README tested pairs to IIR-b.

**Skeptic.** Both plans decide that IIR-a and the X-c scaffold share DspTap 0.3.0 (IIR_PLAN line 508; X13). SampleRateTap's CLAUDE.md (A9/Q4) has submodule moves name a DspTap tag, so IIR-b cannot pin IIR-a's kernels until a 0.3.0 that also carries the scaffold. In the other direction, TapTools waits on IIR-a, which waits on IIR-0. IIR-b's PR list has no TAP_SR_DSPTAP_MINIMUM raise and no update to the root README's tested pairs, both of which A9 requires in the same PR. Whether this blocks is a schedule matter that one re-decision fixes, but as decided it couples the two features.

### red-6: The 0.57° phase target is a round-trip budget applied to one stage, and the plan alternates between phase and group delay

**major** · IIR Q5 / X4 plan 6.3

> 5. Decided (v7): the near-linear variant's passband phase error is at most 0.57°, the ×4 /

**Problem.** Section 6.3 derives 0.57° for the whole round trip: 2 sin(φ/2) = 0.01, or −40 dB. Q5 makes it the variant's own error, and IIR-0 uses it to set the section count. An IIR round trip at ×8 is 3 half-bands down and 3 up, and at ×16 it is 4 and 4. Up and down use the same allpass design, so their phase errors have the same sign and add. At 0.57° per stage the ×8 round trip can reach 3.4°, which cancels only to −24.5 dB, and ×16 can reach 4.6°, about −22 dB. Relaxed early stages see the band lower in their own passband, so the real sum is below this worst case; that is why the budget has to be stated per stage at its divisor.

The quantity also changes across the plan. Section 1 calls Q5 "its group-delay target" and section 4 pins "the near-linear variant's passband group-delay deviation", while Q5 decides a phase error. A group-delay bound converts to a phase bound only at a stated frequency: 6.3's 0.034 samples is taken at 2.25 kHz, the ×8 band edge at 48 kHz. The same 0.034 samples at a lone ↓2's 9 kHz edge is 2.3°.

**Evidence.** Worst-case sums: 6 × 0.57° = 3.42°, and 2 sin(1.71°) = 0.0597, −24.5 dB; 8 × 0.57° = 4.56°, and 2 sin(2.28°) = 0.0796, −22 dB. At 9 kHz: 2π · 9000 · 0.034 / 48000 = 0.040 rad = 2.30°. Plan text: IIR_PLAN.md line 61 ("its group-delay target (question 5)"), line 475 ("passband group-delay deviation as a pinned number"), lines 587-589 (Q5); X4_X8_PLAN.md lines 472-477.

**Fix.** State the target as the round trip's: at most 0.57° summed over the 2·log2(D) stages, across the chain's band. Derive each stage's share at its divisor, and pin phase error, not group-delay deviation, in section 4, both per stage and per round trip.

**Skeptic.** Section 6.3 derives 0.57° for the round trip's phase error φ. Q5 assigns it to 'the near-linear variant', which is a single ↑2/↓2 stage, and calls it the round-trip condition. A ×8 round trip has 6 such stages and ×16 has 8. Up and down use the same H, so their phase errors add. Arithmetic checked: 6×0.57° gives −24.5 dB and 8×0.57° gives −22.0 dB. 0.034 samples at 9 kHz/48 kHz is 2.30°. The quantity also drifts: line 61 and section 4 (line 475) pin a group-delay deviation, while Q5 decides a phase error.

### rules-1: Both plans serve rates that no engine's charter names (3, 6, 2.75625 and 5.5125 kHz), with no family-plan change

**major** · X 6.6 / 7 / 6.5; IIR 1

> **48 → 6, 48 → 3, 44.1 → 5.5125 and 44.1 → 2.75625 kHz work,** since the designs are

**Problem.** The root CLAUDE.md's D12 rule says no rate is served by an engine whose charter does not name it. The family's supported rates are exactly 14, the lowest 8 kHz. rational/PLAN.md lists 7.35 kHz (44.1/6, a ratio its charter accepts) as a non-goal because it is "not in the 14; adding a rate is a family-plan change". The x4/x8 plan ships rational/examples/low_band_fft.cpp converting 48 kHz to 3 kHz. It pins `rational.LowBand.*` at x8 and x16 from 48 kHz (6 and 3 kHz) and writes TapTools SCENARIOs at 48 → 6, 48 → 3 and 44.1 → 5.5125. It admits that none of these is a supported rate but proposes no family-plan change. The IIR plan's first consumers include 48 → 6 kHz as well.

**Evidence.** CLAUDE.md:28-30 ("no rate is served by an engine whose charter does not name it"); PLAN.md:102 ("Supported rates: exactly these 14"); rational/PLAN.md:1189-1190 (7.35 kHz non-goal); rational/CLAUDE.md charter: "The 48 kHz family (8 … 384 kHz)". X4_X8_PLAN.md:521-522 (SCENARIOs), :537 (demo 48 kHz → 3 kHz), :553-561 (ships the example and pins three tests), :575-576. IIR_PLAN.md:53 ("48 → 24 / 12 / 6").

**Fix.** Before X-b, record a family decision. Either add the low rates the round trip needs to PLAN.md 2.1, with their matrix rows, or restate the rule: rational serves ratios, and rates are documentary. If the second, amend CLAUDE.md's "no rate is served" sentence and rational's 7.35 kHz non-goal in the same PR. IIR section 1 then cites that decision or drops "/ 6".

**Skeptic.** Checked the root CLAUDE.md D12 sentence ("no rate is served by an engine whose charter does not name it"), PLAN.md:102 (exactly 14 rates, lowest 8 kHz), rational/CLAUDE.md's charter ("The 48 kHz family (8 … 384 kHz)") and rational/PLAN.md:1189-1190, which makes 7.35 kHz a non-goal because "adding a rate is a family-plan change". The x4/x8 plan ships rational/examples/low_band_fft.cpp at 48 → 3 kHz, pins rational.LowBand.* at ×16 from 48 kHz, and writes TapTools SCENARIOs at 48 → 6 / 3 and 44.1 → 5.5125. Its section 7 admits that none of these is a supported rate but records no family decision. IIR_PLAN.md:53 lists 48 → 6 among its first consumers. The ratio types are rate-free, so the code works; the gap is that a load-bearing family rule is contradicted with no recorded decision, and X-b and X14 depend on it.

### code-13: Both plans number their decisions from R18

**minor** · status / X-a

> On approval, sections 2–5 go to `rational/PLAN.md` as decisions R18 onward and a reopened decision 6.

**Problem.** rational/PLAN.md's last decision is R17. Both plans claim R18 as their first. The IIR plan says 'the accepted decisions become `rational/PLAN.md`'s R18 … and a family decision beside D18'. The ×4/×8/×16 plan lands first (IIR §5, X13), so the IIR plan's numbers would collide.

**Evidence.** `grep -oE '^\| R[0-9]+ ' rational/PLAN.md` gives R1 to R17; R17 is the channel-count decision (rational/PLAN.md:122).

**Fix.** Have the IIR plan say 'the next R numbers after the ×4/×8/×16 plan's' (and D19 onward), or fix the ranges now.

**Skeptic.** rational/PLAN.md's decisions end at R17, and root PLAN.md's at D18. X4_X8_PLAN.md:16 claims 'R18 onward', and IIR_PLAN.md:24 and :499 claim R18 too. v7 fixes the order with the ×4/×8/×16 plan landing first, so the IIR plan's numbers collide. Stale text.

### dsp-1: The ×8 4th-band advantage comes from the designer's fixed Kaiser β, not from FIR half-bands

**minor, auditor said major** · IIR 2.4; x4x8 3.1 ("Why decision 6 missed it") and X3

> The cause is FIR-specific: a relaxed FIR half-band stops getting cheaper at m = 7 taps per branch, while the 4th-band stage relaxes further.

**Problem.** Both plans say a relaxed FIR half-band saturates at m = 7, and X3 (a breaking 0.8.0 that renames the whole engine) rests on that 19 to 24 % ×8 gap. The cause is narrower. `design_nyquist` is called with β = kaiser_beta(A), but `search_nyquist_m` accepts a design only at −(A + 1) dB. A short windowed sinc with β tuned for 70 dB has sidelobes at about −68 to −70.6 dB, so it misses the 1 dB margin until its sidelobe pattern happens to fall right, at m = 7. The search is not even monotone in m. With one scalar changed (β for A + 2 or A + 3), three half-bands cost the same as ↓4 · ↓2 at ×8, and ×4 gets cheaper too. X3 may still hold, because under R3 a tie goes to fewer stages and ↓4 · ↓2 keeps its latency edge. But the stated cause and the size of the gain are artifacts of today's designer, and a lever that cuts every relaxed half-band in the matrix in the same 0.8.0 re-pin was never weighed.

**Evidence.** audit/dsp/pins.cpp reproduces the shipping pins exactly (search_nyquist_m, 16384-point grid). audit/dsp/hb.cpp, half-band at p = 3/16 (economy, divisor 2), β = kaiser_beta(70): worst stopband m4 −69.2, m5 −67.9, m6 −70.6, m7 −74.0 dB against the −71 dB acceptance; with β = kaiser_beta(74), m5 reaches −72.1 dB. The shipping pins themselves drop to m = 4 at divisors 6 and 8 and m = 3 at 12 and 16, so "stops getting cheaper at m = 7" is false past divisor 4. audit/dsp/betarule.cpp, economy, MACs per output: β(A+0): ×8 ↓4·↓2 85 vs ↓2·↓2·↓2 113, ×4 ↓2·↓2 53. β(A+2): 85 vs 85 (tie), ×4 49. β(A+3): 83 vs 83, ×4 47. β(A+4): 71 vs 79 (11 %), ×4 43. audit/dsp/betasearch.cpp (β searched per design): half-band m = 5 at divisor 2 and m = 4 at divisor 4. rational/PLAN.md's S1 ledger carries the same misattribution ("a relaxed half-band saturates at m = 7").

**Fix.** Restate the cause in IIR 2.4, x4x8 3.1 and the S1 ledger: the β is tied to A while the search demands A + 1 dB. Have X-0 run matrix.py with the designer's β as a variable (kaiser_beta(A + 2…4), or a β pinned per entry beside m) and report X3's gain under both designers before 0.8.0. Label IIR 2.4's "cheapest FIR chain" row as today's designer.

**Skeptic.** Reproduced with my own builds in audit/verify-dsp/ (pins.txt, hb.txt, betarule, betasearch.txt). pins.cpp gives exactly design.h's economy pins: B2 is 7/7/7 at divisors 2/3/4, then 4, 4, 3, 3 at 6/8/12/16. hb: at p = 3/16 with beta(70), m4 −69.2, m5 −67.9, m6 −70.6, m7 −74.0 dB against the −71 dB acceptance, so the search is non-monotone in m. beta(74) gives m5 −72.1. betarule gives ×8 85 vs 113 at beta(A), 85 vs 85 at beta(A+2), 83 vs 83 at beta(A+3) and 71 vs 79 at beta(A+4); ×4 is 53/49/47/43. So the saturation at m = 7 comes from beta tied to A while the search demands A + 1, not from FIR half-bands as such. The IIR plan's 'the cause is FIR-specific' is misattributed, and 'stops at m = 7' fails past divisor 4. Downgraded because the headline overstates. With beta searched per design (my betasearch run: B2 m5 at divisor 2, m4 at divisor 4, m10 at divisor 1; B4 m4 at divisor 2), ↓4·↓2 is still 71 against 79, about 10 % cheaper. Ties go to ↓4·↓2 under R3's fewer-stages rule. So X3's chain choice holds under every designer variant tried. The plan's 19 % is accurate for the shipping designer. rational/PLAN.md 2.1 already names 'the Kaiser fit's floor'. Changing beta would re-pin every FIR design in the family, a much larger change than X3. Fix the wording; the decision does not change.

### dsp-7: The four-path bar names no stopband, and "under two per path" contradicts "fewer than six"

**minor** · IIR 2.4 ("What a four-path stage has to beat") and I12; x4x8 X5's ↓4 · ↓4

> So it must reach 70 dB with **fewer than six sections across its four paths**, under two per path.

**Problem.** The two half-bands being replaced only reject the bands that fold onto the passband; across the whole stopband from 13/64 they reach −42 dB, not 70. A spike that asks the four-path stage for 70 dB over that whole range compares unlike specs and will reject it at any count. Also, "under two per path" allows 7 sections (1.75 per path), which cost 14 MACs, more than the pair's 12; the bar is at most 5 sections, fewer than 1.5 per path. Measured on the right spec, the plan's expectation holds.

**Evidence.** audit/dsp/fp4.py and fp5.py, ×8 economy, the four-path ↓4 from 8 r_min to 2 r_min with f_pass = 3/64 of the high rate. The N5 · N5 cascade (closed-form coefficients) reaches −76.0 dB on [13/64, 19/64] ∪ [29/64, 1/2] but only −42.4 dB on [13/64, 1/2]. Best four-path designs found by SLSQP minimax on the multiband spec: 4 sections −51.7 dB, 5 sections −60.3 dB (layout [2,1,1,1]), 6 sections −84.2 dB ([2,2,1,1]; 12 MACs, a tie), 7 sections −87.3 dB.

**Fix.** Give the four-path stage's stopband as the bands that fold onto the passband ([2r − f_pass, 2r + f_pass] ∪ [4r − f_pass, 4r] at the 8 r_min rate, r = r_min), and write the bar as "at most 5 sections in total". Record the 6-section tie as part of IIR-0's result.

**Skeptic.** Arithmetic: 'fewer than six sections' over four paths is under 1.5 per path, not 'under two'. Seven sections (1.75 per path) would cost 14 MACs, above 12. I checked the stopband claim independently (audit/verify-dsp/fp_v.py): economy ×8's top two N5 half-bands (closed form, 0.1094/0.5351 and 0.1221/0.5581) cascade to −76.0 dB on [13/64, 19/64] and −107 dB on [29/64, 1/2], but only −42.4 dB over the whole [13/64, 1/2]. The FIR 4th-band stage is designed over the whole band (nyquist_worst_stopband_db sweeps from (1−p)/L to 1/2). So 2.4/I12 must name the multiband stopband, or the comparison is unlike-for-like. I did not re-run the SLSQP four-path designs; they are not needed for the finding.

### dsp-8: The round trip does not need a guard band filter, so it is no reason for the linear-phase form

**minor, auditor said major** · IIR I7 (the linear-phase form); x4x8 6.3

> That costs the FIR stages and the near-linear variant their one property, and the ×4 / ×8 / ×16 plan's round trip needs linear phase (its 6.3).

**Problem.** In 6.3 the change Δ is masked to the usable bins (at or below p · r_low, with a taper), so it carries nothing in the guard band, and the full-band path carries the guard band untouched. A guard band filter in the round trip removes nothing the mask has not already removed. It only adds passband ripple and delay. A type II FIR at ×16's 3 kHz low rate adds more delay than the stages' entire round trip. One of the decided linear-phase form's stated reasons therefore does not hold.

**Evidence.** x4x8 6.3: "Δ is the op's change (result minus input), masked to the usable bins with a raised-cosine taper of W bins"; 6.4 decides 75 % of the low band. IIR I7: the 38-tap type II FIR delays 18.5 low-rate samples, which is 18.5 × 16 = 296 full-rate samples at ×16 per placement (592 placed both after the decimator and before the interpolator). 6.6 measures the stages' whole round trip at 414 samples.

**Fix.** Drop the round trip as a reason for the linear-phase guard band form; justify it on its other uses or defer it. Say in x4x8 6.3 that the round trip uses no guard band filter, because the mask does that job.

**Skeptic.** 6.3 masks Δ to the usable bins (≤ p·r_low with a taper). Guard-band content therefore never enters Δ: the full-band path carries it, and up(Δ) images sit at the stopband. A guard band filter in the round trip removes nothing the mask has not already removed. Hann leakage from strong aliases near Nyquist into bins 0.125 r_low away (about 512 bins at 4096 points) is negligible. So 'the round trip needs linear phase' is not a reason for a linear-phase guard band filter. The delay figures check: (38 − 1)/2 = 18.5 low-rate samples is 296 full-rate samples at ×16 per placement, and 592 for two placements, against 414. That matters little next to a 65536-sample frame. Downgraded: the decided linear form has another stated justification (the partner of the FIR stages and the near-linear variant), so only a reason needs removing.

### red-14: Status lines claim everything is decided while several items still read only "Recommended"

**minor** · Status lines / section 3 / section 4

> the decisions are listed in section 3 with a recommendation each, and the estimates in

**Problem.** The IIR status says only the stages' names and the spike's constants remain open. Yet I1, I3, I6, I8, I10 and I11 read *Recommended*, with no decision and no question in section 8. Meanwhile decided items still carry the *Recommended* label: I5 (decided by Q3), I9 (Q6 and Q8) and I12 (Q7). The x4 plan builds on IIR's I1 as if settled (X13's shared 0.3.0).

In the x4 plan, the status says only the TapTools object's design and the IIR names are open, but X2, X5 and X7 still read *Recommended*. X2 and X7 are also stale against X4 (red-15). X-0 still "settles X3 and X6", both already decided or moot, and section 6 "goes to whichever repository X10 picks", although X10 picked DspTap and 6.6 lands in rational. A reviewer cannot tell what was approved, and two of the unflagged items are wrong (red-8, red-10).

**Evidence.** IIR_PLAN.md lines 15-23, 227, 234, 242, 283, 291, 329, 339, 362, 418, 423 and 455-462. X4_X8_PLAN.md lines 3-5, 17, 267, 340, 356 and 581-583.

**Fix.** Mark each item Decided with its version, or list it as open in section 8 or 10. Remove the stale status sentence from v1.

**Skeptic.** In the IIR plan, I5, I9 and I12 still read *Recommended* though Q3, Q6/Q8 and Q7 decided them. I1, I3, I6, I8, I10 and I11 are neither decided nor listed as open. In the x4 plan, X2, X5 and X7 read *Recommended*. X-0 still 'settles X3 and X6' though X3 is decided and X6 moot. The header still says 'Section 6 goes to whichever repository X10 picks' though X10 picked DspTap and 6.6 lands in rational. The IIR status does say 'a recommendation each', so this is stale labelling rather than a false claim.

### red-23: Both plans claim rational/PLAN.md's R18

**minor** · IIR status / x4 plan header

> moves to `docs/IIR_PLAN.md`, and the accepted decisions become `rational/PLAN.md`'s R18 …

**Problem.** The x4 plan's header says "sections 2–5 go to `rational/PLAN.md` as decisions R18 onward", and rational/PLAN.md currently ends at R17. Since X-a lands first by both plans' decision, R18 will be taken. IIR-b's PR list ("… and R18") then points at the wrong decision.

**Evidence.** X4_X8_PLAN.md line 16; IIR_PLAN.md lines 24 and 498-499; rational/PLAN.md:122 (R17 is the last).

**Fix.** Number the IIR decisions after the x4 plan's, or refer to them as "the next free R numbers after X-a's".

**Skeptic.** rational/PLAN.md ends at R17 (line 122). X4_X8_PLAN line 16 claims 'R18 onward' and IIR_PLAN line 24 claims 'R18 …'. X-a lands first by both plans' order, so IIR-b's 'and R18' would collide. This is a trivial renumbering.

### red-7: The −40 dB target behind Q5 is 20 dB looser than the cancellation this plan measures and pins

**minor, auditor said major** · X4 plan 6.3 / 6.6 / IIR Q5

> −40 dB needs φ ≤ 0.57°

**Problem.** The x4 plan measures and pins the FIR round trip far below −40 dB:
- 6.3 says removing a component leaves the passband ripple, −60 to −92 dB at economy;
- 6.6 measured −64.05 dB;
- `rational.LowBand.RemovingOneToneLeavesTheOthers` pins "at least 60 dB down", and TapTools pins `FullCancellationReachesTheRippleFloor`.

Reaching 60 dB needs φ ≤ 0.057°, ten times tighter than 0.57°. A near-linear IIR chain built to Q5 and plugged into the scaffold, as X13 plans, would fail those tests. The first consumer, X14's notch for hum harmonics and room modes, would lose about 24 dB of notch depth on switching. Neither plan says −40 dB is a deliberately lower bar for IIR.

**Evidence.** 2 sin(φ/2) = 10⁻³ gives φ = 0.0573°. Plan text: X4_X8_PLAN.md line 470 (−60 to −92 dB), line 551 (−64.05 dB), lines 559-560 (the 60 dB test), lines 507-510 (X14); IIR_PLAN.md lines 587-589.

**Fix.** Set the bar from the consumer's need. Either require about 60 dB for the near-linear variant (φ ≤ 0.057° over the round trip, re-sizing its sections), or keep 40 dB and state that IIR round trips carry a lower pinned floor, with test thresholds per stage kind in 6.5 and 6.6.

**Skeptic.** The numbers hold: 6.3 measures −60 to −92 dB, 6.6 measures −64.05 dB, and `RemovingOneToneLeavesTheOthers` pins ≥60 dB, which needs φ ≤ 0.0573°. X13 plugs IIR into the scaffold 'under 6.3's phase condition' (−40 dB), and neither plan says the IIR floor is deliberately 20+ dB lower or gives per-kind test thresholds. Downgraded: −40 dB is the maintainer's decided target and FIR stays the default, so the bar is a choice. The problem is that it goes unstated.

### red-9: The scaffold needs an exact integer T, but the IIR latency contract gives chains a double

**minor, auditor said major** · X10 / X13 vs IIR I5

> plug into the scaffold later through its type parameter, under 6.3's phase condition,

**Problem.** X10 decides that the scaffold holds the delay line with "T from the stages' exact latencies", X12 says `latency()` returns T as an integer, and 6.3's cancellation needs T exact. The IIR plan's decided contract (I5, Q3) is that a chain with an IIR stage reports a double, and its exact latency does not compile. A scaffold over sync_stage built as X10 says therefore cannot be instantiated with IIR chains. X13's "plug in later through its type parameter" needs an API for an integer delay that neither plan specifies, such as the near-linear variant's delay-path length. Because the scaffold ships in DspTap 0.3.0, adding that concept later is a breaking DspTap change.

**Evidence.** X4_X8_PLAN.md lines 486-490 (X10), 498 (X12) and 501-506 (X13); IIR_PLAN.md lines 283-289 and 577-584; DspTap chain.h:251-257 (an exact_ratio only).

**Fix.** Decide in X10 how the scaffold gets T from a chain without an exact latency. For example, a stage trait giving the near-linear variant's integer delay path, accepted for near_linear and rejected at compile time for nonlinear. Have IIR's I5 provide that trait.

**Skeptic.** X10 takes T 'from the stages' exact latencies' and X12 returns T as an integer. I5 and Q3 make IIR chains report a double, with the exact latency not compiling. So X13's 'plug in later through its type parameter' has no specified path to an integer T, such as a delay-path trait. Downgraded: the claim that adding one later breaks DspTap is overstated, since an extra accepted trait is additive. This is a seam to specify, not a defect.

### rules-10: DspTap 0.3.0 is "shared", but neither plan orders IIR-a and X-c or says which PR is tagged

**minor, auditor said major** · IIR 5; X13, 8

> DspTap, IIR-a's kernels and guard share 0.3.0 with that plan's round-trip scaffold.

**Problem.** IIR-a is the IIR plan's second PR (DspTap 0.3.0); X-c (the scaffold) comes after X-b. Neither plan says:
- which of the two lands first;
- which PR bumps version.h and project() and takes the v0.3.0 tag;
- whether IIR-b waits for X-c.
DspTap tags the commit that carries its version, and a SampleRateTap commit that moves submodules/dsptap names the tag it pins. Each option fails a statement:
- if IIR-a is tagged v0.3.0, the scaffold becomes 0.4.0 and is not "shared";
- if the tag waits for the scaffold, IIR-b is blocked behind X-c, a dependency neither plan lists;
- pinning an untagged DspTap breaks the root rule.

**Evidence.** IIR_PLAN.md:496-497, :504-508; X4_X8_PLAN.md:501-504, :595-596; CLAUDE.md DspTap-floor bullet ("a commit that moves `submodules/dsptap` names the tag it pins"); DspTap CLAUDE.md ("bump both together, and tag the commit that carries them `vX.Y.Z`").

**Fix.** State one order in both plans. For example: in DspTap, IIR-a then X-c, the second bumping to 0.3.0 and tagged, with IIR-b after the tag. Or give each its own minor version (0.3.0 for the first, 0.4.0 for the second) and say which tag each consumer pins.

**Skeptic.** Both plans say IIR-a's kernels and X-c's scaffold "share 0.3.0" (IIR:508, X13). X-c comes after X-b, and the DspTap order of IIR-a against X-c is never stated. SampleRateTap must pin a DspTap tag (the root CLAUDE.md's A9 bullet), so IIR-b cannot move the pin until 0.3.0 is tagged. If the tag waits for the scaffold, IIR-b depends on X-c, a dependency neither plan lists. Downgraded to minor: it is an unstated ordering, fixed with one sentence.

### rules-11: Versions, the DspTap floor and the tested pairs are unplanned, and I11 may break an exported symbol

**minor, auditor said major** · IIR 5, I11; X0/X3, 8

> the existing `create_stage_format` shape with a kind argument, or new entry points, the

**Problem.** 1. The IIR plan names no SampleRateTap version for the release it decided (both variants and the guard band filter together), nor which PR bumps the three umbrella headers and the root project() (D13).
2. IIR-b's headers include DspTap 0.3.0's kernels and guard, so TAP_SR_DSPTAP_MINIMUM must rise from 0.2.0 to 0.3.0 in all three umbrella headers, with a tested-pairs row in the root README in the same PR (A9). The plan does not mention it. Without it the floor accepts DspTap 0.2.0 and consumers get missing-header errors, including TapTools under X11, which relies on that floor.
3. I11's kind argument on the exported `tap_sr_rational_create_stage_format` changes an existing symbol's signature. That is an ABI break after the 0.8.0 that X0 justified as "so consumers break once".
4. In the x4/x8 plan, X-a is 0.8.0, X-b's additive aliases have no version, and the 0.8.0 tested-pairs row is not mentioned.

**Evidence.** rational/include/tap/sr/rational/rational.h:49-50 (floor 0.2.0); README.md:199-209 (tested pairs); capi.h:127; tests/family/symbols_rational.txt; IIR_PLAN.md:59-62, :487-503 (no version); X4_X8_PLAN.md:237, :289, :592-594.

**Fix.** IIR plan:
- name the release (0.9.0 after 0.8.0) and bump in the PR that completes it (IIR-c or IIR-d);
- raise TAP_SR_DSPTAP_MINIMUM to 0.3.0 in IIR-b, with the tested-pairs row and the pinned tag named;
- choose new entry points so no existing symbol changes.
x4/x8 plan: tag 0.8.0 after X-b and add its tested-pairs row.

**Skeptic.** The IIR plan names no SampleRateTap version and no PR that bumps the three umbrella headers and project() (D13). It never mentions TAP_SR_DSPTAP_MINIMUM, which is 0.2.0 in rational.h:49 and must rise to 0.3.0 when IIR-b includes 0.3.0 headers, with a tested-pairs row (A9). The README's table has a row per release (0.6.0, 0.7.0), and X-a's 0.8.0 row is not mentioned either. I11's kind argument is one of two options ("or new entry points"), and a 0.x minor release may break, so that point is weak. Downgraded to minor: release bookkeeping.

### rules-12: I5's double latency needs DspTap chain code IIR-a omits, and IIR stages cannot meet X10's exact-latency scaffold

**minor, auditor said major** · I5, I10, I11; X10, X13

> The DspTap `chain` concept needs no change for that (it asks latency only of chains

**Problem.** Mixed chains are `tap::dsp::chain`s (I10), since rational's basic_chain takes ratios only. DspTap's chain computes latency only as an exact_ratio and static_asserts that every stage reports one. So "a mixed chain reports a double" (question 3, decided) needs new DspTap chain code: a nominal double latency and the group delay at a frequency. IIR-a's scope (I1) does not include it, so the substrate-first order is skipped. The C ABI's `tap_sr_rational_latency_output_frames(c, num, den)` cannot carry a double either. Meanwhile X13 says the IIR stages "plug into the scaffold later through its type parameter", while X10's scaffold takes "T from the stages' exact latencies". An IIR stage reports a double, so it cannot plug in.

**Evidence.** DspTap include/tap/dsp/chain.h:134-137 (`same_as<exact_ratio>`), :150-158 (static_assert "a chain's latency needs every stage to report…"), :251-257; rational/include/tap/sr/rational/chain.h:180-185 (basic_chain over rational_ratio); capi.h:166; IIR_PLAN.md:283-289, :418-420; X4_X8_PLAN.md:486-489, :505-506.

**Fix.** Add to IIR-a, with its DspTap test, the chain change: a nominal double latency and group_delay_output_frames(f) composed over stages, with exact latency still gated to all-exact chains. Specify the C ABI's latency for IIR handles in I11. In X10/X13, state what an IIR stage must expose to enter the scaffold: an exact integer delay path for the near-linear variant, with the nonlinear variant refused by a static_assert.

**Skeptic.** Confirmed only in narrowed form. DspTap's chain::latency_output_frames() requires exact_ratio, through a static_assert that fires only when the function is called, so I5's "the concept needs no change" is accurate. tap::dsp::chain exposes stage<I>() and each stage's k_up/k_down, so a double latency could be composed in rational without DspTap code. The claim that IIR-a must add chain code is therefore not established. What stands: I5 decides that "a mixed chain reports a double" but names no home (mixed chains are tap::dsp::chain per I10, and latency_seconds() also goes through the exact path), and I11 does not say what tap_sr_rational_latency_output_frames(num, den) returns for an IIR handle. X13's scaffold point is deferred ("later"), and the near-linear variant's delay-path length is exact by I5, so that half does not hold.

### rules-17: New DspTap primitives skip DspTap's discipline and its adding-a-primitive checklist

**minor, auditor said major** · X9, X10, 6.5; IIR I1, IIR-a

> - **X9. Formats and channels:** double first (TapTools and `stft.h` are double), float later.

**Problem.** X10 makes the scaffold a DspTap primitive. DspTap's load-bearing discipline says every primitive follows double as the golden model and float32 as the embedded profile: a `basic_<name><Sample>` template with aliases, and a typed float/double battery that pins cross-precision agreement. "Float later" contradicts it. Both plans also skip DspTap's checklist for a new primitive:
- a README section and the primitive count in the intro (CLAUDE.md: "Seven primitives today");
- each profile's numbers stated;
- the on-target leg that runs its battery, and whether the MAIN_FILTER excludes it.
IIR-a's entry is only "the design math and kernels (I1), their battery and fingerprints", and 6.5's Subband tests say nothing of typing.

**Evidence.** DspTap CLAUDE.md: the design discipline ("Double is the golden model; float32 is the embedded profile … Cross-precision agreement is pinned by tests") and Adding a primitive, items 1-4; X4_X8_PLAN.md:482, :486-490, :514-520; IIR_PLAN.md:227-233, :496-497.

**Fix.** Land the X10 scaffold typed over float and double, with cross-precision agreement (its sample type comes from the stages), and apply X9's "double first" to TapTools only. For IIR-a and X-c each, add the README section, the primitive count and CLAUDE.md's list, the per-profile numbers (double, float, Q15 and Q31 for the IIR kernels) and the QEMU legs that run them.

**Skeptic.** DspTap CLAUDE.md's checklist (README section, primitive count "Seven primitives today", per-profile numbers, the on-target leg and MAIN_FILTER) appears in neither IIR-a nor X-c. X9's "double first … float later" sits against DspTap's double/float typed-battery discipline if it applies to the DspTap scaffold. The scaffold is generic over sync_stage (like chain<>, whose battery is TYPED_TEST), so typing its battery costs little. Downgraded to minor: bookkeeping and one scoping sentence.

### rules-20: Both plans claim rational/PLAN.md's R18

**minor** · IIR status; X status

> On approval, sections 2–5 go to `rational/PLAN.md` as decisions R18 onward and a reopened

**Problem.** R17 is rational/PLAN.md's last decision, and both plans number theirs from R18. The x4/x8 plan lands first (X13, IIR section 5), so the IIR plan's numbers will be wrong. The IIR plan, rational-only stages plus DspTap, also moves to the root docs/, while the x4/x8 plan goes to rational/PLAN.md, and D6 keeps each engine's plan in its own directory.

**Evidence.** rational/PLAN.md:122 (R17); IIR_PLAN.md:24-25 ("moves to `docs/IIR_PLAN.md`, and the accepted decisions become `rational/PLAN.md`'s R18 …"); X4_X8_PLAN.md:16; PLAN.md:41 (D6).

**Fix.** In the IIR plan, number the decisions after the x4/x8 plan's. Place the plan under rational/, or say why docs/.

**Skeptic.** rational/PLAN.md:122 shows R17 as the last decision. Both plans claim "R18 …" (IIR:24, X4_X8:16), and the x4/x8 plan lands first, so the IIR plan's numbers will collide. The docs/ placement is defensible (docs/CHANNELS_PLAN.md is precedent for a plan that also carries a family decision), so only the numbering is a defect.

## Gaps no lens covered

### Precision of the float profile inside the recursions (I1, I8, I9, section 6) (iir)

I1 says the kernels follow `sample_traits`. But `sample_traits<float>` is float I/O with double accumulation, and rational's README:401 says the float datapath accumulates in double. On M33 and M4F that is soft double, about 19x Q15's instruction count by sample_traits.h's own comment. bridge/PLAN.md §7 calls the MCU-float accumulation contract "a DspTap decision". DspTap's rule also runs fragile recursions in double even in the float profile. The plan never says whether the float states and products are float or double. That choice decides the M33/M4F cost, whether the FTZ guard affects Cortex-M float at all, and whether float can reach 120 dB. Separately, I8's Q31 coefficients for Q15 differ from sample_traits<int16_t>'s Q1.14, so the traits I1 cites cannot be used as they stand.

**Check.** In I1/I8, state the float profile's state and multiply type for the allpass and biquad kernels as a DspTap decision. Count one section in each form on M33 and M4F. Redo I9 and the 120 dB risk for the type chosen.

### Composing bands: each band has a different T (6.3, X12, Subband.BandsComposeByAddition) (x4x8)

6.3 writes x[n − T] + up₈(Δ₈) + up₄(Δ₄) with one T. From the plan's own numbers (economy, N = 4096), T₄ = 110 + 4·4096 = 16494 and T₈ = 206 + 8·4096 = 32974. So up₄(Δ₄) needs 16480 more samples of delay, 4120 frames at 12 kHz. X12's memory figure and the scaffold's interface leave that delay out, and the test as named would pass with any alignment the scaffold picks.

**Check.** State the alignment rule: every band delayed to the largest T, with each extra delay an integer and a multiple of D_b. Size its memory in X12. Make BandsComposeByAddition use bands whose T differ.

### Where the mask lives: the scaffold is generic over Op, but masking is in TapTools (X10, 6.3, 6.5) (x4x8)

Two of 6.3's claims depend on Δ being limited to bins up to p·r_low: aliasing in untouched bins is never re-injected, and processed bins are alias-free. X10 puts that masking and tapering in TapTools' stft.h change mode, not in the DspTap primitive. So DspTap's battery cannot pin the claims. An op that does not go through stft.h has no mask at all, for example a time-domain op or 6.5's pitch-dependent process. Its Δ then covers the transition band, and up() re-injects it.

**Check.** Decide whether the scaffold enforces the usable band (for example a low-rate filter on Δ) or states it as a precondition on Op. Add a DspTap test where an unmasked op touches the transition band, and pin the result.

### Clean-room provenance of the designer against DspTap's precedent (section 7, I4, IIR-a) (iir)

The plan says harris's tony_des2.m and tonycxx.m (copyright, no license) were consulted, and v4 quotes their header. DspTap's NOTICE.md (#39, #42) shows how the maintainer handles third-party code near a new implementation: a recorded clean-room procedure, an implementer barred from the source, and an access statement. The plan gives none of these. It does not say who read the scripts, whether IIR-a's implementer may, or where the constexpr prototype's nome-series form came from. No lens examined this.

**Check.** Record who has read the scripts. Bar IIR-a's implementer from them, or record the maintainer's judgement as #42 did. Add an access statement to IIR-a's docs/NOTICE.md. Trace the prototype's formulas to Valenzuela–Constantinides, Gazsi or Vaidyanathan, not to the script.

### The evidence behind "measured" has no durable home (both)

Both plans cite scratch programs in the session scratchpad: x4x8/*, examples/* including the ÷16 demo, ftz/* including the constexpr prototype and the FTZ benchmark. Scratchpad files can be cleared at any time. On approval the numbers move into rational/PLAN.md and docs/, whose convention is measured numbers with their provenance. No PR commits or regenerates these programs, so the decisions taken on them (X3, X4, I4, I9) cannot be re-checked after this session.

**Check.** Have X-0 and IIR-0 commit the programs (for example under rational/tools or docs/) or regenerate each cited number. Re-cite every "measured" against the committed path.

### Placement on docs/ROADMAP.md and the 1.0 freeze (both)

ROADMAP §8 defers "the IIR pre-filter" on purpose until a consumer pulls it (bridge/PLAN.md §7). The IIR plan names no consumer: its "first consumers" are ratios, not projects. Blocks B and C are still open: the hardware campaign, and C3's 1.0 freeze with its CHANGELOG, the D7-versus-semver statement and the P14 configuration. Neither plan says whether 0.8.0's rename and the IIR surface land before the freeze, whether the freeze covers them, or what they push back. None of the four lenses read ROADMAP.md.

**Check.** Add both plans' PRs to ROADMAP.md in a block. Name the consumer that pulls the IIR stages, or record that §8's deferral is lifted. State whether C3's freeze includes the new stages and aliases. Record 0.8.0's old-to-new names where C3's CHANGELOG will be.

### Compile-time design on the CI compilers that were not measured (I4) (iir)

"The same bits on every compiler" was shown only with GCC 13 and Clang 18 on Linux. CI's Windows MSVC leg builds every engine with /W4 /WX, and MSVC's default /constexpr:steps limit is about a tenth of Clang's. Measured here (audit/completeness/cxhb2.cpp), the prototype needs about 15.8k Clang steps for one N9 design and about 30.5k for N9 plus N17. The Chebyshev II, with series sinh/asinh and pole pairing, is not prototyped at all. Bit identity under MSVC's constant evaluator is untested.

**Check.** In IIR-0, compile the half-band and a Chebyshev II prototype under MSVC (and AppleClang). Record the step margin at the largest named order, and compare the coefficient bits across all four compilers.

### Whether the near-linear variant is feasible, and what sets its size (Q5, I3, I4) (iir)

It is on the first release's critical path, yet no near-linear design exists. No optimization method or paper is chosen, the generator's dependencies are not named, and no section count is estimated at 70 or 120 dB or in fixed point. Q5 also says the 0.57° target "sets the section count". For a two-path filter, though, a 70 dB stopband already holds the paths' phase difference to about 2·10^(−70/20) ≈ 6.3·10⁻⁴ rad. The passband phase error is half the paths' difference, so under equal weighting it is about 0.018°. The target binds only if the design weights the passband down.

**Check.** Before IIR-b, have IIR-0 produce one near-linear design per 70 dB profile with a named, published method. Report its stopband, passband phase and group-delay error, and its section count. Say which constraint is active, and whether 120 dB and Q15 are reachable.

### Fixed-point recursions: cost and zero-input limit cycles (I8) (iir)

"Every format in the first release" puts Q31 with 64-bit states on the critical path. On M33 each section is a 64×32 multiply, and no table estimates its cost. The claim that the limit-cycle test decays to exact zero rests on a 1969 citation alone. No lens checked it for the one-multiplier allpass, or for the guard filter's biquads, whose state format I8 does not state. Under rounding, a pole near the unit circle has a dead band of several LSBs. Only the float subnormal case was examined.

**Check.** In IIR-0, run an exhaustive or randomized zero-input limit-cycle search over the stored-state formats, with the chosen rounding, for each section kind. State the biquads' state format. Count Q15 and Q31 sections on M33, M55 and Hexagon.

### The FTZ guard on Hexagon, a gated consumer target (I9) (iir)

I9 is decided (v4) while Hexagon's flush-to-zero control is left as "IIR-0's to check". Hexagon is in CI and is one of the three targets A12/P14 freezes for a current consumer. If Hexagon has no such bit, the fallback is the per-section flush the plan measured at 18–50 %. That cost would then sit on a P14 target, unmeasured, under a decided design.

**Check.** Before IIR-b, have IIR-0 establish Hexagon's FP control (USR) and qemu-hexagon's fidelity to it. Count the fallback's cost on the Hexagon ratchet target, and reopen I9 if it is large.

### X3 was decided on MACs alone (x4x8)

The ×8 advantage behind X3 (19 % down, 15 % up) is a MAC count from an x86 scratch program. The IIR plan itself says MACs are not instructions, and the family gates on instruction counts on M33, M55 and Hexagon. No instruction count of ↓4 · ↓2 against today's chains or three half-bands exists on any target. X-0 only re-runs the generator's MAC arithmetic, so a Helium or HVX outcome that erases the gain would surface only after the breaking 0.8.0.

**Check.** In X-0, take a scratch icount of ↓4 · ↓2 / ↑2 · ↑4 against ↓3 · 3/4 · ↓2 and ↓2 · ↓2 · ↓2, in float and Q15, on the three ratchet targets. Record it beside the MAC figures before X-a.

### The spikes have no exit criteria (both)

X-0 "settles X3 and X6", but X3 is already decided. IIR-0 "settles I3, I4, I7 and I12", but I4 and I7 are decided. Neither plan says which spike result would stop or reshape the next PR. Examples: X-0's diff moving far more rows or chains than expected, the near-linear variant failing its target, float failing 120 dB, Q15 limit cycles, Hexagon lacking FTZ, or the constexpr design failing under MSVC. Without criteria, a spike can only confirm.

**Check.** Give each spike a short go/no-go list. For each item, name the numeric result and the decision it reopens (X3, X4, I2's variant set, I4, I9), so the maintainer approves a conditional plan.

## Refuted or uncertain

### rules-8 (refuted): X-a bundles three decisions in one PR, losing the pure-rename gate

**Skeptic.** The cited rules are about commits within a PR (migration Step 3, "one commit per class, each gated") and about optimization levers ("one lever per change"). Neither requires separate PRs. X-a can carry the rename as its own commit, gated identical (hashes, rows, ABI values, ratchet), before the X3/X4 commits, so the cheap gate is not lost. Splitting the PR is a process preference. At most the plan could say that X0's rename is its own gated commit.

### rules-16 (refuted): Unlicensed design scripts are listed as a source, with no clean-room line for their readers

**Skeptic.** The plan already confines the scripts: "consulted and never ported", "nothing of theirs enters the tree", the designer "written from the method their own header cites … and the papers here", and "run outside the tree by the maintainer, with only the comparison recorded". So the finding's "without saying by whom" is wrong. The #39 clean-room procedure was a remedy for code already derived from Ooura, not a standing rule for every reference consulted. The plan itself flags the Lyons citation as "to be checked against the book". What remains is cosmetic: the item sits under a heading titled "published literature only".

### rules-21 (uncertain): If X-0 merges a generator that admits band 4, the generator no longer produces the committed matrix

**Skeptic.** No CI job runs matrix.py (no hit in .github/), and NO_SINGLE_STAGE = {4} is at matrix.py:72. If X-0 committed matrix.py with band 4 admitted by default, the generator would no longer reproduce the committed rows, against "change the matrix only through the generator". But X-0 says "Nothing ships", and whether its generator change merges at all is unstated. The IIR-0 analogy suggests spikes commit, but the problem is conditional, so I cannot confirm it.

### rules-22 (refuted): rational pins the round trip's contract before its DspTap home exists

**Skeptic.** The substrate discipline covers shared library code. The example's hand-written delay and sum is example code in rational/examples, not substrate, and the plan says the scaffold will replace it (6.6, last paragraph). Pinning an example's printed claims in tests and rewriting them later is a cost and a sequencing preference, not a rule break. The rate question in those tests is rules-1.

### rules-23 (refuted): The demonstration notebook would be committed on scipy designs until IIR-d

**Skeptic.** rational/notebooks/design_spike.ipynb is a committed numpy/scipy notebook that labels itself the independent leg. That is precedent for a spike notebook not driven through the C ABI. The plan says IIR-0's version "runs scipy designs and says so", and its shipping version goes through the C ABI. Folding it into iir_spike.ipynb is a reasonable tidy-up, not a rule violation.

### rules-26 (refuted): `guard_band_filter<S, Ch>` breaks the family's basic_ template and count-first alias shape

**Skeptic.** D18 requires the count "second, after the sample format" on engine types, and `guard_band_filter<S, Ch, P>` satisfies that. Sample-generic templates without a basic_ prefix and with S first already exist in rational: every named chain (`template <sample_type S, size_t Ch> using up_2_up_2 = basic_chain<S, Ch, …>`) and the x4/x8 plan's own `down_by8<S, Ch>`. The name is the maintainer's choice, and I2 leaves the final names to IIR-b. Matter of naming taste.

### red-25 (refuted): IIR-b packs the whole stage design into one PR

**Skeptic.** This is a matter of PR granularity. The decided scope requires both variants and all four formats in one release, and section 5 making that one PR is internally consistent. The latency and flush problems it cites stand on their own (red-8, red-10).
