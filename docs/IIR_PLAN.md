# Recursive polyphase stages for `rational` (the IIR options): plan

Status: **v11, 2026-11-22: IIR-a and IIR-b landed, IIR-c in review.** v11 records the
maintainer's decisions for IIR-c, taken with it, and IIR-c's own results (`rational`'s guard band
filter and the composition tests, section 5 item 4):
- **Question 14, the order rule: decided, 5 / 7 / 7 / 7** at `super_economy` / `economy` /
  `balanced` / `transparent`: the lowest odd Chebyshev II order whose worst alias in the guard band
  is at or below −A/2 (I7). The linear form matches the same stop edges, from DspTap's committed
  `k_guard_band_fir<A, num, den, Order>` rows at those orders.
- **Question 19, the floating limit cycle: decided, an honest limit** (I6), for the Chebyshev II at
  orders 7 and above and for IIR-b's near-linear `transparent` stage at divisor 1. The hot loops
  stay as they are and DspTap does not change: no deadband and no dropped designs. The floating
  state settles below a pinned bound in multiples of the smallest normal; `flush()` equals zero
  padding bit for bit for the stage itself, and within the residue in a chain with stages after a
  cycling one; everywhere else it is exact, and always in fixed point.
- **Question 16, I10's part: decided as recommended.** The guard band filter and the recursive
  stages satisfy `sync_stage`; a mixed chain is a caller's `tap::dsp::chain`; no named IIR chains
  and no coverage-matrix rows; a small offering table in the README and the book. I11 (the C ABI,
  the binding, the notebooks, the ratchet workloads and ceilings) and the 0.9.0 release stay
  IIR-d's.
- **IIR-c** (`rational/include/tap/sr/rational/guard_band.h`, `rational/PLAN.md` R26–R28):
  `guard_band_filter<S, Ch, guard_phase>` over `guard_band_filter_base<S, P>`, both forms in every
  format, the settings, the golden leg and the battery, and the composition tests (section 5 item
  4 has the numbers). Three findings:
  - **The order rule is the nonlinear half-band's.** Behind the near-linear and the FIR half-band
    the same orders leave the guard band's worst alias at −31.4 / −31.6 dB at `super_economy` and
    −48.8 / −45.6 dB at `transparent`, above −A/2 (the rule would pick 7 / 7 / 7 / 11 behind
    either). The passband and the band past the stop edge keep their contracts behind every
    half-band. Put to the maintainer (question 20).
  - **The linear form in Q15 misses the 70 dB tier:** its Q1.14 taps attain −67.1 / −66.8 /
    −64.6 / −70.9 dB from the stop edge (Q31 keeps the designs). Pinned as an honest limit; at Q15
    the Chebyshev II form holds the tier. Put to the maintainer (question 21).
  - **DspTap's `iir_kernels.h` says the allpass sections "reach an exactly zero state"** under
    flush-to-zero; IIR-b's near-linear `transparent` stage at divisor 1 does not. A DspTap
    follow-up, not edited here.

v10 records IIR-a as it landed in
DspTap (tap/DspTap#68, tagged `v0.3.0`), the maintainer's answers that settled I3, I6, I8, I13,
I14 and the names for IIR-b, and IIR-b's own results (`rational`'s stages, section 5 item 3):
- **IIR-a's findings** (DspTap's `iir_design.h`, `iir_kernels.h`, `NOTICE.md`):
  - every committed L∞ section gain of the Chebyshev II is exactly 1.0: with the sections ordered
    by pole radius, each at unity DC gain, every partial cascade peaks at DC (I8);
  - the floating Chebyshev II cascade of order 7 or more holds a zero-input limit cycle at a few
    times the smallest normal under flush-to-zero (up to 13.1 times from full-scale starts), so I6's
    exactly zero window cannot hold for the floating guard band filter at those orders: IIR-c's to
    settle. The half-bands' first-order sections drain; IIR-b found one near-linear design that does
    not (below);
  - the nonlinear half-band's design takes two algebraic rewrites beyond IIR-0's nome step (the last
    step's 1 − x and the band edge's 1 − w² / k), bringing every tabulated coefficient within 13.20
    ulps of the mpmath leg (15.4 without the last);
  - the constexpr math's exact product (`detail/cx_math.h`, under the constexpr sqrt) is formed in
    integers: GCC's FMA contraction changed Dekker's two-product, whose split product is inexact by
    design, and put sqrt an ulp from libm; the Q31 kernels' 96-bit products are integer too;
  - the constexpr designs stay small: 11,090 Clang constant-evaluation steps for the order-17
    half-band, the largest tabulated, against MSVC's documented default of 100,000.
- **IIR-b** (`rational/include/tap/sr/rational/iir_stage.h`, `iir_chain.h`): the names
  (`basic_iir_stage<S, Ch, R, P>`, `iir_phase`, `iir_stage_base<S, R, P>`, `iir_converter`,
  `iir_converter_near_linear`, `iir_down_by`, `iir_up_by`), both variants ↑2 / ↓2 at every profile
  and the design divisors 1, 2, 4 and 8, the cascades by 2 to 16, the golden leg, the battery and
  the documents. Two findings:
  - **the four-path stage does not ship** (I12): measured on the shipping machine over DspTap's
    kernels, it does not beat the two half-bands it would replace by R3's 10 % on all three
    targets (2.4);
  - **one design holds a limit cycle under flush-to-zero** (I6): the floating near-linear stage at
    `transparent` and divisor 1 (order 19) settles at about four times the smallest normal, so its
    state never reaches exactly zero, for the reason IIR-a found in the Chebyshev II. It is pinned as
    an honest limit and put to the maintainer (question 19).

v9 records the design spike
IIR-0 (SampleRateTap#104; every number below marked **IIR-0** is in
`rational/tools/spikes/iir/README.md` with the program that produced it) and the
maintainer's answers to it:
- **Flush-to-zero is the host's job** (I9, reversing v4's guard): the plan documents what to
  set on every target, each IIR stage checks it as a debug-build precondition, and where no
  bit exists the residue it leaves is an honest limit.
