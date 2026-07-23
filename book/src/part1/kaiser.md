# Designing the filter: `kaiser.h`

> The purpose of computing is insight, not numbers.
>
> — Richard Hamming

This is the only file in the library that runs exactly once per converter,
and it decides the quality ceiling of everything downstream. Every output
sample the converter will ever produce is a dot product against
coefficients this file computes in a few milliseconds at construction. If
the design here reaches 120 dB of image rejection, no later cleverness is
needed to preserve it — the hot path is exact integer or double
accumulation all the way out. If the design here falls short, no later
cleverness can recover it. So before touching the code, this chapter builds
the minimum filter-design theory a C++ reader actually needs — which is
less than a DSP course and different from one — and then spends its pages
where the textbooks stop: on the iteration cap, the clamp, the
normalization constant, and the compile-time-versus-runtime decision that
the textbooks never had to make.

## The problem: evaluate a signal between its samples

The converter's core operation (next chapter) is a *fractional delay*: given
the last `T` input samples of a signal, produce its value at a position μ
that falls between two of them. Sampling theory says this is not guesswork.
A signal sampled at rate `fs` with no content above `fs/2` is *completely
determined* between its samples; the reconstruction is

```text
x(t) = Σₙ x[n] · sinc(t − n),        sinc(u) = sin(πu) / (πu)
```

— every sample contributes a sinc centered on itself, and the sum
interpolates exactly. The `sinc` in this file is that function, with the
one hazard a numeric programmer would expect handled explicitly:

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/kaiser.h:kai_sinc}}
```

(The 0/0 at x = 0 is a *removable* singularity — the limit is 1 — but IEEE
arithmetic doesn't take limits, so the code must.)

The catch is the `Σₙ`: it runs over **all** samples, and sinc decays like
1/t, which is uselessly slow. Truncating the sum to a window of `T` samples
around the evaluation point is mandatory. How you truncate is the entire
design problem.

## Why plain truncation rings

Chopping the sinc off after T samples is the same thing as multiplying the
ideal infinite filter by a rectangular window. Multiplication in time is
convolution in frequency, so the ideal filter's perfectly sharp frequency
response gets smeared by the rectangle's spectrum — and the rectangle's
spectrum is awful: its sidelobes start at −13 dB and buy you a stopband of
only about −21 dB. Worse, this is a *shape* problem, not a *size* problem.
Doubling T squeezes the smearing into a narrower band (the transition
sharpens) but the first sidelobe stays at the same level — the Gibbs
phenomenon. A truncated sinc leaks images at −21 dB whether it has 12 taps
or 12,000, and −21 dB is roughly the error of cheap linear interpolation.
For a 120 dB budget, truncation alone is off by five orders of magnitude.

The fix is to taper instead of chop: multiply the sinc by a window that
falls smoothly to zero at the edges. Every smooth window trades the same
two currencies — a wider main lobe (slower transition, so more taps for the
same band edges) buys lower sidelobes (deeper stopband). The question is
only which window spends the taps most efficiently.

## The Kaiser window, and what to cite

James Kaiser's answer (Kaiser 1974; the survey that made it standard
practice is harris 1978) is the *I₀–sinh* window,

```text
w[i] = I₀(β · √(1 − u²)) / I₀(β),      u ∈ [−1, 1] across the window,
```

where I₀ is the zeroth-order modified Bessel function. It is a closed-form
approximation to the *prolate spheroidal* window — the provably optimal
concentration of energy in the main lobe — that costs one special function
instead of an eigenvalue problem. Its virtue for engineering is the single
knob: **β alone sets the sidelobe level**, continuously, from rectangular
(β = 0) to arbitrarily deep, and Kaiser published empirical formulas mapping
a stopband spec in dB directly to β and to the filter length. No iteration,
no optimization run, no table lookup: attenuation in, coefficients out.

That is the theory, and this book will not re-derive it — Kaiser's paper
and harris's survey do it properly. What they do *not* tell you is how to
evaluate I₀ in a `noexcept` header without a math library that provides it,
what happens to the length formula when a caller hands it garbage, why the
normalization constant is `L` and not 1, or whether any of this should run
at compile time. That is the rest of this chapter.

![The Kaiser window for the three presets' beta values: higher attenuation
targets produce more strongly tapered windows](../img/kaiser-window.svg)

*The knob in action: the presets' attenuation targets (96/120/140 dB) map
through `kaiser_beta` to β = 9.6/12.3/14.5, and higher β buys its deeper
stopband by tapering the window harder — which widens the main lobe, which
is why `estimate_taps` charges more taps for the same transition width.
Generated by `scripts/book_figures.py` from the same formulas.*

## `bessel_i0`: a power series with an escape hatch

`<cmath>` has no I₀ (`std::cyl_bessel_i` exists in the special-functions
annex, but it is optional, absent from libc++, and this library targets
toolchains as odd as hexagon-musl). So the file computes it from the power
series

```text
I₀(x) = Σₖ [ (x/2)ᵏ / k! ]²
```

which converges for every finite x:

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/kaiser.h:kai_besseli0}}
```

