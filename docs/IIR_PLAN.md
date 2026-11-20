# Recursive polyphase stages for `rational` (the IIR options): plan

Status: **draft v8, 2026-11-20, for the maintainer's review.** v8 applies the adversarial
audit of v7 (four lenses, each finding checked by a skeptic; the report is
`docs/PLANS_AUDIT_2026-11.md`) and the maintainer's answers to it: rates are named by
ratio (family decision D19, from the ×4 / ×8 / ×16 plan), and one DspTap 0.3.0 carries
this plan's substrate and that plan's round-trip scaffold together (section 5).

What v8 changes, in short:
- **Flush keeps DspTap's contract** (I6): the window ends where the state is exactly zero,
  not where the tail drops below the stopband.
- **The phase target is the round trip's** (question 5): 0.57° summed over every IIR stage
  down and up, pinned as phase, per stage and per round trip.
- **Fixed point gets a headroom rule** (I8): two integer guard bits in every state, a
  section order and a scaling norm, and a full-scale square-wave test.
- **Cascades get relaxed designs** (I13, new): without them a ×8 cascade costs 28 MACs,
  not 16.
- **The charter is amended** in the PRs that change it (IIR-b, IIR-c), with family
  decision D20 for the stage type.
- **The flush-to-zero guard** is chosen per target and sample type (double is software on
  the M33 and M4F), uses compiler builtins rather than CMSIS, and restores only the control
  bits.
- **Corrected numbers:** the IIR group delays (2.2), the FIR ↑2 column (2.2), the cost of
  filtering before the decimator (2.3), the FIR half-band's relaxation (2.4) and the
  four-path bar (2.4).
- **The spike gets a go/no-go list** and commits its programs (section 5).

Decided: the scope, the formats and the release shape (section 1); the substrate in DspTap
0.3.0 (I1); the stage's shape (I2); compile-time design (I4); the latency contract (I5);
the guard band filter's name, an order per profile and both forms (I7); non-finite input
and the conditional flush-to-zero guard (I9); the bar for a four-path stage (I12); the
phase target (question 5); the order (section 5). Recommended and not yet put to the
maintainer: I3, I6 (restated), I8, I10, I11, I13 and I14, listed in section 8. Open until
IIR-b: the stages' names. The estimates in section 2 are marked *est.* until the spike
(IIR-0) pins them.

The plan lives at `docs/IIR_PLAN.md` (it carries a family decision and a DspTap part, as
`docs/CHANNELS_PLAN.md` did), a draft until the maintainer approves it, and is respelled
under the ×4 / ×8 / ×16 plan's X0 in that plan's X-a1, so the vocabulary gate passes it. Its accepted decisions take `rational/PLAN.md`'s next
free R numbers after that plan's, and the family decision is D20, after that plan's D19.

## 1. What this adds, and what it does not

`rational` gains recursive (IIR) stages as **additional options beside its FIR stages**,
which stay the default and stay exactly as they are:

- **↑2 and ↓2 as two-path allpass half-bands** (the harris / Valenzuela–Constantinides
  structure), in two variants:
  - **nonlinear phase**: both paths are cascades of first-order allpass sections in z⁻²
    (an elliptic, power-complementary half-band): the fewest multiplies and the lowest
    delay, with a group delay that rises toward the passband edge;
  - **near-linear phase**: one path is a pure delay and the other an allpass cascade that
    approximates it, so the passband group delay is nearly constant, at more sections.
- **A guard band filter** (`guard_band_filter<S, Ch, guard_phase>`): a low-pass at the low
  rate (before an interpolator, after a decimator), usable with **FIR or IIR** stages, in
  two forms (I7): an odd-order Chebyshev II (`guard_phase::nonlinear`) and a linear-phase
  type II FIR (`guard_phase::linear`). It suppresses the guard band, from the passband
  edge to the low rate's Nyquist. Both forms put a zero exactly at the low rate's Nyquist,
  which is where every half-band is weakest (section 2.3).
- **The door open for ↑3 and ↓3** (three-path structures): the design and stage
  interfaces are written for P paths, P = L or M, and only P = 2 is implemented. A factor
  of 3 is rejected at compile time with a message that names this plan. **Implementing
  ↑3 / ↓3 is not part of this plan.**
- **A measured answer on ↑4 and ↓4** (four-path structures, v2): the spike compares one
  four-path stage against two half-bands inside ×4, ×8 and ×16 chains (section 2.4, I12).
  The four-path stage is implemented only if the spike says so.

**The scope, decided (v6, the maintainer's interview):**
- **First consumers: conversions by powers of 2** (48 → 24 / 12 / 6, 96 → 48 and so on), as
  cascades of ↑2 / ↓2 stages. Rates below 8 kHz, such as 6 kHz, are in `rational`'s charter
  under D19 (the ×4 / ×8 / ×16 plan: the charter names ratios, and the 14 rates are the
  coverage matrix's pinned set). The 16 kHz paths from 48 and 44.1 kHz need a factor of 3 and
  stay on the FIR stages; ↑3 / ↓3 remains the door above, not this plan's work.
- **Every format in the first release:** float, double, Q15 and Q31. So the fixed-point
  datapath (I8), its headroom and limit-cycle rules and its bit-pinned tables land with the
  stages, not after them.
- **Both phase variants together:** the nonlinear and the near-linear variant ship in the
  same release, with both forms of the guard band filter. The near-linear variant's design
  method, its committed tables and its phase target (question 5) are therefore on the
  release's critical path; IIR-0 must show one near-linear design per 70 dB profile before
  IIR-a starts (section 5).
- **Latency:** each IIR stage and each chain holding one reports a nominal number of frames,
  states how far its passband's delay strays from it, and answers the delay at any
  frequency (I5). FIR chains keep their exact fraction.

**The release.** SampleRateTap 0.9.0, after the ×4 / ×8 / ×16 plan's 0.8.0, on DspTap
0.3.0. `docs/ROADMAP.md` section 8 defers "the IIR pre-filter" until a consumer pulls it;
the maintainer's request for this plan lifts that deferral, and IIR-b records it there
(section 5). Whether 1.0's freeze (`docs/ROADMAP.md` C3) covers these stages is the
×4 / ×8 / ×16 plan's question X16, answered for both plans at once.

Not added: IIR in `bridge` (147/160 needs factors 5 and 7, which these structures do not
take) or in `async` (a time-varying delay); IIR rows in the 182-row coverage matrix or the
named chains; phase equalizers; any change to an FIR output, design, pin or count.

## 2. Background

### 2.1 The two-path half-band

H(z) = ½ [A₀(z²) + z⁻¹ A₁(z²)], each Aᵢ a cascade of sections
(aₖ + z⁻²) / (1 + aₖ z⁻²) with 0 < aₖ < 1. Decimating by 2, the commutator feeds even
inputs to one path and odd inputs to the other, each path runs at the low rate (z² at the
high rate is z at the low rate), and the output is the half-sum. Interpolating by 2, both
paths run on each input at the low rate and their outputs interleave. Each section is one
multiply in the one-multiplier form. The nonlinear variant is the elliptic
(bireciprocal) half-band: power complementary, |H(e^{jω})|² + |H(e^{j(π−ω)})|² = 1, so
the passband ripple is tied to the stopband (δp ≈ δs² / 2: at 70 dB, flat to about
4·10⁻⁷ dB).

### 2.2 Sizes against today's FIR half-band (*est.*)

Elliptic orders from `scipy.signal.ellipord` at each profile's (A, passband edge), the
stopband edge mirrored about a quarter of the high rate; the FIR column is `rational`'s
pinned half-band (m taps per branch, `design.h`). MACs per output frame per channel. The
FIR interpolator counts its centre phase (one tap) as a MAC, so its ↑2 costs (2m + 1) / 2:

| profile | A | elliptic N | allpass coefficients | IIR ↑2 / ↓2 | FIR m | FIR ↑2 / ↓2 |
|---|---:|---:|---:|---:|---:|---:|
| `super_economy` | 70 dB | 9 | 4 | 2 / 4 | 9 | 9.5 / 19 |
| `economy` | 70 dB | 9 | 4 | 2 / 4 | 11 | 11.5 / 23 |
| `balanced` | 70 dB | 9 | 4 | 2 / 4 | 13 | 13.5 / 27 |
| `transparent` | 120 dB | 17 | 8 | 4 / 8 | 31 | 31.5 / 63 |

Passband group delay, nonlinear variant, in high-rate samples, from the two-path design
itself (the closed-form coefficients of I4): `economy` 2.4 … 5.0 (DC to the 3/8 edge)
against the FIR's flat 21; `transparent` 4.2 … 11.9 against 61. (v7's 2.5 … 5.9 and
4.2 … 13.4 came from an ordinary elliptic low-pass, which the two-path structure cannot
realize.) The near-linear variant's sections and delay are the spike's to measure; the
literature puts it at roughly two to three times the nonlinear's coefficients for the same
attenuation.

MACs are not instructions: a recursive section is a serial dependency per sample, so the
FIR's vectorized dots (Helium, HVX) may close some of the gap. The ratchet measures it.

### 2.3 The guard band filter, and why odd-order Chebyshev II

Every half-band crosses over at a quarter of the high rate, which is the low rate's
Nyquist: the IIR half-band is −3 dB there (power complementary), the FIR Nyquist filter
−6 dB. Interpolating, input content between the passband edge and the low Nyquist images
into the transition band with little attenuation; decimating, the transition band aliases
into the same region of the low rate. An odd-order Chebyshev II low-pass at the low rate
has a flat passband, an equiripple stopband, a steep transition, and a zero exactly at
z = −1, the crossover point itself. Placed before an interpolator or after a decimator, it
attenuates exactly the region the half-band leaves, for FIR and IIR stages alike. It
makes the stage's phase nonlinear (stated wherever it is offered); with the near-linear
variant it costs that variant's property, which is why the linear form exists (I7).

**Why after the decimator, not before.** Aliasing that lands in the passband cannot be
removed afterward, and keeping it out is the half-band's job, not this filter's.
- Each half-band has its stopband edge at r − f_pass, where r is its lower rate, the
  decimator's output rate (`rational/PLAN.md` 3.1; 2.1(a) with equality for the FIR Nyquist
  filter, bireciprocal for the IIR). Decimating by 2 folds the band about the low Nyquist,
  r / 2: at 16 kHz with f_pass = 6 kHz, the stopband starts at 10 kHz, the mirror of 6 about 8.
