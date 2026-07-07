# Sample types as a customization point: `sample_traits.h`

> Form is exactly emptiness; emptiness is exactly form.
>
> — the Heart Sutra

The polyphase machinery of the last two chapters computes one thing: a dot
product between a window of input samples and an interpolated row of filter
coefficients. The problem is that this library ships to machines that do not
agree on what a number is. A Xeon host wants `float` samples and will happily
accumulate in `double`. A Hexagon DSP has no double-precision FPU at all —
every `double` operation is a soft-float library call. A Cortex-M33 has no
vector unit and wants 16-bit samples it can crunch two at a time. The same
algorithm must therefore run in three different arithmetic systems, produce
measured quality in each, and pay nothing for the flexibility.

Here is what "nothing" has to mean, concretely. The inner loop of
`interpolate()` runs one multiply-accumulate and one coefficient blend per
tap, per channel, per output sample. At 48 kHz stereo with the default
balanced preset (48 taps), that is about 4.6 million multiply-accumulates
per second — and every one of them goes through the customization point this
chapter describes. Any mechanism that adds even one indirect call to that
path has already lost.

This chapter tells two interleaved stories. The C++ story is how a traits
struct and a concept make the sample type a *compile-time* customization
point — and why the obvious alternatives (virtual dispatch, CRTP) were
rejected. The arithmetic story is fixed-point numerics from scratch: what
Q-formats are, where the headroom bits went, why the accumulators are
exactly as wide as they are, and two places where the file's own comments
record hard-won corrections. The two stories are one file:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_overview}}
```

Three sample types, and a division of labor worth pausing on: the clock
servo and the filter *design* always run in `double`, because they execute a
handful of operations per block or once at construction. Only the datapath —
the code that touches every sample — is templated. Optimizing anything else
would be effort spent where the profile isn't.

## The mechanism: a struct full of static functions

The customization point is a class template with no primary definition:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_primary}}
```