- **The round trip's target is per profile** (question 5): −60 dB at the three 70 dB
  profiles and −100 dB at `transparent`, which IIR-0's designs reach at no extra sections.

What else v9 changes, from IIR-0's go/no-go list (section 5):
- **The near-linear variant is feasible at all four profiles** (2.2), at 1.25 to 2.4 times
  the nonlinear variant's multiplies.
- **Float reaches 120 dB** with float states (I14).
- **Fixed point:** the rounding is truncation, not round-to-nearest, which leaves limit
  cycles; and one difference node needs a third integer bit, or the accumulator (I8).
- **The four-path ↓4 stage joins IIR-b** at the placements where it passes I12's bar on
  instructions (2.4, I12).
- **Compile-time design holds,** with two amendments (I4).
- **The guard band filter's order** gets a rule (I7), and the flush window a margin (I6).

v8 applied the adversarial
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
- **The flush-to-zero guard** (reversed in v9: the host's job, I9) is chosen per target and sample type (double is software on
  the M33 and M4F), uses compiler builtins rather than CMSIS, and restores only the control
  bits.
- **Corrected numbers:** the IIR group delays (2.2), the FIR ↑2 column (2.2), the cost of
  filtering before the decimator (2.3), the FIR half-band's relaxation (2.4) and the
  four-path bar (2.4).
- **The spike gets a go/no-go list** and commits its programs (section 5).

Decided: the scope, the formats and the release shape (section 1); the substrate in DspTap
0.3.0 (I1); the stage's shape and, since v10, its names (I2); the profiles (I3, v10);
compile-time design (I4); the latency contract (I5); the flush window (I6, v10); the guard band
filter's name, an order per profile and both forms (I7); the fixed-point datapath (I8, v10);
non-finite input, and flush-to-zero as the host's job (I9, v9); the bar for a four-path stage
(I12), and IIR-b's measurement against it (v10); the relaxed designs and the cascade helper (I13,
v10); float states in the float profile (I14, v10); the round trip's target (question 5, v9); the
order (section 5); I7's order rule (question 14, v11), I10 (question 16's part, v11) and the
limit cycle (question 19, v11). Recommended and not yet put to the maintainer: I11, listed in
section 8; open: questions 20 and 21 (v11). Numbers marked *est.* are arithmetic; numbers marked
**IIR-0** are the spike's measurements, **IIR-b** the shipping stages', **IIR-c** the guard band
filter's.

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
  release's critical path. IIR-0 found a design at every profile, `transparent` included
  (2.2).
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

### 2.2 Sizes against today's FIR half-band (*est.*, then **IIR-0**)

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
realize; **IIR-0** confirms 2.40 … 4.96 and 4.16 … 11.92.)

**The near-linear variant (IIR-0).** One path a pure delay, the other an allpass of order N
(in z²) that approximates it; N is also its multiplies per decimated output:

| profile | allpass order N | sections (second + first order) | stopband | passband delay (high-rate samples) |
|---|---:|---|---:|---:|
| `super_economy` | 5 | 2 + 1 | −75.5 dB | 10 |
| `economy` | 7 | 3 + 1 | −77.5 dB | 14 |
| `balanced` | 8 | 4 + 0 | −74.6 dB | 16 |
| `transparent` | 19 | 9 + 1 | −121.2 dB | 38 |

Its passband delay strays at most 0.027 samples from those numbers, below the FIR's flat 21
and 61. That is 1.25 to 2.4 times the nonlinear variant's multiplies (v8 guessed two to
three), and the only active constraint is the stopband.

**Instructions (IIR-0).** Against today's FIR stage, the IIR stage counts 3 to 7 times fewer
instructions in Q15, and in float 113 times fewer on the M33, where the FIR's float path
accumulates in soft double. In the IIR stages themselves, fixed point counts more
instructions than float on the M33, M55 and Hexagon.

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
  that folds into the guard band. Matching the after-filter needs order 15 at twice the
  rate, six times the MACs (**IIR-0**). And an odd order there puts its zero at the
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

### 2.4 Factor 4 inside a chain: one four-path stage or two half-bands (v2; **IIR-0**)

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
per output frame per channel (decimating), against the cheapest FIR chain measured.
The relaxed rows need I13's designs per divisor; without them every stage runs the
profile's full design. **IIR-0** confirms the orders above and these counts, and adds the
near-linear rows:

| profile | ×2 | ×4 | ×8 | ×16 |
|---|---:|---:|---:|---:|
| IIR half-band cascade, `economy`, relaxed (I13) | 4 | 8 | 16 | 24 |
| IIR half-band cascade, `economy`, unrelaxed | 4 | 12 | 28 | 60 |
| near-linear cascade, `economy`, relaxed (I13) | 7 | 13 | 21 | 29 |
| near-linear cascade, `economy`, unrelaxed | 7 | 21 | 49 | 105 |
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
- The early half-bands are already nearly free (one or two coefficients each), so v8
  expected the cascade to stay cheaper in MACs for IIR.

**What IIR-0 measured** (nonlinear variant):
- **MACs: at best a tie.** Six sections against the pair's 12 MACs at the ×8 top and ×16
  middle placements, for all three 70 dB profiles; four sections at `super_economy`'s ×16
  top. It fails at ×4 for every profile and everywhere at `transparent`.
- **Group delay:** the same as the pair's within 0.04 frames.
- **Instructions:** against the pair it replaces at `economy`, −23 / −21 / −32 % in float
  and −13 / −17 / −22 % in Q15 on the M33 / M55 / Hexagon. Four independent recursions
  interleave where a cascade's serial ones cannot.

So it passes I12's bar on instructions at those placements and joins IIR-b (I12). The
margin is between the spike's own kernels, and Q15 on the M33 is near the bar, so IIR-b's
ratchet confirms it on the shipping code before it is offered. The near-linear four-path
stage was not measured.