- Content in the stopband, [r − f_pass, r], folds onto [0, f_pass]: the passband, attenuated
  by A. So the passband is alias-free to the profile with or without this filter.
- Content in the transition band, (f_pass, r − f_pass), folds onto (f_pass, r / 2): the guard
  band. So every alias the half-band lets through lands in the guard band, mixed there with
  genuine content the half-band has already attenuated.
- The filter removes the guard band as a whole. It cannot separate the aliases from the
  genuine content there, and does not need to: that band is outside the passband, which no
  profile promises to keep.
- In a cascade the same holds stage by stage (each stage folds only into the band above
  f_pass), so one filter after the last decimator suffices.
- **Before the decimator it is a different and dearer filter** (*est.*, `cheb2ord`, 0.01 dB
  to 6 kHz, 70 dB). The same order 5, designed at 32 kHz, reaches 70 dB only at 13.2 kHz:
  it is −0.27 dB at 7.6 kHz and −10.7 dB at 10 kHz, so it leaves almost untouched the band
  that folds into the guard band. Matching the after-filter needs order 14 to 16 at twice
  the rate, about five to six times the MACs. And an odd order there puts its zero at the
  high rate's Nyquist, not at the crossover, which is the reason for an odd order. It would
  be needed only to keep genuine content in the guard band while removing its aliases.
- Interpolating is the mirror image: before the interpolator, the filter removes the
  low-rate content above f_pass that would image into the half-band's transition band.

**Its numbers.** A Chebyshev II filter has no single corner frequency; it has three numbers:
- **its passband edge**, the profile's f_pass = p · r_low, shared with the stages so a
  profile means one thing in every stage kind (I3): 6 kHz at 16 kHz for `economy`;
- **its stopband edge**, where it reaches A, set by its order;
- **its zero**, exactly at the low Nyquist (8 kHz at 16 kHz).

At 16 kHz, `economy` (70 dB, flat within 0.01 dB to 6 kHz; *est.*, `cheb2ord`), the stopband
starts at 7.92 kHz for order 3, 7.61 for order 5, 7.25 for order 7, 6.96 for order 9 and
6.74 for order 11. The edges are fractions of the rate, like every profile in the family,
never a fixed number of Hz below Nyquist. A wider passband would be a custom profile, for
the stages and the filter alike.

**What it leaves.** Between its passband edge and its stopband edge (6 to 7.61 kHz at 16 kHz,
order 5) the filter is in its own transition, so aliases there are only partly attenuated. A
higher order narrows that band and never removes it. What it guarantees is the band from its
stopband edge to Nyquist at A, and its zero at Nyquist, where the half-band's aliases are
strongest (at −3 or −6 dB, about as loud as the signal). That matters to what follows at the
low rate:
- a nonlinear process, which would intermodulate strong aliases near Nyquist down into the
  passband;
- a low-rate output that is listened to as it is (16 kHz speech);
- FFT bins near Nyquist.