Three details carry all the engineering.

**The recurrence.** Each term is the previous term times `(x/2k)²` — no
factorials, no powers, no overflow staging. Term k relates to term k−1 by
exactly the ratio `r²`, computed in two multiplies. For the β values this
library ever produces (about 12.3 for the 120 dB preset, 14.5 for 140 dB)
the terms grow until k ≈ x/2 ≈ 6 and then collapse factorially; a few dozen
terms reach full double precision, matching the "~50-term" budget the
file's header comment charges against constexpr evaluation.

**The stopping criterion.** `term < 1e-21 * sum` stops when the next term
can no longer perturb the sum's 16 significant digits — a *relative* test,
so it is correct whether I₀ is 1.0001 or 10⁴ (it is about 19,000 at β = 12).
The margin below double epsilon (≈ 2.2·10⁻¹⁶) costs a handful of extra
iterations and removes any temptation to reason about rounding at the
boundary.

**The iteration cap — the line a textbook would not print.** The loop bound
`k < 1000` looks redundant: the series *always* converges, so the relative
test *always* fires eventually. For every real x, yes. Feed the function a
NaN — say, from an uninitialized config field three call frames up — and
every comparison involving `term` is false, including the exit test. An
unbounded loop in a `noexcept` function would hang the caller's constructor
forever. With the cap, the worst case is a garbage return value that the
converter-level validation (chapter after next) rejects anyway. The cap is
not about convergence; it is about making *termination* independent of
floating-point semantics. This costs one integer compare per iteration and
turns an unprovable property into a checkable one.

The unit test pins the function against reference values computed
independently (`bessel_i0(1.0) = 1.2660658777520084…`), at tolerances that
scale with the magnitude — 10⁻¹² absolute near 1, 10⁻⁶ near 19,000 — i.e.
constant *relative* accuracy, which is what the window formula's ratio
`I₀(β·…)/I₀(β)` actually consumes.

## `kaiser_beta`: an empirical fit, taken as published

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/kaiser.h:kai_beta}}
```

This is Kaiser's published fit, digit for digit — `0.1102`, `0.5842`,
`0.07886` are his constants, not this library's, and the right response to
magic numbers with a citation is to copy them exactly and test them exactly
(the unit test asserts the formulas symbolically, so a typo in a constant
cannot hide). Two things are worth understanding rather than memorizing:

- **Why piecewise.** The relationship between β and achieved attenuation is
  smooth but not polynomial; Kaiser fit it in two regimes. Above 50 dB the
  relationship is essentially linear. Between 21 and 50 dB the fractional
  power term takes over. Every preset this library ships (96–140 dB) lives
  on the first line; the second exists so that off-spec experiments degrade
  gracefully instead of nonsensically.
- **Why zero below 21 dB.** A rectangular window — no taper at all —
  already achieves about 21 dB. Asking the fit for less than the free
  floor correctly returns "don't taper."

## `estimate_taps`: the cost formula, with a seatbelt

β sets the stopband *depth*; the number of taps sets how fast the response
can *fall* into it. Kaiser's length estimate (the form popularized by
harris) says taps scale linearly with attenuation and inversely with
transition width:

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/kaiser.h:kai_estimate}}
```