**What IIR-b measured on the shipping code** (`rational/tools/spikes/iir/iirb_fourpath/`, its
README and `outputs/`): the four-path stage in IIR-b's draft of `basic_iir_stage`, over DspTap
0.3.0's kernels, against the pair it replaces (↓2 at divisor 4 then at 2), 2 s of stereo at
48 kHz in 32-frame blocks, streaming instructions, `economy` (the other 70 dB profiles within
0.01 % of it); negative is fewer:

| target | float ↓ | Q15 ↓ | float ↑ | Q15 ↑ | float in ×8 ↓ | Q15 in ×8 ↓ |
|---|---:|---:|---:|---:|---:|---:|
| M33 | +10.7 % | +14.6 % | +2.1 % | −14.7 % | +8.9 % | +12.1 % |
| M55 | +9.2 % | −10.1 % | −1.2 % | −5.0 % | +7.5 % | −8.3 % |
| Hexagon | −19.5 % | −24.1 % | −7.7 % | −10.7 % | −16.2 % | −19.7 % |

No format and direction clears the bar on all three targets at either placement (the ×16 middle
replaces the same pair at the same divisors, with smaller shares: +4.2 / +3.6 / −9.4 % in float
going down), so **the four-path stage does not ship** and every cascade is half-bands (I12).
IIR-0's kernels had the section counts as compile-time facts; DspTap's are generic over them, and
on the in-order Arm cores the four-path stage pays four path set-ups and its path array per
output for one or two sections each. A four-path stage that passed there would need a kernel
specialized to its [2, 2, 1, 1] layout in DspTap.

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
  documented Q formats), `subnormals_flushed<S>()` (I9) and the chain's latency members (I5)
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
- **I2. The public shape. *Decided (v5);* the names *decided (v10, the maintainer):*
  `basic_iir_stage<S, Ch, R, P>` with `P` an `iir_phase` (`nonlinear`, `near_linear`), its
  count-independent base `iir_stage_base<S, R, P>`, the aliases `iir_converter<Ch, R>` (nonlinear)
  and `iir_converter_near_linear<Ch, R>` with their `_q15` / `_q31` siblings, and the cascades
  `iir_down_by<S, Ch, P, D>` / `iir_up_by<S, Ch, P, D>` (I13). R is `up2` / `down2`: the four-path
  stage did not pass I12 (2.4), and the `static_assert`'s message names ↑3 / ↓3 as planned and
  ↑4 / ↓4 as measured.** A stage
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
- **I3. Profiles.** *Decided (v10, the maintainer), as recommended; IIR-b: every profile reaches
  both variants, at every divisor 1, 2, 4 and 8.* The same four names at the same (A, passband edge)
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
    - **IIR-0:** GCC 13 and Clang 18 give identical bits for all 36 tables (the nonlinear
      half-bands at every profile and divisor, the Chebyshev II at orders 3 to 11). The N17
      half-band takes 30,487 Clang constexpr steps, an order-11 Chebyshev II 21,451 with
      unity gain per section. MSVC's documented default is 100,000.
    - **Two amendments (v9, from IIR-0):**
      - the prototype's nome step lost up to 94,049 ulps on the relaxed designs, through
        cancellation; an algebraic rewrite brings every table within 15 ulps of mpmath, and
        it is the form IIR-a ships;
      - the Chebyshev II's L∞ gain distribution (I8) takes 414,830 steps, about four times
        MSVC's default, so it leaves constexpr: a `tools/` script computes the section
        gains and commits them beside the design, pinned by a test, while the poles and
        zeros stay constexpr.
    - **Still to run:** MSVC (CI's Windows leg, `/W4 /WX`) and AppleClang. IIR-a moves the
      spike's bit test (`rational/tools/spikes/iir/constexpr/`, a self-contained CMake
      project) into DspTap's battery, so CI's Windows and macOS legs run it.
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
  in `rational`. A recursive stage's tail never ends on its own. *Decided (v10, the
  maintainer), as recommended (v8, restated by the audit):* `window_frames()` is the count after
  which the state is **exactly zero**, plus one frame per section, so the contract holds
  unchanged. IIR-b implements it with DspTap's `floating_flush_frames` (floating point) and, in
  fixed point, IIR-0's bound for the first-order sections (per path 1 plus each section's longest
  floor-truncated decay from the full headroom) and twice a per-section radius bound for the
  near-linear second-order ones; `rational.iir_fixed_test.FixedPointSilenceIsExactlyZeroAfterTheWindow`
  drives every fixed-point stage at full scale and finds every state zero inside its window.
  **IIR-b's finding:** the floating near-linear stage at `transparent` and divisor 1 holds a
  zero-input limit cycle at 3.7 to 4.1 times the smallest normal under flush-to-zero (in float
  and double, from 12 to 20 of 39 noise starts): a second-order section's product falls below the
  smallest normal and flushes, and the section, run without it for that sample, lifts the state
  again, IIR-a's Chebyshev II mechanism. Every other stage drains to exactly zero. Pinned as an
  honest limit (the residue within eight times the smallest normal; a chain whose later stages
  follow that one flushes within it of zero padding), and question 19.
  *Decided (v11, the maintainer, question 19): keep the honest limit,* for that stage and for the
  floating Chebyshev II of the guard band filter at orders 7 and above; no deadband, no dropped
  design, no DspTap change. **IIR-c:** the floating Chebyshev II at orders 7 and above holds the
  cycle from some states (8 to 16 of 20 starts at the named settings), its state settling at most
  8.7 times the smallest normal in float and 10.4 in double, pinned below 16; orders 3 and 5 drain
  to exactly zero at every named spec point; the fixed-point cascade always drains, within 321
  (Q15) and 691 (Q31) frames of full-scale drive against windows of 420 to 2,130.
  - **Floating point:** the count from the format's largest finite value (over the state
    headroom) down to its smallest normal value, through the stage's largest coefficient,
    computed at design time, plus a margin of one frame per section (IIR-0 found a whole
    stage needs about one frame more than the formula). Below the smallest normal value the
    host's flush-to-zero (I9) zeroes the state. **IIR-0:** 853 low-rate frames in float and
    6,867 in double at `economy`, 2,080 and 16,752 at `transparent`; the flush's output
    length grows by that much.
  - **Where the host has not flushed subnormals,** or the target has no bit for that type
    (I9), the state does not reach zero: after the window the output is at most a few
    multiples of the smallest subnormal (about 10⁻⁴⁵ in float, 10⁻³²³ in double), not
    silence. The contract states it as an honest limit, and a test pins the bound.
  - **Fixed point:** the count from the full state headroom (I8) to exact zero under the
    chosen rounding. **IIR-0:** 117 frames in Q15 and 240 in Q31 at `economy`, 308 and 647 at
    `transparent`.
  - `FlushEqualsZeroPaddingBitForBit` runs over chains holding IIR stages and both guard
    band forms, with flush-to-zero set as a host would. Non-finite input is the exception,
    as I9 states.