Leaving the primary template *undefined* is deliberate. A defined primary
template would need default behavior, and there is no honest default for
"how do I multiply-accumulate your type" — any guess would compile for
unsupported types and be silently wrong. Undefined, the template turns an
unsupported type into a compile error at the first use. (A more *readable*
error is the concept's job, below.)

Each supported type then gets a full specialization. The float one is the
simplest and shows the complete vocabulary — three associated types and
seven operations:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_float}}
```

Every operation the datapath performs on samples is named here: convert a
designed coefficient to storage form (`makeCoeff`), convert the fractional
position to the blend representation (`makeBlendFactor`,
`blendFactorFromQ64`), the multiply-accumulate (`mac`), the adjacent-phase
coefficient blend (`blend`), the accumulator-to-sample conversion
(`finalize`), and silence. The polyphase chapter's `interpolate()` is written
entirely in this vocabulary:

```cpp
acc = Tr::mac(acc, hist[t], Tr::blend(c0[t], c1[t], fr));
```

and consequently never mentions `int16_t`, `double`, or a shift instruction.
One algorithm, one body of tests, three number systems.

### Why not virtual dispatch

The classical OO answer — an abstract `SampleOps` interface with `mac()` and
`blend()` as virtual functions — fails on the arithmetic of the hot loop.
A virtual call is an indirect call through a vtable: the compiler cannot
inline it, and what it cannot inline it cannot optimize *across*. The Q15
`mac` below compiles to roughly two instructions when inlined; as a virtual
call it would be a call, a return, an argument setup, and — far worse — an
opaque boundary in the middle of the loop. Everything Part III wins depends
on the compiler seeing through these functions: the Q15 dot product
auto-vectorizes on hosts and gets Helium code on the M55 (the C2 audit
verified both), and the C4 SMLALD kernel exists because the products were
visible as exact 16×16 multiplies. Four and a half million vtable
indirections per second, each one an optimization fence, was never a
candidate.

Virtual dispatch also answers a question nobody asked. Dynamic dispatch
buys the ability to choose the implementation *at run time* — but a
converter's sample type is fixed at the moment you write
`AsyncSampleRateConverterQ15`. Paying the vtable price for flexibility that
is never exercised is the definition of the wrong tool.

### Why not CRTP

The curiously recurring template pattern is the usual zero-cost answer to
virtual dispatch, and it was rejected for a simpler reason: CRTP customizes
through *inheritance* — `class MySample : SampleBase<MySample>` — and the
sample types here are `float`, `std::int16_t`, and `std::int32_t`. You
cannot derive from a built-in type, and you should not have to wrap one in
a class (with all the conversion friction that implies) just to teach a
library how to multiply it. A traits struct attaches behavior to a type
*from the outside*, without requiring the type's cooperation. This is the
same reason the standard library uses `std::char_traits` rather than
requiring your character type to inherit from something: the type being
customized is not yours to modify.

The cost of the traits approach is one level of naming indirection
(`SampleTraits<S>::mac` instead of `x.mac`), which a `using Tr =` alias
reduces to nothing. The benefit is that the whole mechanism evaporates at
compile time: every call in this file is a `static` member function,
resolved by the template machinery, inlined by any compiler at any
optimization level worth shipping.

## Q-formats, from zero

Now the arithmetic story. Fixed-point notation **Qm.n** describes an
integer reinterpreted as a fraction: *n* bits after the binary point, *m*
bits (beyond the sign) before it. The stored integer *k* represents the
value *k* / 2ⁿ. So:

- **Q0.15** ("Q15"): an `int16_t` representing *k* / 2¹⁵. Range −1.0 to
  +0.99997. This is what 16-bit audio *is* — the industry just rarely says
  so out loud.
- **Q0.31** ("Q31"): the same idea in an `int32_t`, range −1.0 to
  +(1 − 2⁻³¹).
- **Q1.14**: an `int16_t` representing *k* / 2¹⁴ — one bit of *headroom*
  above ±1.0, range −2.0 to +1.99994, at the cost of one bit of precision.

Addition in a Q-format is ordinary integer addition. Multiplication adds
the fractional bit counts: Q0.15 × Q1.14 gives a product with 29 fractional
bits (Q29). Nothing is approximate yet — an integer multiply of two 16-bit
values is *exact* in 32 bits. Fixed-point arithmetic done carefully is not
"lossy integer math"; it is exact arithmetic with explicitly scheduled
rounding. The whole craft is deciding where the one rounding happens and
proving nothing overflows before it.

## The headroom bit: why coefficients are Q1.14, not Q0.15

The obvious choice for 16-bit coefficients is Q0.15, same as the samples.
It does not work, and the reason is a property of the filter itself: each
polyphase row has unity DC gain, and the prototype's *peak tap* reaches
approximately 1.0. Q0.15's most positive value is 0.99997 — the peak tap
does not fit. Saturating it would dent the filter's frequency response
precisely at the row where the response matters most.

So the coefficients trade one precision bit for one headroom bit:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_q15_coeff}}
```

with the conversion doing round-half-away-from-zero and saturating at the
integer limits (the *design* is checked separately; saturation here is a
belt against future filter specs, not an expected event):

```cpp
{{#include ../../../include/srt/sample_traits.h:st_roundsat}}
```

What did the traded bit cost? Quantizing coefficients to Q14 puts the
filter's stopband floor at roughly −86 dB — and the header's comment makes
the argument that matters: the Q15 *output* format's own noise floor is of
the same order. A 16-bit datapath cannot deliver more than the 16-bit
format can carry, so spending coefficient precision beyond the format's
floor would purchase nothing measurable. The end-to-end test agrees: the
Q15 converter measures **~77 dB SNR** on a half-scale 997 Hz sine across a
+200 ppm clock crossing (`tests/test_fixed_point.cpp` prints it; the CI
threshold sits at 73 dB), and that number is the *format's* floor, not the
converter's. The same trade at 32 bits gives Q1.30 coefficients
(`makeCoeff` scales by 2³⁰), where the quantization floor is so far down
that the Q31 path measures **133 dB** — statistically the float datapath's
own 135 dB.

The two unit tests pinning the scale factors are almost insultingly simple,
and that is their virtue: `Q15::makeCoeff(1.0) == 16384` is the sentence
"the peak tap fits" written as an assertion.