Note what the signature normalizes to: transition width *as a fraction of
the input rate*, and the return is taps *per polyphase branch*. The full
prototype (next section) has `L·T` taps at an oversampled rate of `L·fs`;
run the classic formula at that rate and both numerator and denominator
pick up the same factor of L, which cancels. Expressing the estimate per
branch keeps the caller's arithmetic in the units the caller actually has —
"8 kHz of transition at 48 kHz" — with no L in sight.

Plug in the `balanced()` preset: 120 dB across a 20→28 kHz transition at
48 kHz gives `(120 − 8) / (2.285 · 2π · 8000/48000) ≈ 46.8`, so 47 taps;
the unit test (`Kaiser.TapEstimateMatchesHarrisFormula`) brackets exactly
this computation at 45–49, and the shipped preset says `taps_per_phase = 48`
— the estimate rounded up to an even count (even matters later: the SMLALD
kernel on Cortex-M33-class parts consumes taps in pairs). This function is
how the presets were *chosen*; the bank itself takes `T` from the spec, so
the estimate is a design aid with a unit test rather than a hot dependency.

Then there is the comment at the top of the body, which earns its own
paragraph because it was not in the first version of this file. The raw
formula misbehaves at both edges of its domain: `atten_db < 8` makes the
numerator negative, and a zero or negative transition width divides to
±infinity. Both would then hit `static_cast<std::size_t>` — and converting
a negative or non-finite `double` to an unsigned integer is **undefined
behavior** in C++, not "some big number." Not implementation-defined:
undefined, the kind UBSan flags and optimizers exploit. An adversarial
audit of the library flagged the cast; the guard was added in response. The
predicate is written `!(trans_width_norm > 0.0)` rather than
`trans_width_norm <= 0.0` deliberately — the negated form is also true for
NaN, so all three pathologies (negative, zero, NaN) funnel into the same
clamp, and the attenuation edge is covered by the `n > 4.0` select on the
other side. The floor of 4 taps is the smallest window the bank will accept.
A design helper this cheap has no business having *any* input that invokes
UB, however silly the input.

## `design_prototype`: where all of it lands

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/kaiser.h:kai_prototype}}
```

One pass, one output array, but four decisions are packed into these lines.

**The grid.** The prototype is the windowed sinc sampled `L` times per
input sample — `t = (i − center) / num_phases` is time measured in *input*
samples. This is the oversampled master filter that the next chapter slices
into L branches; length `L·T` means 4,096 doubles for `fast()`, 12,288 for
`balanced()`, 40,960 for `transparent()`. `center` places the peak exactly
mid-array, which makes the filter linear-phase by symmetry — its group
delay is a constant `T/2` input samples, the number the converter's latency
formula quotes.

**The window argument, defensively.** `u` sweeps [−1, 1] across the array
and feeds `√(1 − u²)`. At the exact endpoints `1 − u²` is zero in real
arithmetic but can round a few ulps *negative* in floating point, and
`std::sqrt` of a negative is NaN — one NaN tap would silently poison every
dot product that ever touches that row. The `std::max(0.0, …)` costs
nothing and closes the hole. (Notice the theme: this file trusts
floating-point identities nowhere — not in `sinc`, not in the series exit,
not here.)

**What `cutoff_norm` means, and its surprising value.** The cutoff is
normalized so 1.0 sits at the *input* Nyquist, and the caller centers it in
the transition band: `(passband_hz + stopband_hz) / fs`. For the balanced
preset that is (20,000 + 28,000)/48,000 = **exactly 1.0** — the −6 dB point
of this anti-imaging filter sits *at* 24 kHz, with the response still flat
at 20 kHz and 120 dB down by 28 kHz. A reader trained on decimation filters
may flinch: doesn't a cutoff at Nyquist let aliasing through? No — this
filter's job is *interpolation* in a near-unity converter. The images it
must kill are reflections of the input spectrum around `fs`, so content
below 20 kHz images no lower than 28 kHz; the band between 20 and 28 kHz
contains, by construction of the spec, nothing anyone claimed to protect.
Splitting the transition symmetrically across Nyquist spends the taps where
they buy audible margin on both sides. This is the first of several places
where "near-unity only" (the library's headline restriction) converts
directly into cheaper mathematics.

**The normalization: sum = L, not 1.** A textbook lowpass normalizes its
coefficient sum to 1 so DC passes at unity gain. This prototype normalizes
to `L` — because no output sample is ever computed with the whole
prototype. Each output uses one branch of `T` taps: every L-th coefficient.
The L branch sums partition the total, and for a good lowpass they
partition it *evenly* — each branch's DC gain deviates from the mean only
by stopband-sized leakage (a branch sum is, in DFT terms, the prototype's
response sampled at multiples of the input rate: exactly the image
frequencies the stopband suppresses). Normalize the total to L and every
branch lands at 1 ± leakage; feed the converter DC and DC comes out, at any
fractional position. That is not left to inspection:
`Polyphase.DcGainIsUnityAcrossMu` pushes an all-ones window through the
*built* bank at 64 random μ values and requires unity within 10⁻⁴ — a
bound loose enough to admit float coefficient storage and row blending,
tight enough that a normalization bug (off by one branch, off by a factor
of L) fails by orders of magnitude. One subtle consequence lands two
chapters from now: with branch gains pinned near 1, the *peak* coefficient
also sits near 1.0, which is precisely why the fixed-point formats must
spend a headroom bit (Q1.14, Q1.30) on their coefficients.

## The headline decision: runtime design, not `constexpr`

Everything above is pure functions of compile-time-lookable values — and
this is C++20, where `constexpr` has teeth. The obvious modern move is to
evaluate the whole design at compile time: coefficients in `.rodata`
(attractive on a flash-based microcontroller), zero construction cost, even
`static_assert`s on the response. The file's own header records why that
was rejected, and since the reasoning is a design artifact it is kept where
refactors will trip over it:

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/kaiser.h:kai_design_note}}
```