It is not needed by the ×4 / ×8 / ×16 plan's round trip: there the change is masked to the
usable bins, so it carries nothing in the guard band, and the full-band path carries the
guard band untouched (that plan's 6.3).

Reach (*est.*, `scipy.signal.cheb2ord`, passband flat to the profile's edge within
0.01 dB), as the stopband edge it reaches in fractions of the low rate's Nyquist:

| A | profile | order 3 | order 5 | order 7 | order 9 |
|---|---|---:|---:|---:|---:|
| 70 dB | `super_economy` (0.667) | 0.986 | 0.931 | 0.870 | 0.820 |
| 70 dB | `economy` (0.750) | 0.990 | 0.951 | 0.906 | 0.869 |
| 70 dB | `balanced` (0.792) | 0.992 | 0.960 | 0.923 | 0.892 |

At 48 kHz, `economy` with a fifth-order guard band filter is flat to 18 kHz and 70 dB down
from about 22.8 kHz, with the zero at 24 kHz.

### 2.4 Factor 4 inside a chain: one four-path stage or two half-bands (v2, *est.*)

**Why ask.** For the FIR stages, the ×4 / ×8 / ×16 plan measured that the cheapest ×8 chain
at the 70 dB tiers is ↓4 · ↓2 with a single 4th-band stage: 85 MACs per output against 113
for three half-bands at `economy`, with today's designer. The audit found the cause: the
designer takes its Kaiser β for A dB (`design.h:301`, `nyquist.h:214`) while the pin search
accepts a design only at A + 1 dB, so the relaxed half-band misses the margin until m = 7
at design divisors 2 to 4. (It relaxes further past them: m = 4 at divisors 6 to 9 and
m = 3 at 12 and 16.) With β taken for A + 2 dB, three half-bands tie ↓4 · ↓2 at ×8. That
plan's X-0 measures both designers (its X3); the FIR rows below are today's designer's.

**Why the IIR answer may differ.** A relaxed IIR half-band keeps getting cheaper as its
transition widens, down to a single coefficient (N3). Each stage of a half-band cascade is
designed at its own rate, with f_pass = p · r_out and its stopband edge mirrored about its
own low Nyquist. The elliptic orders from `scipy.signal.ellipord`, with the ripple tied by
power complementarity, are:

| profile | ×2 | ×4 | ×8 | ×16 |
|---|---|---|---|---|
| `economy` (70 dB, 3/8) | N9 | N5 · N9 | N5 · N5 · N9 | N3 · N5 · N5 · N9 |
| `transparent` (120 dB, 5/12) | N17 | N9 · N17 | N7 · N9 · N17 | N5 · N7 · N9 · N17 |

Stages run from high rate to low. An order-N half-band has (N − 1) / 2 allpass
coefficients, and a decimator spends one multiply per coefficient per output. So in MACs
per output frame per channel (decimating; *est.*), against the cheapest FIR chain measured.
The relaxed rows need I13's designs per divisor; without them every stage runs the
profile's full design:

| profile | ×2 | ×4 | ×8 | ×16 |
|---|---:|---:|---:|---:|
| IIR half-band cascade, `economy`, relaxed (I13) | 4 | 8 | 16 | 24 |
| IIR half-band cascade, `economy`, unrelaxed | 4 | 12 | 28 | 60 |
| cheapest FIR chain, `economy` (**measured**, today's designer) | 23 | 53 | 85 | 157 |
| IIR half-band cascade, `transparent`, relaxed (I13) | 8 | 16 | 28 | 44 |
| IIR half-band cascade, `transparent`, unrelaxed | 8 | 24 | 56 | 120 |
| cheapest FIR chain, `transparent` (**measured**, today's designer) | 63 | 105 | 181 | 301 |

**What a four-path stage has to beat.** Inside ×8 at `economy`, the top two half-bands
(N5 and N5) cost 8 + 4 = 12 MACs per chain output. A four-path stage
H(z) = ¼ Σₖ z⁻ᵏ Aₖ(z⁴) in their place, at design divisor 2, spends one multiply per allpass
section across its four paths per output, at twice the chain's output rate. So it must
meet the pair's spec with **at most 5 sections in total** (fewer than 1.5 per path); 6 is a
tie, and IIR-0 records it.
- **The spec it has to meet** is what the pair rejects, not 70 dB everywhere: the bands that
  fold onto the passband, [2r − f_pass, 2r + f_pass] ∪ [4r − f_pass, 4r] at the 8 r rate
  (r = the chain's output rate). Across the whole band above 13/64 of that rate the pair
  reaches only about −42 dB, so a four-path stage asked for 70 dB there would fail at any
  section count. On the right spec the pair reaches −76 dB on the first band and −107 dB
  on the second (*est.*, the closed-form N5 designs).
- The early half-bands are already nearly free (one or two coefficients each), so the
  expectation is that the cascade stays cheaper in MACs for IIR. That is an expectation,
  not a measurement.

**Why measure it anyway.**
- **Group delay:** one four-path stage may delay less than two cascaded half-bands, which
  matters to a latency-first caller and to the near-linear variant's delay path.
- **Instructions, not MACs:** a cascade is a chain of serial recursions; four independent
  paths give the compiler four recursions to interleave, which may count fewer
  instructions on Helium and HVX at the same MACs.
- **The near-linear variant** needs more sections per path, which changes the arithmetic.

The structure is published: f. harris's M-path recursive polyphase filters and Renfors and
Saramäki's recursive Nth-band filters (section 7). It fits the P-path interface I2 already
reserves, with P = 4.

## 3. Decisions

- **I1. Where the code lives. *Decided (v8):*** the substrate rule. The design math (the
  allpass half-band coefficients, the Chebyshev II odd-order design and its
  second-order-section pairing, the linear form's tables) and the kernels (the first-order
  allpass section and the biquad / first-order cascade, per `sample_traits` format with
  documented Q formats), the flush-to-zero guard (I9) and the chain's latency members (I5)
  land in DspTap first, with its own battery, in DspTap 0.3.0, which also carries the
  ×4 / ×8 / ×16 plan's round-trip scaffold (the maintainer's choice, v8; section 5).
  `rational` holds only the stage types and their contracts.
  - **DspTap's checklist for a new primitive** applies: a README section and the primitive
    count in its intro and in `CLAUDE.md`'s list; a contract docstring; each profile's
    numbers stated (double, float, Q15 and Q31: scale, floor, saturation-free level); a
    typed float/double battery with cross-precision agreement; and the QEMU legs that run
    it (M4, M4F, M33, M55, with the `MAIN_FILTER` naming anything excluded).
  - **The design note.** `kaiser.h:14-22` and `nyquist.h:54` say every DspTap designer is
    runtime double. IIR-a adds the constexpr designers (I4) and amends that note to state
    their scope: a handful of coefficients per design, not long FIR tables.
- **I2. The public shape. *Decided (v5);* the names are chosen in IIR-b.** A stage
  template of its own beside `basic_stage`, `basic_iir_stage<S, Ch, R, P>` with `P` an
  `iir_phase` (`nonlinear`, `near_linear`), a `static_assert` that R is `up2` or `down2`
  (its message names ↑3 / ↓3 as planned, and admits `up4` / `down4` if I12's four-path stage
  ships), and aliases beside `converter<Ch, R>`. The names stay with the maintainer (D4, D5,
  D15); placeholders here are `iir_converter<Ch, R>` and `iir_converter_linear<Ch, R>`.
  *Declined:* an IIR profile of `basic_stage`, since the machine, the state and the contracts
  differ, and the family's one-meaning-per-profile rule (A4) would then carry two machines
  under one word.
  - **The family decision (D20).** The family plan's section 2 says "Minimum-phase and IIR
    tiers are profiles." D20 supersedes that line: IIR stages are a stage type of their own,
    not a profile, and still not an engine. IIR-b edits `PLAN.md` section 2 to cite it.
  - **The family's stage rules** apply to `basic_iir_stage` and to `guard_band_filter`
    alike:
    - A12: `process()`, `pull()` and `flush()` out of line (`TAP_SR_RATIONAL_NOINLINE`),
      each the block for its channel count, with the state in a cursor;
    - K12 / R17: the count-independent state and the cold code in a base built once per
      format, ratio and variant (placeholder `iir_stage_base<S, R, P>`);
    - D17: a `noexcept validate()` stating each rejection in the constructor's words, the
      C ABI validating before it constructs, and rejection tests through
      `TAP_SR_EXPECT_REJECTED`, for custom profiles and guard band settings above all;
    - each PR that adds a public header updates `_header_count_rational`
      (`rational.Family.HeaderCountIsPinned`).
- **I3. Profiles.** *Recommended:* the same four names at the same (A, passband edge)
  numbers, so `economy` means 70 dB flat to 3/8 in every stage kind; each variant pins its
  section count per profile in the spike. If the near-linear variant (or float at 120 dB)
  cannot reach a profile, that profile is not offered for it, stated rather than
  approximated. Each profile also has its relaxed designs per divisor (I13).
- **I4. Where the coefficients come from: compile time where the design is closed form
  (decided, v5).** The FIR stages design at construction. The nonlinear variant and the
  Chebyshev II guard band filter have closed-form designs (Gazsi's explicit formulas; the
  elliptic half-band of Vaidyanathan and of Renfors and Saramäki; the Chebyshev II's
  analogue prototype). The near-linear variant needs an iterative optimization, and so does
  an equiripple linear guard band filter.
  - **The feasibility, measured (v5).** A scratch prototype computes the elliptic
    half-band's allpass coefficients in `constexpr`, from the published closed form (the
    nome series of Valenzuela and Constantinides, as harris's book evaluates it).
    - It uses constexpr sqrt, sin and cos of its own (double Newton and Taylor series),
      and no libm. That is like DspTap's `srdif_trig` only in using no libm: `srdif_trig`
      evaluates in 64-bit integer arithmetic, at construction. These are evaluated at
      compile time in double, so their bit stability rests on the compiler's constant
      evaluator, which IIR-0 checks per compiler.
    - Under GCC 13 and Clang 18 (`-std=c++20`) it compiles to the same four coefficients
      for `economy`'s half-band, bit for bit on both compilers, within 1.1·10⁻¹⁵ of the
      same design through libm.
    - The two-path response is 77.6 dB down from its stopband edge (the spec is 70) and
      flat to 7.6·10⁻⁸ dB.
    - **Not yet measured:** MSVC (CI's Windows leg, `/W4 /WX`) and AppleClang. The
      prototype takes about 15.8k Clang constexpr steps for one N9 design and 30.5k for N9
      and N17 together; MSVC's default step limit is about a tenth of Clang's. IIR-0
      compiles the half-band and a Chebyshev II prototype under all four compilers,
      records the step margin at the largest named order, and compares the coefficient
      bits.
  - **The rule.** The nonlinear variant and the Chebyshev II design with `constexpr`
    functions in DspTap (I1).
    - The named profiles' coefficients are `static constexpr` tables, computed at compile
      time: no design at construction and no libm. Their bits are pinned per compiler by
      IIR-0 and then by a test.
    - A custom profile calls the same functions at construction, at run time.
    - The fixed-point tables (I8) round from the same constexpr doubles.
    - The constexpr math (sqrt, sin, cos; sinh and asinh, or their series, for the
      Chebyshev II) is tested against libm and an mpmath leg, with its error pinned.
  - **Committed tables for the iterative designs:** the near-linear variant and the
    linear guard band form, per named profile or setting, generated by scripts in `tools/`
    (as the scipy reference vectors are) and pinned by tests. An iterative optimizer over a
    dense grid would exceed compile-time evaluation limits (Clang's default is 1,048,576
    steps) and would be slow to compile even where it fits. *Recommended (v8):* the linear
    form's generator is equiripple (`scipy.signal.remez`), and a custom linear setting
    designs at run time through DspTap's Kaiser designer, pinned by tolerance.
  - The Chebyshev II was not prototyped; its constexpr design is the spike's to confirm.
- **I5. Latency and group delay. *Decided (v6, question 3);* the API placed (v8).** Today
  every stage and chain reports its latency as an exact fraction of output frames (R7). The
  matrix's ↓8 · ↓2 reports 207/16 = 12.9375 frames, for example, because a linear-phase FIR
  delays every frequency by the same amount. A recursive stage has no single delay. The
  nonlinear variant delays frequencies near its passband edge more than low ones (2.4 to 5.0
  high-rate samples at `economy`, 2.2), and the near-linear variant delays them almost
  equally, about a known whole number of samples.
  - **A stage** reports `group_delay_output_frames(f)` (a double, from its coefficients) and
    a nominal latency: the near-linear variant its delay-path length (exact), with the
    passband deviation stated; the nonlinear variant its DC group delay, with the passband
    maximum stated. IIR stages never provide `latency_output_frames()`.
  - **The near-linear variant's delay path** is also an exact member (placeholder
    `delay_path_output_frames()`). The ×4 / ×8 / ×16 plan's round-trip scaffold accepts it
    in place of an exact latency; the nonlinear variant has none, so the scaffold refuses it
    at compile time (that plan's X10).
  - **A chain.** Mixed chains are `tap::dsp::chain`s (I10), whose only latency today is the
    exact `latency_output_frames()`, asserted per stage when it is called. IIR-a adds to
    DspTap's chain, with its test: a nominal latency (a double), the passband deviation and
    `group_delay_output_frames(f)`, composed over the stages, with each FIR stage
    reporting its exact latency as a constant group delay. The exact latency still compiles
    only for all-exact chains. The `chain` concept itself needs no change.
- **I6. Flush.** `flush()` feeds `window_frames()` zeros, and DspTap's chain defines that
  count as the zero input frames after which the stage's output is silence
  (`chain.h:45-54`), pinned bit for bit by `FlushEqualsZeroPaddingBitForBit` in DspTap and
  in `rational`. A recursive stage's tail never ends on its own. *Recommended (v8, restated
  by the audit):* `window_frames()` is the count after which the state is **exactly zero**,
  so the contract holds unchanged.
  - **Floating point:** the count from the format's largest finite value (over the state
    headroom) down to its smallest normal value, through the stage's largest coefficient,
    computed at design time. Below that the guard or the per-section flush (I9) zeroes the
    state, and the per-section flush's threshold is the smallest normal value, so the count
    is the same on every target. *Est.:* about 850 low-rate frames in float and 6900 in
    double at `economy` (a = 0.8134), 2100 and 16700 at `transparent` (a = 0.9188); IIR-0
    pins them, and the flush's output length grows by that much.
  - **Fixed point:** the count from the full state headroom (I8) to exact zero under the
    chosen rounding, which the limit-cycle search (I8) establishes.
  - `FlushEqualsZeroPaddingBitForBit` runs over chains holding IIR stages and both guard
    band forms. Non-finite input is the exception, as I9 states.
- **I7. The guard band filter's shape. *Decided (v3, v5, v7).*** A 1:1 stage of its own,
  `guard_band_filter<S, Ch, guard_phase>` (the maintainer's name), that a caller places in a
  chain, before an interpolator or after a decimator, FIR or IIR. It is designed from the
  profile's passband edge, an order and A, and its stopband edge follows (2.3). **An order
  per profile** (v5), each set by the spike: `guard_band_settings::economy()` and its
  siblings, and a custom form. The Chebyshev II form takes odd orders only. DspTap's `chain`
  already takes a 1:1 stage (its ratio product and output bounds assume no L ≠ M); IIR-a
  adds the test that pins it. *Declined:* an option on each stage, since it would double
  every stage's surface and could not serve the FIR stages without touching them.
  - **Both forms (v7), the phase a type parameter as in I2:** `guard_phase::nonlinear`
    (the Chebyshev II, the cheapest) and `guard_phase::linear` (an even-length symmetric
    FIR, type II, linear phase, with its zero exactly at Nyquist from its symmetry, as the
    odd-order Chebyshev II has from its order).
  - **Why the linear form.** The Chebyshev II makes a chain's phase nonlinear, which costs
    the FIR stages and the near-linear variant their one property wherever a guard band
    filter is wanted: a low-rate output that is listened to, a nonlinear process at the low
    rate, analysis near Nyquist (2.3). With the linear form an all-FIR chain keeps R7's exact
    latency. (v7 also gave the round trip as a reason; it is not one, since the round trip
    needs no guard band filter, 2.3.)
  - **Sizes**, against the Chebyshev at 16 kHz, `economy`, both flat to 6 kHz within
    0.01 dB peak to peak, 70 dB (*est.*, `scipy.signal.remez`; the Chebyshev's MACs counted
    as biquads with b0 = b2 plus one first-order section):

    | 70 dB from | Chebyshev II | type II FIR |
    |---|---|---|
    | 7.61 kHz | order 5, about 10 MACs | 40 taps: 20 MACs folded, 19.5 samples (1.22 ms) |
    | 7.25 kHz | order 7, about 14 MACs | 48 to 50 taps: 24 to 25 MACs, 23.5 to 24.5 samples (1.5 ms) |
    | 6.96 kHz | order 9, about 18 MACs | 64 taps: 32 MACs, 31.5 samples (1.97 ms) |

    So the linear form costs about twice the MACs (2.0, 1.7 to 1.8 and 1.8). Its delay is a
    whole number of half-samples, so it is still an exact fraction (39/2 frames at order
    40).
  - **Its coefficients:** committed tables per named setting (I4).
  - **Pinned per form** (section 4): its zero at Nyquist, its stopband from its stated edge,
    its fixed-point tables; for the linear form also its symmetry and its exact latency
    inside an all-FIR chain. Each form has a ratchet workload, a C ABI entry and a row in
    the offering table (I10, I11).
- **I8. Fixed point.** *Recommended:*
  - **Formats:** Q15 samples with Q31 coefficients (every aₖ is in (0, 1)); Q31 samples with
    64-bit states; the biquads in Q2.30 coefficients (|a₁| < 2); 64-bit accumulation, one
    rounding point per section, saturation at the output, all stated as numbers in the
    header.
  - **Headroom (v8, from the audit).** An allpass keeps energy, not peak level: inside a
    path, a node can exceed the input's full scale. The paths' L1 norms reach 2.11 and 2.69
    at `economy` and 3.06 and 3.71 at `transparent` (*est.*), and a full-scale square wave
    drives an internal node to 1.93. So every state carries **two integer guard bits**
    (|state| < 4): Q15 samples run in 32-bit Q2.29 states, Q31 samples in 64-bit states with
    the same two bits. IIR-0 confirms the norms per profile and variant.
  - **The biquads:** direct form I in fixed point (one rounding point, at the section's
    output, through the 64-bit accumulator), transposed direct form II in float; sections
    ordered by increasing pole radius, with the gain distributed by the L∞ norm (Jackson,
    1970). The wrong order lets partial cascades gain +5.7 to +11.1 dB; this one keeps them at
    or below 0 dB (*est.*).
  - **Limit cycles.** Recursive fixed point can hold a zero-input limit cycle, so the
    rounding mode is chosen against the literature (Jackson, 1969). IIR-0 runs an exhaustive
    or randomized zero-input search over the stored-state formats, with the chosen rounding,
    for each section kind (the one-multiplier allpass and the biquad), and a test pins that
    silence after signal decays to exact zero within a stated count (I6).
  - **Tests:** full-scale square and clipped input through every profile with no internal
    saturation; the limit-cycle decay; unity gain and the Q15 floors.
  - **Cost:** a Q31 section is a 64 × 32 multiply; IIR-0 counts Q15 and Q31 sections on the
    M33, M55 and Hexagon.
- **I9. Non-finite input and subnormals.**
  - **Non-finite input. *Decided (v5, question 6).*** An FIR stage clears a non-finite burst
    after one window (`test_non_finite.cpp`); a recursive state never does. State it as the
    honest limit and pin it (a non-finite input poisons the stage until `reset()`), rather
    than pay a per-sample check.
  - **Subnormals: why a recursive state needs a rule.** After the input goes silent, a
    float state decays into the subnormal range. Two things then go wrong:
    - subnormal arithmetic is slow on x86 and on some A-class Arm cores, the classic CPU
      spike on silence;
    - on any target, a first-order section with 1/2 < a < 1 sticks at k times the smallest
      subnormal, for k ≤ 1 / (2 (1 − a)): under round-to-nearest, a times it rounds back to
      itself, so the state never reaches zero. That is twice the smallest subnormal at
      `economy`'s a = 0.8134 and six times at `transparent`'s a = 0.9188 (**measured**, the
      audit, M33 under QEMU).

    FIR stages have neither problem, since their delay lines empty after one window.
  - **The control bits, and the arithmetic they govern.**
    - x86: MXCSR's FTZ (bit 15) and DAZ (bit 6), through `_mm_getcsr` / `_mm_setcsr`
      (`<xmmintrin.h>`), for float and double alike;
    - AArch64: FPCR.FZ (bit 24), through `mrs` / `msr` on FPCR (inline assembly under GCC
      and Clang, `_ReadStatusReg` / `_WriteStatusReg` under MSVC), for float and double;
    - Cortex-M with an FPU, and AArch32: FPSCR.FZ (bit 24), through the compiler builtins
      `__builtin_arm_get_fpscr` / `__builtin_arm_set_fpscr` (GCC and Clang, no header). On
      Cortex-M, FPDSCR (0xE000EF3C) holds the FPSCR that exception handlers start with. The
      bit governs only what the FPU computes: float on the M4F and M33, float and double on
      the M55 (`__ARM_FP & 8`). Double on the M4F and M33, and everything on the M4 without
      an FPU, is libgcc's soft-float, which FZ does not govern (**measured**, the audit:
      with FZ set, a float state reaches 0 after 423 samples on the M33, and a double state
      is still 9.88·10⁻³²⁴ after 20000);
    - Hexagon: IIR-0 establishes its floating-point control (USR) and how faithfully
      `qemu-hexagon` models it.

    No CMSIS header is used: DspTap is standard-library only, and its vendored CMSIS-Core
    reaches the include path only under `TAP_DSP_FFT_CMSIS`, on the M55 alone.

    The bit belongs to the thread, and so to the host. Most audio hosts set it on their
    audio threads, but a library cannot assume they do, and must not change it beyond its
    own call. `-ffast-math`'s start-up FTZ is not something the family builds with.
  - ***Decided (v4, question 8):*** a scoped guard in DspTap (I1; placeholder
    `tap::dsp::flush_subnormals`). The IIR stages' float and double `process()`, `pull()`
    and `flush()` construct it on entry.
    - **The mechanism is chosen per target and sample type, at compile time (v8):** the
      guard where the bit governs that type's arithmetic, and the per-section flush (a
      compare and select per state per sample, its threshold the type's smallest normal
      value) everywhere else: double on the M4F and M33, and both types on the M4 soft leg.
    - **It touches the register only when it has to** (the maintainer's decision, v4). It
      reads the control register and compares it with the bits it needs (FZ; FTZ and DAZ on
      x86).
      - When they are already set, as most audio hosts set them on their audio threads, it
        does nothing more: no write and no restore.
      - When they are not, it sets them and on exit restores **only the control bits**,
        keeping the exception flags the call raised. So both paths leave the caller's flags
        the same way (the stage's own arithmetic may raise the inexact flag in either).

      The branch runs once per call and always goes the same way for a given host, so it is
      predicted; on Cortex-M the compiler may predicate the write with an `IT` block
      instead.
    - **Why it restores at all.** The bit belongs to the thread, and so to its host.
      - A stage that left FTZ on would change the arithmetic of everything after it on that
        thread: other plugins, the host's own DSP, anything relying on gradual underflow,
        and, with DAZ, every comparison of a subnormal.
      - In the test batteries, which run many tests on one thread, results that pass through
        subnormals would depend on test order.
    - **Its cost is per call, not per sample.**
      - x86-64 (**measured**, a shared Xeon VM at 2.1 GHz, median of 15 runs of 4M calls, three
        runs, indicative):

        | caller's FTZ/DAZ | unconditional set and restore | this guard, conditional |
        |---|---|---|
        | already on | +6.5 to +6.7 ns per call | about 0 (within ±0.07 ns) |
        | off | +5.6 to +6.5 ns | +5.3 to +6.2 ns, the same within noise |

        So the conditional form is never slower, and it is free in the usual host case.
        Against a stereo two-path half-band decimator with four sections (0.6 µs per
        32-frame block, 5 to 6 µs per 256), either form is within run-to-run noise.
      - The per-section flush costs 18 to 50 % on the same blocks (**measured**); it remains
        only where the bit does not govern the arithmetic.
      - What the guard saves (**measured**): with subnormal states and no guard, the same
        blocks run 9 to 11 times slower, 6.9 µs instead of 0.6 µs at 32 frames.
      - Cortex-M4F, M33 and M55, unconditional form (GCC 13): `vmrs`, `orr`, `vmsr`, `vmsr`.
        The conditional form adds a compare and a branch, or an `IT`, and drops both `vmsr`
        when the bit is set. The ratchet counts the difference per call; cycles are not
        measured (no hardware here).
      - AArch64: `mrs`, `orr`, `msr`, `msr` unconditionally, and only the `mrs` and the
        compare when the bit is set. An FPCR write can cost more than an ordinary
        instruction on some cores, which is where skipping it gains most; IIR-0 measures
        both forms on the macOS runner's Apple silicon.
    - **Two pinned tests in every build:**
      - `IirStage.SilenceDecaysToExactZero`: silence after signal decays to exact zero within
        the stated count (I6), for float and double, on every QEMU leg (double on the M33
        through the per-section flush);
      - `IirStage.LeavesTheFloatingPointEnvironmentAsItFoundIt`: the caller's control bits
        are unchanged after a call, compared without the cumulative exception flags (MXCSR
        & ~0x3F; FPSCR and FPCR without their cumulative flags and NZCV), in both host
        states, built on the M4 (where the guard is empty), M4F, M33 and M55 legs.
    - The fixed-point profiles have no subnormals; their counterpart is the zero-input
      limit cycle (I8).
  - *Declined:*
    - the caller's responsibility, documented: no cost, but the slow path returns whenever
      a host forgets;
    - the per-section flush everywhere: no register access, but a compare per state per
      sample on every target.
- **I10. Composition.** *Recommended:* the IIR stages and the guard band filter satisfy
  `sync_stage`, so a caller's `tap::dsp::chain` mixes them with FIR stages (an IIR ↑2 and a
  FIR ↑3, a guard band filter before either). `rational`'s `basic_chain` takes ratio types
  only, so a mixed chain is a `tap::dsp::chain`, and it does not relax its stages: the
  caller builds each stage at `profile.relaxed(d)`, or uses I13's helper for a power-of-2
  cascade. No named IIR chains and no coverage-matrix rows at first; a separate small table
  states the offering: ↑2 and ↓2 × four profiles × two variants, with no guard band filter,
  the Chebyshev II form, or the linear form.
- **I11. The C ABI and the notebooks.** *Recommended:*
  - **New entry points** for the IIR stages and the guard band filter (placeholders
    `tap_sr_rational_create_iir_stage_format` and `tap_sr_rational_create_guard_band_format`),
    so no existing symbol changes; the symbol set is re-pinned with additions only.
  - **Their latency:** a double getter for the nominal latency and the group delay at a
    frequency. The exact `tap_sr_rational_latency_output_frames(c, num, den)` fails for an
    IIR handle, as its C++ counterpart does not compile.
  - The ctypes binding, and a notebook committed executed that measures FIR against both IIR
    variants, with no guard band filter and with each form: response, aliasing and imaging,
    group delay, latency, MACs.
  - **A demonstration notebook (v5, the maintainer's ask):** `guard_band_demo.ipynb`
    (placeholder) in `rational/notebooks/`, committed executed, made to show the guard
    band filter to others.
    - **The signal:** a logarithmic sine sweep from 20 Hz to just below the input Nyquist,
      at −6 dBFS, in float, through the C ABI, so it runs the shipping C++. IIR-0's
      version runs scipy designs and says so.
    - **The plots:**
      - a spectrogram per configuration, all on one fixed scale (0 to −140 dB), with the
        passband edge, the guard band filter's stopband edge and the low Nyquist drawn on
        it, so 70 dB of stopband reads at a glance;
      - a 3-D waterfall of stacked spectra for the same runs;
      - a table of the worst alias level in the passband, in the guard band, and above
        the filter's stopband edge.
    - **The configurations, side by side:**
      - the FIR half-band alone, and with each guard band form;
      - the IIR nonlinear variant alone, and with each form;
      - the near-linear variant alone, and with each form;
      - the filter before the decimator against after it, which measures 2.3's argument:
        the same order before leaves the guard band, and the order that matches it costs
        about five to six times as much;
      - the interpolating mirror: a low-rate sweep through ↑2, with and without the
        filter before it, where the images appear;
      - each configuration's group delay.
    - **Listening:** short clips of the sweeps, written by the notebook to files outside
      the tree rather than embedded, so the committed notebook stays small.
    - The book's Part VII takes its figure from the same code; `scripts/book_figures.py`
      already draws the book's figures.
- **I12. Factor 4: a four-path stage or two half-bands. *Decided (v5, question 7).*** The
  spike (IIR-0) measures, inside ×4, ×8 and ×16 chains at every profile and both variants,
  the half-band cascade against each placement of one four-path stage, on the spec of 2.4:
  MACs, passband group delay, and instructions on the M33, M55 and Hexagon. The four-path
  stage ships only if it beats the half-band pair by more than R3's 10 % in MACs or
  instructions at some profile, or if a latency-first consumer asks for its group delay.
  Otherwise ×4 stays two half-bands, as R3's cost choice makes it for the FIR stages after
  the ×4 / ×8 / ×16 plan's X3, and the result is recorded with its numbers. *Expected,* from
  section 2.4: the cascade wins on MACs.
- **I13. Relaxed designs for cascades (v8, new).** *Recommended:* an IIR stage, like an FIR
  stage, is designed at its design divisor d within a chain (its lower rate over the chain's
  lowest), with the passband edge fixed at p · r_min and the stopband mirrored about its own
  low Nyquist. Each profile's `relaxed(d)` has compile-time tables (I4) at the divisors a
  power-of-2 cascade uses (2, 4 and 8; the near-linear variant's committed tables likewise),
  and `rational` offers one helper (placeholder `iir_halfband_chain<S, Ch, P, D>`) that
  composes a power-of-2 cascade with each stage relaxed to its divisor, since a raw
  `tap::dsp::chain` does not. Without it, the cascades cost 2.4's unrelaxed row, 1.5 to 2.5
  times the relaxed one at `economy`.
- **I14. The float profile inside the recursion (v8, new).** `sample_traits<float>` is float
  input and output with double accumulation, and DspTap runs numerically fragile recursions
  in double even in the float profile. On the M33 and M4F double is soft-float, about 19
  times a Q15 instruction count by `sample_traits.h`'s own comment. *Recommended:* the IIR
  stages' float profile keeps its states and products in float. The one-multiplier allpass
  section is structurally passive and insensitive to its coefficient, so it is not the
  fragile kind; the biquads run in float too. IIR-0 measures float's noise floor at every
  profile and counts one section both ways on the M33 and M4F. If float cannot reach
  120 dB, `transparent` is not offered in float (I3), rather than paying soft double. With
  float states, the flush-to-zero guard covers float on the M4F and M33 (I9).

## 4. Tests and gates

- **Every FIR pin unchanged**: the scipy vectors, the matrix rows, the Q15/Q31 bit-for-bit
  tables, the C ABI's bit-for-bit tests, and every existing ratchet workload's count
  identical to the instruction (the FIR code is not touched). (`rational`'s float output
  hashes are deliberately not pinned, since they depend on the compiler's contraction.)
- **The IIR outputs:** Q15 and Q31 bit-pinned; float and double against the golden leg
  within a stated tolerance per format, not bit-pinned (SampleRateTap does not pass
  `-ffp-contract=off`, so float bits differ between x86 and the Arm legs).
- **A golden leg independent of the structure**: the same transfer function in direct
  form through `scipy.signal.lfilter`, with zero-stuffing or down-sampling around it,
  against the polyphase stage within a stated tolerance per format; the committed vectors
  generated by `tools/reference/`.
- **The spec measured**: stopband, passband ripple, imaging and aliasing per profile with
  DspTap's analysis instruments; power complementarity for the nonlinear variant; the
  near-linear variant's passband **phase error**, pinned per stage at its divisor and summed
  over the round trips the ×4 / ×8 / ×16 plan composes (question 5).
- **The relaxed designs** (I13): each divisor's table meets its spec, and the helper's
  cascade costs what 2.4's relaxed row says.
- **Latency** (I5): each stage's nominal latency and group delay against the golden leg; the
  chain's composed members; the near-linear delay path exact.
- **Flush** (I6): `FlushEqualsZeroPaddingBitForBit` over chains holding each IIR variant and
  each guard band form.
- **The guard band filter, per form**: its zero at Nyquist, its stopband from its stated
  edge, its fixed-point tables, that a decimating cascade's aliases land only above the
  passband edge (`GuardBand.AliasesLandOnlyAboveThePassbandEdge`), and the combined response
  with an FIR and an IIR half-band; for the linear form also its symmetry and its exact
  latency inside an all-FIR chain.
- **Fixed point**: unity gain, the headroom (full-scale square and clipped input, no
  internal saturation), saturation at the output, the Q15 floors, the limit-cycle decay.
- **Subnormals and the environment**: I9's two tests on every leg.
- **The family's rules**: channels in the type; A12, K12 and D17 (I2), with rejection tests
  through `TAP_SR_EXPECT_REJECTED`; the pinned header count; both builds (exceptions and RTTI
  off); the QEMU legs; the ratchet with new workloads (↑2 / ↓2, float and Q15, both variants,
  `economy`, with no guard band filter and with each form) under the family's ±3 %, recorded
  at the midpoint of the two builds where they split; and the size and stack ceilings.

## 5. The pull requests, in order

1. **IIR-0, the design spike** (`rational/notebooks/iir_spike.ipynb`, committed executed,
   with the scratch programs this plan cites, moved under `rational/tools/spikes/iir/` and
   every **measured** number re-cited against its path there): section counts and
   coefficients per profile, variant and divisor (I13), the guard band settings, the
   fixed-point norms and floors and the limit-cycle search (I8), float's noise floor (I14),
   MACs, phase error and group delay, I12's four-path comparison at ×4, ×8 and ×16
   (instruction counts from a scratch build on the ratchet's targets), the Chebyshev II's
   constexpr design and the four compilers (I4), the linear-phase form's sizes (I7),
   Hexagon's floating-point control (I9), the FTZ guard's cost on Apple silicon, and the
   demonstration notebook's first version on scipy designs (I11). Nothing ships in the
   library. **Its go/no-go list**, each result and the decision it reopens:
   - **No near-linear design** reaches a 70 dB profile with a named, published method
     (Krukowski and Kale's, or another in section 7) → that profile is not offered for it
     (I3); if none is reached, section 1's "both variants together" goes back to the
     maintainer before IIR-a.
   - **The round trip's phase.** A 70 dB stopband already bounds a near-linear stage's
     passband phase error to about 0.02° if its design weights the bands equally (*est.*,
     unverified). If IIR-0 confirms it, the round trip cancels to about −52 dB at ×8 and ×16
     at no extra sections, and question 5's target may be tightened toward the FIR's
     −60 dB (the maintainer's call).
   - **Float at 120 dB** fails → `transparent` is not offered in float (I14).
   - **A limit cycle** survives the chosen rounding → the rounding changes before IIR-a (I8).
   - **Hexagon has no flush-to-zero control**, or `qemu-hexagon` does not model it → the
     per-section flush's cost there is counted, and I9 reopens if it exceeds 10 % of a
     block.
   - **The constexpr design** exceeds a compiler's default step limit, or its bits differ
     between compilers → that design moves to committed tables (I4 reopens).
   - **The four-path stage** passes I12's bar → it joins IIR-b's scope.
2. **IIR-a, DspTap** (part of DspTap 0.3.0): the design math and kernels, the guard, the
   chain's latency members (I1, I5, I9), their battery and fingerprints, DspTap's checklist
   (I1), and the clean-room record in `NOTICE.md` (section 7). If it is the second of the
   two 0.3.0 PRs to land, it bumps `version.h` and `project()` to 0.3.0, and the maintainer
   tags the commit `v0.3.0`.
3. **IIR-b, `rational`'s ↑2 / ↓2 stages**, both variants (I2–I6, I8, I9, I13, I14), with the
   golden leg, the README and the book's Part VII. It also:
   - moves `submodules/dsptap` to `v0.3.0` and raises `TAP_SR_DSPTAP_MINIMUM` to 0.3.0 in all
     three umbrella headers, with the root README's tested pairs (A9);
   - amends `rational`'s charter, which today says a stage is one Nyquist FIR filter and
     there are two machines: `rational/PLAN.md` section 1 and R2, `rational/CLAUDE.md`'s
     charter constraints, the root README's `rational` row (with which profiles each IIR
     variant offers) and the root `CLAUDE.md`'s "chains of Nyquist stages";
   - records D20 in the family plan, edits its section 2 line, and adds this plan's R
     decisions after the ×4 / ×8 / ×16 plan's;
   - adds this plan's PRs to `docs/ROADMAP.md` as a block, with section 8's deferral lifted;
   - updates the header-count pin.
4. **IIR-c, the guard band filter**, both forms (I7), and the composition tests (I10), with
   the charter's note that a 1:1 guard band stage is admitted beside the ratios
   (`ratio<L, M>`'s `static_assert` unchanged).
5. **IIR-d, the C ABI, the binding, the notebooks, the ratchet workloads and ceilings**
   (I11, section 4), and the release: SampleRateTap 0.9.0 (D13: the three umbrella headers
   and the root `project()`), with its tested-pairs row; the maintainer tags `v0.9.0`.

**The order (decided, v7; DspTap's part decided v8).**
- **In SampleRateTap,** after A13d, so every new type carries its channel count from the
  start, and after the ×4 / ×8 / ×16 plan's X-a and X-b: that plan's breaking 0.8.0 (decision
  6 reopened, the spelling rule X0, D19) lands first. This plan's stages then take X0's
  spelling and record their baselines and ceilings against the final matrix, once.
- **In DspTap, one 0.3.0** (the maintainer's choice, v8) carries IIR-a and that plan's
  round-trip scaffold (X-c), in whichever order they are ready; the scaffold does not wait
  for SampleRateTap. The second to land bumps the version, and the maintainer tags it.
- **What it costs:** IIR-b and TapTools' round trip both start at the tag, so each waits for
  the later of the two PRs, and IIR-0 is on IIR-a's path. The scaffold's PR can land first
  and wait.

## 6. Risks

- **Instructions, not MACs**: the serial recursion may vectorize worse than the FIR dot;
  the ratchet decides what the README claims, per target.
- **Float at 120 dB**: a recursive structure's rounding noise may sit above −120 dB in
  float; measured in IIR-0, and `transparent` not offered where it fails (I14).
- **Phase surprises**: a caller expecting the FIR's linear phase. The variant is in the
  type name, and the README states the group delay as numbers.
- **The near-linear design**: no design exists yet, and an optimizer's result depends on its
  start. IIR-0 must produce one before IIR-a (section 5); committed tables (I4) make it
  reproducible, and the generator is in the tree.
- **The shared DspTap 0.3.0**: the two plans' consumers wait for the later of IIR-a and the
  scaffold (section 5). Accepted by the maintainer (v8).
- **A consumer who builds with `-ffast-math`** (or MSVC's `/fp:fast`, or icx's default
  `-fp-model=fast`). The headers compile with the consumer's flags, so these contracts hold
  only without it.
  - Reassociation and contraction move float outputs beyond the golden leg's tolerances,
    which are set without it.
  - `-ffinite-math-only` lets the compiler delete `std::isnan` and `std::isfinite` checks,
    so I9's non-finite behaviour no longer holds.
  - An executable linked with it starts with FTZ and DAZ on, through GCC's `crtfastmath.o`.
    Before GCC 13 a shared library built with it did the same to its whole host when
    loaded.

  The family builds and tests without it, and DspTap's pinned targets pass
  `-ffp-contract=off` (`tests/CMakeLists.txt`). *Recommended:* say so in each README's
  honest limits. The guard (I9) does not rely on it either way.

## 7. Sources (published literature only)

- R. A. Valenzuela and A. G. Constantinides, "Digital signal processing schemes for
  efficient interpolation and decimation," *IEE Proceedings G*, 130(6), 1983.
- M. Renfors and T. Saramäki, "Recursive Nth-band digital filters, Parts I and II,"
  *IEEE Trans. Circuits and Systems*, CAS-34(1), 1987 (the door for ↑3 / ↓3).
- L. Gazsi, "Explicit formulas for lattice wave digital filters," *IEEE Trans. Circuits
  and Systems*, CAS-32(1), 1985.
- P. P. Vaidyanathan, *Multirate Systems and Filter Banks*, Prentice Hall, 1993 (IIR
  power-symmetric filters and their allpass decomposition).
- f. harris, *Multirate Signal Processing for Communication Systems*, 2004 (2nd ed. 2021),
  the chapter on recursive polyphase filters; with M. d'Oreye de Lantremange and
  A. G. Constantinides, "Digital signal processing with efficient polyphase recursive
  all-pass filters," International Conference on Signal Processing, Florence, 1991.
- A. Krukowski and I. Kale, *DSP System Design: Complexity Reduced IIR Filter
  Implementation for Practical Applications*, Kluwer, 2003 (near-linear-phase two-path
  designs, fixed-point implementation).
- L. B. Jackson, "An analysis of limit cycles due to multiplication rounding in recursive
  digital (sub)filters," Allerton Conference, 1969.
- L. B. Jackson, "On the interaction of roundoff noise and dynamic range in digital
  filters," *Bell System Technical Journal*, 49(2), 1970 (scaling by L1, L2 and L∞ norms).
- Chebyshev II prototypes and the bilinear transform: A. Antoniou, *Digital Signal
  Processing*, McGraw-Hill, 2006.
- f. harris, "A most efficient digital filter: the two-path recursive all-pass filter," in
  R. G. Lyons (ed.), *Streamlining Digital Signal Processing: A Tricks of the Trade
  Guidebook*, IEEE Press / Wiley, 2007, chapter 9, pp. 85–104 (the details as DSPRelated
  cites them; to be checked against the book).
- **Design scripts, consulted and never ported** (v4): f. harris, `tony_des2.m` with its
  helper `tonycxx.m`. They are the MATLAB designer for two-path recursive all-pass filters
  from *Multirate Signal Processing for Communication Systems*'s companion code, headed
  "Script file written by fred harris of SDSU. Copyright 2002", and reposted on DSPRelated
  in 2020 (thread 10416, "fred harris polyphase recursive all-pass filter function
  tony_des2").
  - They carry a copyright and no license, so nothing of theirs enters the tree. The
    designer is written from the method their own header cites (Valenzuela and
    Constantinides, above) and the papers here.
  - They serve IIR-0 as an independent check of the coefficients, run outside the tree by
    the maintainer, with only the comparison recorded.
  - **The clean-room record (v8),** as DspTap's `NOTICE.md` keeps one for #39 and #42: IIR-a
    records who has read the scripts (this plan's drafting read their header and the
    DSPRelated thread), whether IIR-a's implementer may, or the maintainer's judgement in
    their place as #42 did; and it traces every formula in the constexpr designer,
    including the prototype's nome series, to Valenzuela and Constantinides, Gazsi or
    Vaidyanathan, not to the scripts.

## 8. Questions for the maintainer

Decided:
1. The names (I2): the shape is decided (v5: one stage template, the variant a type
   parameter); the names themselves are chosen in IIR-b.
2. Decided (v5): compile-time design for the nonlinear variant and the Chebyshev II guard
   band filter, committed tables for the near-linear variant (I4); v8 adds the linear
   guard band form to the committed tables.
3. Decided (v6): the latency contract (I5). FIR stages and all-FIR chains keep their exact
   fraction of frames. A chain with an IIR stage reports a number of frames instead (a
   double):
   - the near-linear variant its delay path's length;
   - the nonlinear variant its delay at DC;

   each with how far the passband's delay strays from it, and the delay at any frequency on
   request (`group_delay_output_frames(f)`). v8 places the API in DspTap's chain (IIR-a).
4. Decided (v5): an order per profile (I7). Decided (v7): both forms, the Chebyshev II and
   the linear-phase type II FIR, the phase a type parameter (I7).
5. Decided (v7, restated v8 by the audit): the round trip's phase error is at most 0.57°, so
   a change cancels to −40 dB. It is the sum over every IIR stage, down and up: the
   decimator and the interpolator of a half-band share one H(z), so their errors add, and a
   ×8 round trip at 0.57° per stage could reach 3.4° (−24.5 dB). Each stage's share is
   derived at its divisor (an even split is about 0.1° per stage at ×8; relaxed early stages
   see the band lower in their own passband, so they need less), and the quantity pinned is
   phase, per stage and per round trip, not group delay. An IIR round trip's pinned floor is
   −40 dB, against the FIR round trip's −60 dB, and the tests state each stage kind's
   threshold (the ×4 / ×8 / ×16 plan's 6.5 and 6.6). It sets the variant's section count,
   which IIR-0 pins (and its go/no-go list may tighten it).
6. Decided (v5): pin the poisoning as an honest limit (I9).
7. Decided (v5): "more than 10 % cheaper, or a latency-first consumer" is the bar (I12),
   measured on 2.4's spec.
8. Decided (v4): the scoped flush-to-zero guard, conditional, restoring what it changed (I9),
   chosen per target and type (v8). Open again only if IIR-0's Apple-silicon or Hexagon
   numbers say otherwise.
9. Resolved (v5): the scripts are the DSPRelated ones, cited in section 7.
10. Decided (v8): one DspTap 0.3.0 with the ×4 / ×8 / ×16 plan's scaffold (section 5).

Recommended, not yet put to the maintainer (your word settles each):
11. I3: the same four profiles at the same numbers, a profile not offered where a variant
    cannot reach it.
12. I6 (restated): `window_frames()` is the count to an exactly zero state, so DspTap's flush
    contract holds; the flush's output grows by that count.
13. I8: two integer guard bits in every state, direct form I biquads ordered by pole radius,
    L∞ scaling.
14. I10 and I11: composition through `tap::dsp::chain`, new C ABI entry points, the notebooks.
15. I13: relaxed IIR designs per divisor, and a power-of-2 cascade helper.
16. I14: float states and products in the float profile, `transparent` dropped in float if
    it cannot reach 120 dB.