## The accumulation story: exact until the last line

Here is the Q15 multiply-accumulate:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_q15_mac}}
```

Two things are chosen here. The product is computed in `int32_t` — a
16×16→32 multiply, which every target does in one instruction — and it is
**exact**: the worst-case product is −32768 × −32768 = 2³⁰, comfortably
inside `int32_t`'s ±2³¹ range. But note how *thin* that comfort is: a
single worst-case product uses all but one bit of an `int32_t`. Summing
even two of them could wrap. An `int32_t` accumulator is therefore not
"risky"; it is simply wrong.

The accumulator is `int64_t`, and now do the arithmetic the comment
gestures at. The shipping filters run 32 to 80 taps per phase (fast,
balanced, transparent presets). Summing N values adds at most log₂N bits
to the worst-case magnitude: 48 taps add ~5.6 bits, 80 taps add ~6.3 — call
it six to seven bits. Worst case for the transparent preset:
80 × 2³⁰ < 2³⁷, against an accumulator that holds ±2⁶³. Twenty-six bits of
spare headroom. That surplus is the point: the sum is exact — not
approximately safe, *exact*, every intermediate value representable — no
matter what the samples and coefficients do. There is no intermediate
rounding anywhere in the loop, which also means the accumulation is
associative, which is why the C4 chapter's dual-MAC kernel and the C1
blended-row rewrite could both be verified *bit-exact* rather than
"close enough."

All of the rounding budget is spent in one place:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_q15_finalize}}
```

The accumulator holds a Q29 value (Q0.15 sample × Q1.14 coefficient); the
output wants Q15; so shift right by 14 after adding half an output LSB
(1 << 13). That is round-half-up. A numericist will object that
round-half-up carries a bias and round-half-even does not — and the comment
answers the objection with scale: the bias exists only on exact half values
and is a fraction of one sub-LSB rounding step, orders below the Q15 noise
floor that the 77 dB measurement already includes. Half-even costs extra
operations per output sample to fix an error you cannot measure. The
`clampSat` around it is the saturation that makes hot signals *clip*
instead of wrap — and wrapping is the catastrophic failure mode:

```cpp
EXPECT_EQ(Q15::finalize(std::int64_t{1} << 40), 32767);
```

plus an end-to-end test (`FullScaleSineDoesNotWrapQ15`) that drives a
99%-of-full-scale sine through a +500 ppm crossing and asserts the output's
second difference never exceeds the analytic bound for a clean sine — a
wraparound anywhere inside would blow that bound by orders of magnitude.

## Q31 and the pre-shift: when even int64 isn't enough

The 32-bit path cannot copy the 16-bit strategy, and the reason is worth
computing rather than asserting. A full-precision Q0.31 × Q1.30 product
carries 61 fractional bits and a worst-case magnitude near 2⁶¹ (full-scale
sample, peak ~1.0 coefficient). An `int64_t` holds ±2⁶³ — barely four such
products of margin. The shortest shipping filter sums 32 of them; the
transparent preset sums 80. At 48 taps the worst-case sum is
48 × 2⁶¹ ≈ 2⁶⁶·⁶, over the accumulator's limit by a factor of about twelve.
Full-precision products simply do not fit, and there is no 128-bit
accumulator worth having on the targets this path exists for.

So each product gives up 16 bits *before* joining the sum:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_q31_mac}}
```

Now redo the bound: Q45 products have worst-case magnitude 2⁴⁵, and
80 × 2⁴⁵ < 2⁵²— eleven bits of headroom restored. What did the discarded
bits cost? Each truncation throws away less than one Q45 LSB, and the
final conversion (`finalize` shifts a further 14 bits, Q45 → Q31) puts a
Q45 LSB **14 bits below the output's own LSB**. Even if all 80 taps'
truncation errors conspired in the same direction, the accumulated error is
under 80 × 2⁻⁴⁵ ≈ 2⁻³⁸·⁷ — less than 1/200 of one Q31 output LSB. The
measurement closes the argument: the Q31 converter's 133 dB / 105 dB
(997 Hz / 19.5 kHz) match the float datapath's numbers, whose residual is
set by the phase-table interpolation, not by anyone's arithmetic. The
discarded bits are provably and measurably inaudible — this is the
fixed-point craft in one line of code: *decide* where precision dies,
prove the grave is deep enough, then measure anyway.

The full specialization, for reference — note the doc comment carries the
same overflow argument, so the file survives without the book:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_q31}}
```