- **I7. The guard band filter's shape. *Decided (v3, v5, v7).*** A 1:1 stage of its own,
  `guard_band_filter<S, Ch, guard_phase>` (the maintainer's name), that a caller places in a
  chain, before an interpolator or after a decimator, FIR or IIR. It is designed from the
  profile's passband edge, an order and A, and its stopband edge follows (2.3). **An order
  per profile** (v5): `guard_band_settings::economy()` and its siblings, and a custom form.
  *Decided (v11, the maintainer, question 14), as recommended (v9, IIR-0):* the rule that sets
  it is the lowest odd order whose worst alias in the guard band is at or below −A/2, which gives
  orders 5 / 7 / 7 / 7 at `super_economy` / `economy` / `balanced` / `transparent`.
  **IIR-c** recomputes it from the designs behind the nonlinear half-band (−40.1 / −43.5 / −40.2
  / −61.6 dB at those orders; `rational.guard_band.OrderIsTheLowestOddOrderWhoseGuardBandAliasIsAtHalfA`)
  and finds it is that half-band's rule: behind the near-linear and the FIR half-band the same
  orders leave −31.4 / −39.2 / −37.8 / −48.8 and −31.6 / −37.0 / −37.0 / −45.6 dB, and the rule
  would pick 7 / 7 / 7 / 11 behind either (question 20). The Chebyshev II form takes odd orders only. DspTap's `chain`
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
    as biquads with b0 = b2 plus one first-order section; **IIR-0** confirms 40 / 50 / 64
    taps):

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
- **I8. Fixed point.** *Decided (v10, the maintainer), as IIR-a implemented it in DspTap's
  `iir_kernels.h`:* floor truncation in the one-multiplier sections, magnitude truncation in the
  second-order ones, two integer guard bits (Q15 in Q2.29 states, Q31 in Q2.60), the difference
  nodes in the accumulator, saturation at the output only. **IIR-b** measured on the stages: the
  largest state under full-scale square and clipped drive 1.99 of full scale in Q15 and in
  Q31 (limit 4), every output within 2 LSB of the double stage clamped (Q15; 1e-8 in Q31), the
  Q15 floor −100.8 dB RMS against double, DC at full scale within 1 LSB. *Recommended* (v9: rounding
  and headroom from IIR-0):
  - **Formats:** Q15 samples with Q31 coefficients (every aₖ is in (0, 1)); Q31 samples with
    64-bit states; the biquads in Q2.30 coefficients (|a₁| < 2); 64-bit accumulation, one
    rounding point per section, saturation at the output, all stated as numbers in the
    header.
  - **Headroom.** An allpass keeps energy, not peak level: inside a path, a node can exceed
    the input's full scale. The stored states' L1 norms reach 2.11 and 2.69 at `economy` and
    3.06 and 3.71 at `transparent` (**IIR-0** confirms 3.71 at most), and a full-scale
    square wave drives a state to 1.93. So every state carries **two integer guard bits**
    (|state| < 4): Q15 samples run in 32-bit Q2.29 states, Q31 samples in 64-bit states with
    the same two bits.
    - **The difference node needs more (IIR-0):** in the one-multiplier section the
      difference x − y₁ reaches 5.22 at `transparent`. It is formed in the 64-bit
      accumulator, never stored, so the states keep two guard bits (the alternative, three
      integer bits in every state, Q3.28 / Q3.60, costs a bit of precision everywhere).
  - **The biquads:** direct form I in fixed point (one rounding point, at the section's
    output, through the 64-bit accumulator), transposed direct form II in float; sections
    ordered by increasing pole radius, with the gain distributed by the L∞ norm (Jackson,
    1970). The wrong order lets partial cascades gain +5.7 to +11.1 dB; this one keeps them at
    or below 0 dB (*est.*).
  - **Limit cycles (IIR-0).** Recursive fixed point can hold a zero-input limit cycle.
    IIR-0 searched every section of every design exhaustively (complete by Jackson's 1969
    bound):

    | rounding at each section's output | first-order sections with a cycle | second-order sections with a cycle |
    |---|---:|---:|
    | round to nearest (any tie rule) | 44 of 74 | 65 of 88 |
    | floor truncation | 0 of 74 | 28 of 88 |
    | magnitude truncation | 0 of 74 | 0 of 88 |

    So round-to-nearest is out. *Recommended:* floor truncation for the one-multiplier
    allpass sections (no cycle, provably, and about a quarter fewer Q15 instructions than
    magnitude truncation), magnitude truncation for the guard filter's biquads. A test pins
    that silence after signal decays to exact zero within the stated count (I6).
  - **The floors (IIR-0):** the Q15 floor equals the output's own rounding.
  - **Tests:** full-scale square and clipped input through every profile with no internal
    saturation; the limit-cycle decay; unity gain and the Q15 floors.
  - **Cost (IIR-0):** a Q31 section is a 64 × 32 multiply, and fixed point counts more
    instructions than float on the M33, M55 and Hexagon (`outputs/` of the spike).
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
  - ***Decided (v9, the maintainer): flushing subnormals is the host's job.*** No IIR stage
    reads-and-writes or restores the floating-point control register. The bit belongs to
    the thread, and so to the host: most audio hosts already set it on their audio threads,
    and a library that set it would have to restore it on every call. This reverses v4's
    scoped guard; what v4 measured stays below as the record of why the choice matters.
  - **What the plan documents, per target.** IIR-a writes it in DspTap's README and the IIR
    headers; IIR-b repeats it in `rational`'s README and the book's Part VII:

    | target | what the host sets | the arithmetic it governs | how |
    |---|---|---|---|
    | x86-64 | MXCSR FTZ (bit 15) and DAZ (bit 6) | float and double (SSE) | `_mm_setcsr(_mm_getcsr() \| 0x8040)` (`<xmmintrin.h>`, GCC, Clang and MSVC), on each audio thread before it runs |
    | AArch64 (Apple silicon, Arm Linux, Windows on Arm) | FPCR.FZ (bit 24) | float and double | `msr fpcr` (inline assembly under GCC and Clang, `_WriteStatusReg` under MSVC), on each audio thread |
    | Cortex-M with an FPU (M4F, M33, M55), AArch32 | FPSCR.FZ (bit 24), and FPDSCR (0xE000EF3C) so exception handlers, where MCU audio often runs, start with it | float on the M4F and M33; float and double on the M55 | `__builtin_arm_set_fpscr` (or CMSIS's `__set_FPSCR` in firmware that has CMSIS), at reset |
    | Cortex-M without an FPU (M4), and double on the M4F and M33 | nothing: software float | — | an honest limit (below) |
    | Hexagon | none modelled by QEMU (**IIR-0**) | — | IIR-a checks the vendor's Programmer's Reference Manual for the documentation; until then an honest limit |

  - **What a host that does not set it gets** (honest limits, stated and pinned):
    - **CPU:** on x86 and some A-class Arm cores, silence after signal runs 9 to 11 times
      slower once the states go subnormal (**measured**, v4: 6.9 µs instead of 0.6 µs per
      32-frame block);
    - **Output:** the state sticks at k times the smallest subnormal, as above, so the output
      after the flush window is at most that, not silence (I6).
  - **A debug-build precondition (v9, the maintainer's choice).** Each IIR stage's
    `process()`, `pull()` and `flush()` asserts
    `TAP_EXPECTS(tap::dsp::subnormals_flushed<S>())` on targets where a bit governs S's
    arithmetic, and compiles nothing elsewhere. `TAP_EXPECTS` is `assert` by default, so the
    check runs in Debug builds and costs nothing in Release; a consumer may substitute its
    own policy (`detail/expects.h`). `subnormals_flushed<S>()` (placeholder name) is
    DspTap's and public, so a host and its tests can ask it too. A test that pins the
    no-FTZ behaviour defines `TAP_DSP_ALLOW_SUBNORMALS` (placeholder) to compile the check
    out.
  - **Tests:**
    - the IIR tests set FTZ (and DAZ on x86) in their fixture's set-up and restore it in its
      tear-down, as a host would, so the rest of the battery on the same thread is untouched;
      the QEMU legs do the same around each IIR test;
    - `IirStage.SilenceDecaysToExactZero`: under FTZ, within I6's count, for float and
      double on every leg where a bit governs the type;
    - `IirStage.WithoutFlushToZeroTheResidueIsBounded`: the honest limit, built with the check
      compiled out, on every leg (double on the M33 and Hexagon included);
    - `IirStage.NeverTouchesTheFloatingPointEnvironment`: the control bits are the same
      before and after every call, in both host states;
    - the precondition fires in a Debug build when FTZ is off, tested the way DspTap tests
      its other `TAP_EXPECTS` preconditions.
  - **The record of the declined guard (v4's measurements, x86-64, a shared Xeon VM at
    2.1 GHz, indicative):** setting and restoring the bits unconditionally cost +5.6 to
    +6.7 ns per call; the conditional form cost about 0 when the host had them set; the
    per-section flush cost 18 to 50 % of a block (+104 % on Hexagon, **IIR-0**). *Declined
    (v9):* the scoped guard (v4), the per-section flush, flushing once per call (+2 % on
    Hexagon, but it makes output depend on block size, breaking bit-identical chunking), a
    flush on a fixed schedule (unmeasured), and an add-and-subtract offset (+65 % on
    Hexagon).
  - The fixed-point profiles have no subnormals; their counterpart is the zero-input limit
    cycle (I8).
- **I10. Composition.** *Decided (v11, the maintainer, question 16's part), as recommended;
  IIR-c implements it (`rational/tests/test_iir_composition.cpp`, `rational/PLAN.md` R27).* The
  recommendation, as decided: the IIR stages and the guard band filter satisfy
  `sync_stage`, so a caller's `tap::dsp::chain` mixes them with FIR stages (an IIR ↑2 and a
  FIR ↑3, a guard band filter before either). `rational`'s `basic_chain` takes ratio types
  only, so a mixed chain is a `tap::dsp::chain`, and it does not relax its stages: the
  caller builds each stage at `profile.relaxed(d)`, or uses I13's helper for a power-of-2
  cascade. No named IIR chains and no coverage-matrix rows at first; a separate small table
  states the offering: ↑2 and ↓2 × four profiles × two variants, with no guard band filter,
  the Chebyshev II form, or the linear form. **IIR-c:** the table is in `rational`'s README and
  the book's Part VII, each cell (multiplies per output, the nominal latency and its deviation)
  pinned by `rational.iir_composition.TheOfferingTableIsMeasured`; at `economy` going down the
  nonlinear stage costs 4 / 24 / 54 multiplies per output with no filter / the Chebyshev II /
  the linear form, the near-linear one 7 / 27 / 57.
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
        six times as much (IIR-0);
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
  the ×4 / ×8 / ×16 plan's X3, and the result is recorded with its numbers.
  - **IIR-0's result (2.4):** at best a tie on MACs, but 13 to 32 % fewer instructions than
    the pair it replaces at the ×8 top and ×16 middle placements, at the three 70 dB
    profiles. It passes the bar on instructions there, so the nonlinear four-path ↓4 / ↑4
    joins IIR-b, offered only where IIR-b's ratchet confirms the margin on the shipping
    code (Q15 on the M33 is near the bar). ×4 stays two half-bands, and so does
    `transparent`. The near-linear four-path stage is not part of the release.
- **I13. Relaxed designs for cascades (v8, new).** *Decided (v10, the maintainer), as
  recommended; the helper is `iir_down_by` / `iir_up_by<S, Ch, P, D>`, D = 2, 4, 8 or 16 (I2),
  and its cascades cost 2.4's relaxed rows exactly (`rational.IirChain.CascadesCostTheRelaxedRow`).*
  An IIR stage, like an FIR
  stage, is designed at its design divisor d within a chain (its lower rate over the chain's
  lowest), with the passband edge fixed at p · r_min and the stopband mirrored about its own
  low Nyquist. Each profile's `relaxed(d)` has compile-time tables (I4) at the divisors a
  power-of-2 cascade uses (2, 4 and 8; the near-linear variant's committed tables likewise),
  and `rational` offers one helper (placeholder `iir_halfband_chain<S, Ch, P, D>`) that
  composes a power-of-2 cascade with each stage relaxed to its divisor, since a raw
  `tap::dsp::chain` does not. Without it, the cascades cost 2.4's unrelaxed row, 1.5 to 2.5
  times the relaxed one at `economy`.
- **I14. The float profile inside the recursion (v8, new).** *Decided (v10, the maintainer), as
  recommended below; IIR-b's float stages are within 3.8e-7 of full scale of the direct-form golden
  leg at every profile and divisor.* `sample_traits<float>` is float
  input and output with double accumulation, and DspTap runs numerically fragile recursions
  in double even in the float profile. On the M33 and M4F double is soft-float, about 19
  times a Q15 instruction count by `sample_traits.h`'s own comment. *Recommended:* the IIR
  stages' float profile keeps its states and products in float. The one-multiplier allpass
  section is structurally passive and insensitive to its coefficient, so it is not the
  fragile kind; the biquads run in float too. **IIR-0 confirms it:** the worst error with
  float states and products is −137.1 dB against full scale (`transparent`, near-linear,
  interpolating, white noise), so `transparent` stays offered in float, and float states
  count 1,038 instructions against 29,401 for double on the M33 and M4F, 28 times fewer.
  With float states, a host's FPSCR.FZ governs them on the M4F and M33 (I9).

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
  over the round trips the ×4 / ×8 / ×16 plan composes, with the round trip's cancellation
  pinned at −60 dB at the 70 dB profiles and −100 dB at `transparent` (question 5).
- **The relaxed designs** (I13): each divisor's table meets its spec, and the helper's
  cascade costs what 2.4's relaxed row says.
- **Latency** (I5): each stage's nominal latency and group delay against the golden leg; the
  chain's composed members; the near-linear delay path exact.
- **Flush** (I6): `FlushEqualsZeroPaddingBitForBit` over chains holding each IIR variant and
  each guard band form, with flush-to-zero set as a host would.
- **The guard band filter, per form**: its zero at Nyquist, its stopband from its stated
  edge, its fixed-point tables, that a decimating cascade's aliases land only above the
  passband edge (`guard_band.AliasesLandOnlyAboveThePassbandEdge`), and the combined response
  with an FIR and an IIR half-band; for the linear form also its symmetry and its exact
  latency inside an all-FIR chain.
- **Fixed point**: unity gain, the headroom (full-scale square and clipped input, no
  internal saturation), saturation at the output, the Q15 floors, the limit-cycle decay.
- **Subnormals and the environment**: I9's tests on every leg: exact silence under
  flush-to-zero, the bounded residue without it, the environment never touched, and the
  debug-build precondition.
- **The family's rules**: channels in the type; A12, K12 and D17 (I2), with rejection tests
  through `TAP_SR_EXPECT_REJECTED`; the pinned header count; both builds (exceptions and RTTI
  off); the QEMU legs; the ratchet with new workloads (↑2 / ↓2, float and Q15, both variants,
  `economy`, with no guard band filter and with each form, and the four-path ↓4 / ↑4 against
  the half-band pair at the placements I12 admits) under the family's ±3 %, recorded at the
  midpoint of the two builds where they split; and the size and stack ceilings.

## 5. The pull requests, in order

1. **IIR-0, the design spike: done** (SampleRateTap#104, `rational/tools/spikes/iir/`, and
   `rational/notebooks/iir_spike.ipynb`, the demonstration's first version, on Python
   designs). Its go/no-go list, answered:
   - **near-linear design:** go at all four profiles (2.2);
   - **the round trip's phase:** better than v8 estimated. The passband phase error equals
     asin of the stopband at the mirror frequency, whatever the weighting, 0.018° at 70 dB;
     relaxed round trips cancel to −61 to −71.5 dB at the 70 dB profiles (×2 to ×16) and −107
     to −115 dB at `transparent`. Question 5 is tightened to −60 / −100 dB (v9);
   - **float at 120 dB:** go (I14);
   - **limit cycles:** round-to-nearest leaves them; the rounding changes (I8);
   - **Hexagon:** no flush-to-zero control modelled. With I9 now the host's job, this
     becomes an honest limit there, not a library cost;
   - **the constexpr designs:** go, with two amendments (I4); MSVC and AppleClang still to
     run, in IIR-a's battery;
   - **the four-path stage:** passes on instructions at two placements, so it joins IIR-b
     (I12).

   Not measured: MSVC and AppleClang, Apple silicon, Hexagon silicon, cycle counts on any
   target, and the near-linear four-path stage.
2. **IIR-a, DspTap: landed** (tap/DspTap#68, DspTap 0.3.0 at bd9617a, tagged `v0.3.0`; its
   findings are v10's status above). The design math and kernels, the four-path
   ↓4 / ↑4 kernel (I12), `subnormals_flushed<S>()` and the per-target flush-to-zero
   documentation (I9), the chain's latency members (I5), their battery and fingerprints,
   the constexpr bit test from IIR-0 so CI's MSVC and AppleClang legs run it (I4), DspTap's
   checklist (I1), and the clean-room record in `NOTICE.md` (section 7). If it is the second of the
   two 0.3.0 PRs to land, it bumps `version.h` and `project()` to 0.3.0, and the maintainer
   tags the commit `v0.3.0`.
3. **IIR-b, `rational`'s ↑2 / ↓2 stages**, both variants, and the four-path ↓4 / ↑4 where I12
   admits it (I2–I6, I8, I9, I12–I14), with the
   golden leg, the README and the book's Part VII. *In review (v10):* the stages and the cascades
   by 2 to 16 as I2 names them; the four-path stage measured and not shipped (2.4); the golden leg
   (`rational/tools/reference/make_iir_reference_vectors.py`: each stage's transfer function in one
   direct form through `scipy.signal.lfilter`, 64 stages), met within 9.6e-14 (double), 3.8e-7
   (float), 1.5e-5 (Q15) and 5.2e-9 (Q31) of full scale, the Q15 and Q31 outputs bit-pinned; the
   stopbands at every profile and divisor −71.3 to −145.6 dB (each at least A + 1); the
   near-linear phase error per stage 0.0116° at the 70 dB profiles and 0.00005° at `transparent`;
   the round trip through `tap::dsp::subband_delta` at ×2 to ×16 leaving a removed tone −64.5 /
   −70.0 / −68.6 dB down at `super_economy` / `economy` / `balanced` and −114.8 dB at `transparent`
   (thresholds −60 and −100, question 5); without flush-to-zero the residue at most 7 times the
   smallest subnormal (bound 16); every FIR pin, ratchet count and ceiling unchanged (the DspTap pin
   move A/B'd exactly on the M33, M55, Hexagon and host in both builds). It also:
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
   (`ratio<L, M>`'s `static_assert` unchanged). *In review (v11):*
   `guard_band_filter<S, Ch, guard_phase>` (`rational/include/tap/sr/rational/guard_band.h`) over
   `guard_band_filter_base<S, P>` (K12), `guard_phase::nonlinear` (the odd-order Chebyshev II over
   DspTap's `biquad_cascade`) and `guard_phase::linear` (the even-length symmetric FIR over
   `dot_row`), `guard_band_settings::super_economy()` … `transparent()` at the orders 5 / 7 / 7 / 7
   and a custom form (the Chebyshev II designed at run time; the linear form by DspTap's Kaiser
   designer at run time, and refused by `validate()` where no length up to 512 taps meets it).
   Measured (`rational/tests/test_guard_band.cpp`, `test_iir_composition.cpp`,
   `test_iir_subnormals.cpp`):
   - the golden leg (`rational/tools/reference/make_guard_band_reference_vectors.py`: the
     Chebyshev II as scipy's own `cheby2` design through `sosfilt`, which the script checks is
     DspTap's transfer function within 1e-14; the FIR's committed taps through `lfilter`): within
     8.2e-15 / 4.4e-16 of full scale in double, 9.0e-7 / 6.8e-8 in float, 1.5e-5 / 2.4e-4 in Q15
     and 5.1e-9 / 3.9e-9 in Q31 (Chebyshev II / FIR), the Q15 and Q31 outputs bit-pinned; the
     group delay within 1.2e-13 frames of scipy's;
   - the spec from the double filter's impulse response: the Chebyshev II −A dB from its stop edge
     (equiripple), flat from 0 to −0.01 dB at p, −302 dB or below at Nyquist; the FIR −71.4 /
     −73.3 / −71.2 / −123.0 dB from the same edge and 0.0086 / 0.0068 / 0.0088 / 0.0062 dB peak to
     peak; the zero at Nyquist exact in every format's table (the first-order numerator's b0 == b1,
     the FIR's taps symmetric bit for bit), and a Nyquist tone removed exactly in fixed point;
   - latency: the Chebyshev II 0.54 frames at DC and 5.04 at the passband edge at `economy`
     (deviation 4.51), against its impulse response within 1e-9 frames; the FIR's (N − 1) / 2,
     30 / 50 / 58 / 72 taps, exact, and an all-FIR chain holding it keeps its exact latency;
   - fixed point: the Chebyshev's largest state 1.67 of full scale under full-scale drive (limit
     4), its Q15 floor −101.1 dB RMS against double, every state zero inside its window; the FIR
     in Q15 a format limit (its Q1.14 taps −67.1 / −66.8 / −64.6 / −70.9 dB, −77.8 dB RMS against
     double), in Q31 the design's;
   - flush and the limit cycle as I6 states them (v11); without flush-to-zero the Chebyshev's
     residue at most 10 times the smallest subnormal (bound 32), the FIR exact;
   - the aliases behind each half-band (`guard_band.AliasesLandOnlyAboveThePassbandEdge`): in the
     passband −71.4 dB or below with or without the filter, past its stop edge −75.1 dB or below
     with either form, near −3 (recursive) and −6 dB (FIR) without it;
   - composition: both forms satisfy `sync_stage`; mixed chains (an IIR ↑2 and an FIR ↑3 behind
     a guard band filter, the mirror ↓3 · ↓2 before one, the filter before the FIR and the
     near-linear interpolators and after the FIR decimator, a near-linear cascade by 8 and
     `down_by8`) bit-identical for any chunking and flushing as zero padding, with the honest
     limit where it applies; the offering table (I10) measured and pinned
     (`iir_composition.TheOfferingTableIsMeasured`).
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
  the later of the two PRs. The scaffold's PR can land first and wait.

## 6. Risks

- **Instructions, not MACs**: the serial recursion may vectorize worse than the FIR dot;
  the ratchet decides what the README claims, per target.
- **Float at 120 dB**: a recursive structure's rounding noise may sit above −120 dB in
  float; measured in IIR-0, and `transparent` not offered where it fails (I14).
- **Phase surprises**: a caller expecting the FIR's linear phase. The variant is in the
  type name, and the README states the group delay as numbers.
- **The near-linear design**: an optimizer's result depends on its start. IIR-0 found a
  design at every profile; committed tables (I4) make it reproducible, and the generator is
  in the tree. The round trip's −60 dB at the 70 dB profiles has about 1 dB of margin in
  IIR-0's designs, so IIR-b pins it per profile and divisor.
- **A host that does not set flush-to-zero** (I9): its CPU spikes on silence on x86, and its
  output after a flush is a subnormal residue rather than silence. The documentation, the
  debug-build precondition and `subnormals_flushed<S>()` are the defence; the stages
  themselves do nothing about it.
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
  honest limits. Its start-up FTZ and DAZ (above) do what I9 asks of a host.

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
  designs, fixed-point implementation). IIR-0 could not consult it: its near-linear method
  (an iterated linear program in the denominator coefficients, then SLSQP) is derived from
  the structure in the spike's README, the structure from Renfors and Saramäki. IIR-a checks
  the attribution against the book before the generator lands.
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
5. **Decided (v9, the maintainer): the round trip cancels to −60 dB at the three 70 dB
   profiles and −100 dB at `transparent`,** which IIR-0's designs reach at no extra sections
   (section 5). That is a round-trip phase error of at most 0.057° and 0.0057°. What v7 and
   v8 said of the budget still holds, at the new numbers. v7 decided 0.57°, so that a change
   cancels to −40 dB. It is the sum over every IIR stage, down and up: the
   decimator and the interpolator of a half-band share one H(z), so their errors add, and a
   ×8 round trip at 0.57° per stage could reach 3.4° (−24.5 dB). Each stage's share is
   derived at its divisor (an even split is about 0.1° per stage at ×8; relaxed early stages
   see the band lower in their own passband, so they need less), and the quantity pinned is
   phase, per stage and per round trip, not group delay. With v9's target, an IIR round
   trip meets the FIR round trip's −60 dB at the 70 dB profiles, so the ×4 / ×8 / ×16
   plan's tests (its 6.5 and 6.6) hold one threshold per profile for both stage kinds.
6. Decided (v5): pin the poisoning as an honest limit (I9).
7. Decided (v5): "more than 10 % cheaper, or a latency-first consumer" is the bar (I12),
   measured on 2.4's spec.
8. **Decided (v9, the maintainer; reversing v4's guard): flushing subnormals is the host's
   job** (I9). The plan documents what to set on every target. Each IIR stage checks it as a
   debug-build precondition. Where no bit exists (double on the M4F and M33, the M4 without
   an FPU, Hexagon as QEMU models it), the residue is an honest limit (I6).
9. Resolved (v5): the scripts are the DSPRelated ones, cited in section 7.
10. Decided (v8): one DspTap 0.3.0 with the ×4 / ×8 / ×16 plan's scaffold (section 5).
11. Followed from IIR-0 (v9), by the plan's own rules: the four-path ↓4 / ↑4 joins IIR-b
    where it passes I12's bar (I12); the constexpr amendments (I4).

12. Decided (v10, the maintainer): I3, the same four profiles at the same numbers; IIR-b found
    every profile reachable by both variants.
13. Decided (v10, the maintainer): I6, `window_frames()` is the count to an exactly zero state
    under flush-to-zero, plus one frame per section (with question 19's exception).
15. Decided (v10, the maintainer): I8, as IIR-a implemented it (floor truncation in the
    one-multiplier sections, magnitude truncation in the second-order ones, two integer guard
    bits, the difference nodes in the accumulator).
17. Decided (v10, the maintainer): I13, relaxed designs per divisor and the cascade helper,
    named `iir_down_by` / `iir_up_by`.
18. Decided (v10, the maintainer): I14, float states and products in the float profile.
14. Decided (v11, the maintainer): I7's order rule, the lowest odd order whose worst guard-band
    alias is at or below −A/2, orders 5 / 7 / 7 / 7; the linear form at the same stop edges.
19. Decided (v11, the maintainer): keep the honest limit, for the floating near-linear
    `transparent` stage at divisor 1 and the floating Chebyshev II at orders 7 and above (I6).

Recommended, not yet put to the maintainer (your word settles each):
16. I11: new C ABI entry points, the notebooks (IIR-d). I10, composition through
    `tap::dsp::chain` with the offering table, was decided in v11 as recommended.

Open (v11, IIR-c's findings):
20. **The guard band order behind the near-linear and the FIR half-band** (I7). Question 14's
    rule is computed behind the nonlinear half-band; behind the near-linear and the FIR half-band
    the named orders leave the guard band's worst alias at −31.4 / −31.6 dB at `super_economy` and
    −48.8 / −45.6 dB at `transparent`, above −A/2 (−35, −60), while `economy` and `balanced` stay
    below it. The choices: keep 5 / 7 / 7 / 7 and state it (IIR-c does, pinned); 7 / 7 / 7 / 11,
    which holds −A/2 behind every half-band (`transparent` at 30 multiplies instead of 20, and a
    102-tap FIR); or a setting per stage kind.
21. **The linear form in Q15** (I7, I8). Its Q1.14 taps attain −67.1 / −66.8 / −64.6 / −70.9 dB
    from the stop edge, below the 70 dB tier the FIR stages all reach in Q15; IIR-c pins it as an
    honest limit. The choices: keep it; Q15 rows in DspTap designed with the margin quantization
    needs (a DspTap change to `iir_tables.h`, longer rows); or the linear form not offered in Q15.