Present the alternative fairly, because it *almost* works:

- **The language isn't there yet.** `std::sin`, `std::sqrt`, `std::pow`
  are not `constexpr` before C++26 (P1383 fixes this). A C++20 constexpr
  design needs hand-rolled constexpr transcendentals — several hundred
  lines of the most bug-prone code in numerics, duplicating functions the
  runtime already has, in a library whose entire test story leans on
  comparing against exactly those runtime functions.
- **The compile-time cost is not a rounding error.** Constexpr evaluation
  is interpretation, three to four orders of magnitude slower than native
  code. The design touches every one of 12K–41K taps with a `sin`, a
  `sqrt`, and a ~50-term Bessel series. What runs in well under 10 ms
  native becomes tens of seconds to minutes interpreted — **per translation
  unit**, because a header-only library re-instantiates in every TU that
  includes it. A user with twenty includes pays twenty times, on every
  rebuild, forever.
- **The inputs are not actually compile-time.** The band edges are scaled
  by the *runtime* sample rate (`filter_spec::scaled_to`,
  `config::for_sample_rate`) — a converter constructed for a rate read from
  an ALSA descriptor at startup cannot have baked coefficients at all. A
  constexpr path would be a second, divergent code path serving only the
  subset of users with fully static configs.

Against all that, the runtime cost being amortized is: one design, under
10 ms, in a constructor documented as setup-time-only, off the audio path
by the library's own RT rules. The trade is lopsided once written down —
but only once written down, which is why the file writes it down. (If
C++26 constexpr math plus a measured compile-time budget someday flips the
trade for static configs, the pure functions here are already shaped for
it: no state, no allocation, `std::span` in, coefficients out.)

## The test evidence: the spec, measured by DFT

A filter design module invites a lazy test — "coefficients equal last
week's coefficients." That freezes bugs in amber. What the library pins
instead is the *specification*: `tests/test_kaiser.cpp` computes the
prototype's actual frequency response by direct DFT and asserts the numbers
the presets advertise.

![Prototype magnitude response of the three presets, with a passband-ripple
detail panel](../img/kaiser-response.svg)