## The blend, and the comment that was wrong by three orders of magnitude

`blend` linearly interpolates between the same tap of two adjacent phase
rows (the polyphase chapter explains why; the residual falls ~12 dB per
doubling of the phase count). In Q15 it looks like this:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_q15_blend}}
```

That comment has a history, and the history is this book's whole
methodology in miniature. The blend multiplies a Q15 fraction
(`fr` ≤ 32767) by a coefficient difference (`diff` = b − a, two `int16_t`
values, so |diff| ≤ 65535). The original version of this comment justified
the `int64_t` by claiming the `int32_t` product would fit "but only with
~5% margin." An audit later recomputed it: the worst-case product is
32767 × 65535 = 2,147,385,345, and `INT32_MAX` is 2,147,483,647. The
margin is 98,302 out of 2.1 billion — **0.005%**, not 5%. Three orders of
magnitude, in a comment whose entire job was to quantify safety.

Nothing was wrong with the *code* — it used `int64_t` and still does. But
consider what the wrong comment was waiting to do: some future optimizer,
squeezing the M33 (where the C4 campaign found this very blend dominates
the Q15 frame cost — each `fr * diff` is a `smull`), reads "~5% margin,"
concludes the `int32_t` version is comfortably safe, and ships a datapath
that is one adjacent-phase anomaly away from integer overflow. The audit
also measured the *actual* worst |diff| on the transparent table: 41 —
real coefficients come nowhere near the bound. The corrected comment keeps
both numbers and the conclusion: a margin of 0.005% against a theoretical
bound is not an invariant to lean on silently, whatever today's table
does. The lesson generalizes: **a safety-margin comment is arithmetic, and
arithmetic in comments rots exactly as fast as arithmetic in code — the
difference is that no test ever fails on it.** Verify the numbers you
write in prose. This book's build system exists because of that sentence.

(The Q31 blend uses a Q20 fraction rather than Q15 — since the product runs
in `int64_t` anyway, the six extra fraction bits are free.)

## `blendFactorFromQ64`: feeding the integer phase

One trait remains, and it earns its keep on exactly one class of hardware.
The C3 optimization (Part III) replaced the resampler's `double` phase
accumulator with a Q0.64 integer — after which the *only* floating-point
left on the fixed-point per-sample path was the conversion of the phase
fraction into a blend factor. `blendFactorFromQ64` closes that hole. The
Q15 version is a single shift — the top 15 bits of the fraction *are* the
Q15 blend factor:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_q15_q64}}
```

The float version is subtler:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_blend_q64_float}}
```

Why reduce to 24 bits first? Because a `float` significand holds exactly
24 bits: any integer up to 2²⁴ converts to `float` *exactly*, and the
subsequent multiply by 2⁻²⁴ (a power of two) is also exact. Convert the
full 64-bit fraction instead and the compiler must round — correctly, but
via a path that on a double-less target may detour through software
arithmetic. This two-instruction dance keeps the conversion
single-precision, exact, and branchless. The target it matters on is
Hexagon, the one genuinely FP64-less machine in the fleet (the C3 write-up
records the correction: the M55's *scalar* FPU turned out to support
doubles after all — only its vector unit doesn't). C3's gating run showed
what removing per-sample soft-double math is worth on Hexagon: −15.5%
instructions on the Q31 pipeline, −10.3% on Q15. And because 2⁻⁶⁴ phase
resolution beats the old double path's 2⁻⁵², quality *improved* while the
code got faster: 135.0 dB at 997 Hz.

## The concept: making the contract legible

Everything above defines the customization point; the last twenty lines of
the file *enforce* it:

```cpp
{{#include ../../../include/srt/sample_traits.h:st_concept}}
```

The datapath templates constrain themselves with it —
`template <SampleType S> class BasicAsyncSampleRateConverter` — and the
payoff is the shape of the failure. Instantiate the converter with
`double` (no specialization exists) and, without the concept, the error
would surface wherever the template machinery first touched the undefined
traits — some line deep inside `interpolate()`, wearing five frames of
instantiation context. With the concept, the compiler rejects
`BasicAsyncSampleRateConverter<double>` *at the declaration you wrote*,
and its diagnostic walks the `requires`-expression clause by clause: which
operation is missing, what signature it expected. The concept turns "a
missing operation somewhere" into a checklist. Write a partial
`SampleTraits<MyType>` — say, everything but `blendFactorFromQ64` — and
the error names exactly that member.

Note the return-type constraints (`-> std::same_as<...>`) are doing real
work: a `finalize` that returned `int` instead of `int16_t` would satisfy
a naive "does it compile" check and then quietly change overload and
conversion behavior downstream. The concept pins the whole signature.

The three `static_assert`s at the bottom are the file testing itself: every
translation unit that includes the header re-verifies that the three
shipped specializations satisfy the concept they claim to. If a future
edit breaks one — renames a member, fumbles a return type — the diagnostic
arrives at header-parse time, before any user code, naming the assert.
Cost: zero, everywhere except the compiler's own microseconds.

## Why these ~220 lines look the way they do

| Decision | Alternative rejected | Reason |
|---|---|---|
| Traits struct of `static` functions | virtual `SampleOps` interface | 4.6M `mac`/s in the hot loop; virtual calls block inlining and every Part III optimization behind an opaque boundary |
| External traits | CRTP / member functions | sample types are `int16_t`/`float` — built-ins can't inherit and aren't ours to modify |
| Undefined primary template | primary with defaults | no honest default for foreign arithmetic; silence would be wrongness |
| Q1.14 / Q1.30 coefficients | Q0.15 / Q0.31 | the ~1.0 peak tap must fit; one headroom bit costs a precision bit the output format couldn't carry anyway |
| `int64_t` accumulator, no intermediate rounding | `int32_t` accumulator | one worst-case Q15 product nearly fills `int32_t`; exactness makes every kernel rewrite bit-verifiable |
| Q31 products pre-shifted to Q45 | full 62-bit products | 48 taps of 2⁶¹ ≈ 2⁶⁶·⁶ overflows `int64_t` ~12×; truncation cost < 1/200 output LSB, measured invisible |
| Round-half-up in `finalize` | round-half-even | the bias is sub-sub-LSB; half-even costs real per-sample work to fix an unmeasurable error |
| `int64_t` blend product | `int32_t` (it *almost* fits) | 0.005% worst-case margin — recomputed by audit from a comment that claimed 5% |
| `SampleType` concept + self-`static_assert`s | let instantiation errors happen | failures surface at the declaration, itemized per missing operation |

## Verify it yourself

```sh
# The whole fixed-point suite: scale factors, saturation, DC gain,
# measured SNRs (watch for the "[ measured ]" lines), full-scale non-wrap:
ctest --test-dir build -R FixedPoint --output-on-failure

# The measured numbers this chapter quoted:
#   [ measured ]   997 Hz, 16-bit fixed: SNR ~77 dB
#   [ measured ]   997 Hz, 32-bit fixed: SNR ~133 dB
#   [ measured ] 19500 Hz, 32-bit fixed: SNR ~105 dB

# Recompute the blend margin the audit checked (don't trust this book either):
python3 -c "print(32767*65535, 2**31-1, 1 - 32767*65535/(2**31-1))"

# Break it on purpose, three ways:
#  1. In makeCoeff (Q15), change 16384.0 to 32768.0 — the peak tap saturates
#     and DcGainIsUnityQ15 fails its ±4 tolerance.
#  2. In finalize (Q15), delete clampSat and cast directly — the full-scale
#     sine test detects wraparound as a blown second difference.
#  3. Instantiate srt::BasicAsyncSampleRateConverter<double> anywhere and
#     read the concept diagnostic: every missing operation, by name, at the
#     line you wrote.
```

The third experiment is the C++ half of this chapter in one error message;
the first two are the arithmetic half in two failing assertions.