*What the spec tests pin: each preset's transition starts at its passband
edge and reaches its rated floor by its stopband edge, and the detail panel
shows all three passbands flat within ±0.01 dB. The curves come from
`scripts/book_figures.py`, which re-runs `design_prototype`'s math verbatim.*

The measurement function evaluates `|H(f)|` at arbitrary frequencies in Hz
against the oversampled prototype (rate `L·fs`), normalized by L so the
passband reads 0 dB — a direct O(n) sum per frequency. No FFT: an FFT
would demand a power-of-two grid, deliver frequencies nobody asked for,
and drag in a dependency, all to accelerate a few hundred evaluations in a
test that runs in milliseconds. Then, for each shipped preset:

- **Passband flatness:** every 500 Hz from DC to the passband edge,
  response within ±0.01 dB of unity. That is the "flat to 20 kHz" claim in
  the README, as an executable inequality.
- **Stopband depth:** every 250 Hz from the stopband edge out to *three
  times the sample rate*, response below −(spec − 1) dB. The 3·fs reach
  matters: the polyphase structure's images repeat around every multiple
  of fs, so a stopband that sagged past the first image would pass junk at
  96 kHz even if 28 kHz looked fine. The 250 Hz step is calibrated to the
  filter, not guessed: a T-tap-per-branch prototype has sidelobe nulls
  spaced fs/T ≈ 1 kHz apart, so 250 Hz sampling puts about four probes on
  every lobe — a peak cannot hide between probes. The 1 dB grace absorbs
  the gap between Kaiser's empirical β fit and the realized window; the
  presets' 120 means "at least 119 measured," and in practice the margin
  is comfortable.

Honest limits, as always: these tests certify the *double-precision
prototype*. Coefficient quantization (float, Q1.14, Q1.30) and the
row-blending residual are downstream effects certified by the next
chapter's tests and the end-to-end SNR suite — the layering is deliberate,
so a failure names its culprit.

## Why these ~100 lines look the way they do

| Decision | Alternative rejected | Reason |
|---|---|---|
| Kaiser window | Parks–McClellan / remez | one β knob, closed form, no iteration to converge or fail at setup; near-optimal is optimal enough at 120 dB |
| Power-series I₀ | `std::cyl_bessel_i` | optional annex, missing on libc++/embedded toolchains; the series is 12 lines and testable |
| Iteration cap `k < 1000` | trust convergence | NaN input defeats the relative-error exit; termination must not depend on FP semantics in a `noexcept` function |
| UB clamp in `estimate_taps` | trust callers | negative/infinite → `size_t` cast is UB; found by audit, closed for one branch |
| Cutoff centered in transition, up to input Nyquist | classic conservative cutoff | near-unity interpolation only fights images of the protected band; symmetric transition spends taps evenly |
| Normalize sum to L | sum to 1 | per-*branch* DC gain is what reaches the output; pinned by the DC unit test |
| Runtime design | C++20 constexpr tables | pre-C++26 constexpr math gap; minutes of interpreted evaluation per TU; runtime sample rates exist; <10 ms once at setup |
| Spec-based DFT tests | golden coefficient files | tests the claim, not the bits; refactors that preserve the response pass |

## Verify it yourself

```sh
# Build and run the design-math tests: Bessel/beta reference values, the
# harris estimate bracket, and the DFT passband/stopband spec checks for
# all three presets:
cmake -B build && cmake --build build -j
ctest --test-dir build -R Kaiser --output-on-failure

# The claim the normalization exists to protect (unity DC gain through the
# built bank, swept over mu):
ctest --test-dir build -R Polyphase.DcGain --output-on-failure

# Break it on purpose: in design_prototype, change the normalization to
# `1.0 / sum` (the textbook choice) and watch DcGainIsUnityAcrossMu fail by
# a factor of num_phases; or weaken kaiser_beta's 0.1102 to 0.11 and watch
# the Transparent stopband check report the exact frequency that leaks.
```

Both sabotage runs are worth the five minutes: the first shows you which
test owns the normalization contract, and the second shows the empirical β
fit has no slack at 140 dB — which is precisely why the constants are
copied from Kaiser 1974 to the last digit.
