# The polyphase bank

> Show me your flowcharts and conceal your tables, and I shall continue to be mystified. Show me your tables, and I won't usually need your flowcharts; they'll be obvious.
>
> — Fred Brooks, *The Mythical Man-Month*

The previous chapter ended with a prototype filter: 12,288 double-precision
coefficients (for the default preset) describing one ideal anti-imaging
lowpass, oversampled 256× against the input rate. This chapter is about a
data structure. Per output sample, the converter's budget is one dot
product of 48 multiply-accumulates — not 12,288 — and the fractional
position μ arrives with 2⁻⁶⁴-sample resolution, demanding a filter for a
delay the table cannot possibly enumerate. `polyphase_filter_bank` is the
arrangement of those 12,288 numbers that makes the right 48 of them, for
*any* μ, a matter of two pointer offsets and a linear blend. Almost
everything interesting about it is in the layout: one extra row nobody
asked for, every row stored backwards, and a table that no code path may
touch after its constructor returns.

## The decomposition: L filters hiding in one

Recall what the prototype is: the windowed sinc sampled on a grid of 1/L
input samples, `L·T` taps long. Evaluating the input signal at a position
`p/L` between samples means dotting the T input samples in the window
against the sinc *offset by p/L* — which, on the prototype's grid, is
simply every L-th coefficient starting at p:

```text
branch 0:  h[0],  h[L],   h[2L],  …  h[(T−1)L]        delay 0
branch 1:  h[1],  h[L+1], h[2L+1] …                    delay 1/L sample
branch p:  h[p],  h[L+p], h[2L+p] …                    delay p/L sample
branch L−1: …                                          delay (L−1)/L
```

That is the entire polyphase decomposition for this use case — no z-domain
identities required. One oversampled filter *is* L ordinary T-tap filters
interleaved, each a fractional-delay filter for one grid position. Nothing
is computed to "decompose" it; the bank merely copies the prototype into a
`(rows × T)` table so that each branch's taps — which are strided L apart
in the prototype — become contiguous in memory, because the dot product
will read them T-at-a-time, millions of times, and the prototype order
would stride the cache to death. The classic references derive this
structure for rational resamplers (it is also how commercial ASRC silicon
like the AD1896 organizes its ROM); here it is simpler, because near-unity
operation means each output needs exactly *one* branch evaluation — the
question is only which branch, and what to do between branches.

## Between the branches: why L = 256 and a linear blend

μ is a 64-bit fraction; the table has L rows. Rounding μ to the nearest
row would quantize the delay to 1/L of a sample, and delay quantization on
a moving signal is *noise* — worse at high frequencies, where a fixed time
error subtends more phase. The bank's answer is the standard one at this
quality tier: pick the two rows adjacent to μ·L and interpolate the
*coefficients* linearly between them. The residual error of that blend is
the quality knob the spec exposes:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:bank_spec}}
```

The comment's two slopes are the design law for choosing L, and they are
measured properties of this code, not folklore (the README derives its
quality table from the test suite): the blend residual falls **about 12 dB
for every doubling of L** — linear interpolation has second-order error,
so halving the grid step quarters the error — and rises **about 12 dB per
octave of signal frequency**, because coefficient interpolation error acts
like a second derivative and high frequencies bend faster. You can see the
frequency slope directly in the shipped numbers for `balanced()`
(L = 256): 135 dB SNR at 997 Hz, 120 dB at 6 kHz, 112 dB at 12 kHz, 105 dB
at 19.5 kHz — once the signal frequency is high enough for the blend
residual to dominate, each octave costs roughly the predicted 8–12 dB. The
unit tests pin the same staircase at the kernel level, single tones against
the analytic sine: worst-case error below −120 dB at 997 Hz, −110 dB at
4 kHz, −100 dB at 10 kHz, −90 dB at 19 kHz.

Why not simply crank L and skip the blend? Cost. Nearest-row lookup has
*first*-order error — about 6 dB per doubling — so matching the blend's
accuracy at 19.5 kHz would take L in the hundreds of thousands and a table
in the hundreds of megabytes. With the blend, `balanced()` is
(256 + 1) × 48 float coefficients ≈ 48 KB — resident in L2, arguably L1,
on hosts, and tolerable in MCU RAM at Q15 (≈ 24 KB). `transparent()`
doubles L *and* stretches T for ≈ 160 KB in float, buying its extra margin
mostly at the top of the band (108 dB vs 105 dB at 19.5 kHz measured end to
end). Why not a fancier blend — cubic across four rows? It would double
the coefficient traffic and the blend arithmetic in the innermost loop the
library owns, to fix the *highest-frequency* residual only; L = 256 already
puts that residual below the 105 dB the rest of the chain sustains. The
linear blend is the cheapest operation that keeps the table small and the
error second-order; everything faster is worse, everything better is not
needed at this budget.

## The extra row: L + 1 rows for an L-phase filter

Here is the file's cleverest line, and it is a line of *allocation*, not of
algorithm:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:bank_layout}}
```

The problem it dissolves: blending needs rows `p` and `p + 1`. For
p = 0 … L−2 both exist. At **p = L−1** the blend wants "row L" — the
branch for a delay of exactly one whole sample. Modular thinking says row L
"is" row 0, and arithmetically it is — *for a different window*. Branch 0
is a delay of zero against the current window; the position μ → 1 is a
delay of one, which equals a delay of zero against the window advanced by
one input sample. Using row 0 against the *current* window would be wrong
by exactly one sample — not subtly wrong: it would blend the correct filter
with a copy of the signal shifted a full sample, an error at signal level.

The conventional fixes are all branches. Detect p = L−1 and handle the
wrap specially — a data-dependent branch in the per-sample path, taken at
the beat frequency between the two crystals (at 200 ppm, about ten times a
second), which is also precisely the moment the resampler executes a
whole-sample slip, the most delicate step it performs. Or clamp μ short of
1.0 and accept a periodic discontinuity — a spur at the beat frequency,
in a library chasing 120 dB.

The bank's fix: **store row L explicitly, as branch 0 advanced by one input
sample**. It falls out of the construction loop with no special case:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:bank_build}}
```

Follow the index math for `p == phases_`: the prototype index is
`m = t·L + L = (t+1)·L` — branch 0's tap `t + 1`. So row L holds branch 0's
coefficients shifted one *tap*, i.e. one input sample; the final tap
(`m = T·L`) falls off the prototype's end and the `(m < n)` guard writes a
zero. Row L computed against the current window is *identically* branch 0
computed against next window. The consequences, in the order they matter:

- **Branch-free interpolation.** `interpolate()` may always read
  `phase(p)` and `phase(p + 1)` for any p ≤ L−1. No modulo, no compare, no
  special case — the hot loop's structure is independent of μ.
- **Exact continuity at the μ-wrap.** As μ → 1 the blend converges to pure
  row L; the whole-sample slip then advances the window and resets μ to 0,
  where pure row 0 takes over — and those two evaluations are the same
  arithmetic on the same samples. The seam has *zero* width: not "small
  error," but bit-level agreement of the limits from both sides, up to the
  one blend step of the approach.

Neither property is left to prose. `Polyphase.ExtraRowEqualsPhaseZeroAdvancedOneTap`
asserts the layout claim coefficient by coefficient — `phase(L)[0] == 0`
and `phase(L)[u] == phase(0)[u−1]` with `EXPECT_EQ`, exact equality, no
tolerance, because the construction loop is supposed to make them the
*same numbers*, not similar ones. `Polyphase.MuWrapIsContinuousWithWindowShift`
then asserts the consequence at the semantic level: `interpolate(hist,
μ → 1)` equals `interpolate(hist + 1, μ = 0)` on random data — the
whole-sample-slip invariant the resampler (two chapters from now) leans on
every time the crystals drift one full sample apart. The cost of all this:
48 extra coefficients — 192 bytes in float — and one `+ 1` in a `resize()`.
It is the best byte-per-correctness trade in the library.

## Rows stored backwards

The second line of that layout comment: rows are **tap-reversed**.
Convolution is inherently a reversal — output = Σ h[k] · x[now − k] — so
either the coefficient array or the history walk must run backwards.
The resampler keeps each channel's history as an *oldest-first* window
(natural for its append-and-compact delay line, and the friendly direction
for hardware prefetchers). Storing each row reversed at construction —
`table_[p·T + (T−1−t)]` — lets the kernel be the loop every SIMD unit
wants:

```text
for t in 0…T−1:  acc += hist[t] · row[t]
```

both arrays walked forward, contiguously, from element zero. The
reversal is paid once per converter at build time instead of once per
sample as backwards addressing, and the payoff is documented downstream in
this book's optimization chapters: the auto-vectorized Q15 kernels, the
SMLALD pair-loads on Cortex-M33 (which require adjacent taps to sit in
ascending order in one 32-bit load), and the `SRT_RESTRICT` blend loop all
assume exactly this orientation. One subtlety the test above already
banked: "advanced one tap" for the reversed row L means shifted one slot
*toward the newer end*, which is why the zero lands in slot 0 (the oldest)
— the kind of double-negation a comment can state but only an `EXPECT_EQ`
can enforce.

## Quantization happens here, once

The table's element type is not `double` — it is
`sample_traits<S>::Coeff`, and the constructor's `make_coeff(v)` is the
single point where the design-precision prototype becomes datapath
coefficients. Quantizing once at build time, rather than converting on the
fly, means the hot path reads exactly what it dots and the quantization
error is a fixed property of the constructed object, measurable by the
tests rather than dependent on the code path taken.

What each sample type stores (the full traits treatment is the next
chapter; here is what the *bank* needs you to know):

- **float** stores float coefficients: quantization at roughly −150 dB
  against the double prototype — comfortably irrelevant under a 120 dB
  target, which is why the float path's quality tests read the same as the
  design spec.
- **Q15 and Q31 store Q1.14 and Q1.30**, not Q0.15/Q0.31 — one bit of
  headroom spent because of a fact the *previous* chapter created: the
  prototype is normalized so each branch has DC gain 1, which puts the
  peak (center) tap at ≈ 1.0, and 1.0 does not fit a pure fractional
  format whose ceiling is 1 − 2⁻¹⁵. Rather than rescale the filter (and
  move the problem into output gain), each fixed-point format trades its
  top precision bit for range. `make_coeff` rounds half-away-from-zero and
  saturates, so even a tap of exactly 1.0000…1 from design rounding
  becomes the format's max instead of wrapping to −1 — a wraparound there
  would be a −∞ dB event, not a noise-floor one.

The bank is thus one template with three concrete personalities, and the
table *is* the personality: same layout, same extra row, same reversal,
different arithmetic downstream.

## Validation in two layers, and the all-NaN table

The constructor rejects what it can see is nonsense: a non-positive sample
rate, fewer than 4 taps, fewer than 2 phases, inverted or out-of-range band
edges — throwing `std::invalid_argument` at setup time, where exceptions
are allowed and cheap. This is necessary and insufficient, and the gap
between those two words is an audit story worth retelling precisely.

Every check in the constructor is a comparison. Feed the converter a
`config` whose `sample_rate_hz` is NaN — one uninitialized field in caller
code — and every comparison is *false*: `sample_rate_hz <= 0.0`? False.
`stopband_hz > sample_rate_hz`? False. The constructor sails through,
`cutoff_norm` goes NaN, `design_prototype` dutifully computes 12,288 NaN
coefficients (recall the previous chapter: the Bessel iteration cap exists
so even *this* terminates), and the object constructs successfully. The
converter then runs, produces NaN audio forever, and never throws, never
asserts, never glitches in a way a log would catch. The adversarial audit
of the library built exactly this object (finding F2); the fix is the
converter-level `validated()` gate, which enforces what the bank's local
comparisons cannot express:

- **finiteness of every double in the config** — the only guard NaN cannot
  slip, because it is `std::isfinite`, not an ordering;
- **the band-edge sum rule**: `passband_hz + stopband_hz ≤ sample_rate_hz`.
  The bank alone accepts `stopband_hz` up to the sample rate, but the
  cutoff is *centered* at `(pass + stop)/fs` — let the sum exceed fs and
  the anti-image cutoff lands above the input Nyquist, a filter that
  passes the very images it exists to kill, while every local check still
  passes;
- plus the servo's eps-overflow clamp and 32-bit size-product overflow,
  which belong to later chapters.

All of it is pinned by `ConfigValidation.RejectsSilentMisbehavior` — each
formerly-constructible pathology now `EXPECT_THROW`s — and, just as
deliberately, by two `EXPECT_NO_THROW`s: the rate-scaling factory
`config::for_sample_rate` produces specs sitting *exactly on* the sum-rule
boundary (passband + stopband == fs up to rounding), and a validation rule
that rejected its own library's presets would be a different bug. The
division of labor is a pattern to copy: the class rejects what it can
express *locally*; the composition layer owns the invariants that only
exist between components; and every rejected configuration is one a real
caller could plausibly write.

## C++ notes: immutability, `bit_ceil`, and the accessors

**Immutable after construction — as architecture, not style.** The class
has no mutating member functions; every accessor is `const noexcept`. This
buys three unrelated things at once. *Thread safety by subtraction*: the
bank is built on the setup thread and read from the real-time consumer
thread; with no writes after publication there is nothing to synchronize —
the ring buffer chapter's acquire/release agonies simply do not apply to
this object. *RT discipline*: the only allocation is in the constructor,
which the header explicitly assigns to setup time; the audio path holds a
`const` pointer and cannot even express a reallocation. *Exception
containment*: everything that can throw (`bad_alloc`,
`invalid_argument`) throws before the object exists, so a constructed bank
is unconditionally valid — there is no half-designed state for the hot
path to trip over.

**`std::bit_ceil` for L.** The constructor rounds `num_phases` up to a
power of two rather than validating it, and the reason lives in the
resampler's fast path: the Q0.64 phase accumulator selects the row by
taking the top log₂ L bits of a 64-bit fraction — one shift — and the
intra-row blend factor from the bits below — one more shift. That indexing
scheme *requires* a power-of-two L; `bit_ceil` (C++20, `<bit>`, exact and
self-describing where the old `1 << ceil(log2(n))` dance was neither)
guarantees it while giving any spec at least the resolution it asked for.
Rounding up rather than throwing is deliberate policy: more phases is
strictly better along the quality axis, so a spec of 200 phases quietly
becomes 256 rather than a setup error. The same power-of-two guarantee is
what lets `blend_row_phase` recover log₂ L with `std::countr_zero` instead
of storing it.

**The accessor surface is four functions, and their shapes are load-bearing:**

```cpp
{{#include ../../../include/srt/polyphase_filter.h:bank_accessors}}
```

`phase(p)` returns a raw `const Coeff*`, not a `std::span` — the kernels
consume rows through `SRT_RESTRICT`-qualified pointer parameters (that
no-alias promise is worth measured percentage points; see the
vectorization-audit chapter), and a span would be unpacked back to a
pointer at every call site while implying a bounds story the hot path
cannot afford to check. The domain quietly includes `p == num_phases()` —
the extra row is a first-class citizen of the API, which is exactly how
`interpolate()` gets to be branch-free:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:bank_interpolate}}
```

Note the one guard that *does* exist — clamping `p` when μ rounds up to
exactly L — protects against a floating-point edge of the *caller's* μ,
not of the table; and `group_delay_samples()` reports `(L·T − 1)/(2L)`, the
true center of the linear-phase prototype in input samples, which is
"T/2" only to the resolution of the 1/(2L) half-step that the kernel
accuracy tests must account for when they compute the expected analytic
delay. The bank knows its own delay exactly; approximations are for prose.

## Why this table looks the way it does

| Decision | Alternative rejected | Reason |
|---|---|---|
| Contiguous T-tap rows per branch | dot the strided prototype directly | the kernel reads rows millions of times; stride-L access wastes the cache the table was sized to fit |
| Linear blend between adjacent rows | nearest row; cubic blend | nearest needs astronomically large L (first-order error); cubic doubles hot-loop work to fix a residual already below the chain's floor |
| L = 256 default | 128 / 512 | −12 dB residual per doubling vs table size; 48 KB meets the 105 dB @ 19.5 kHz budget; presets bracket it both ways |
| **Extra row L** | wrap to row 0 + branch; clamp μ | branch-free hot loop; μ-wrap/whole-sample slip exactly continuous; costs 192 bytes |
| Tap-reversed rows | reversed iteration per sample | reversal paid once at build; forward contiguous dot is what vectorizers and SMLALD pair-loads require |
| Quantize via `make_coeff` at build | convert coefficients on the fly | error becomes a fixed, testable property of the object; hot path reads storage type directly |
| Q1.14 / Q1.30 coefficients | Q0.15 / Q0.31 | peak tap ≈ 1.0 by DC normalization; headroom bit beats wraparound at the table's largest value |
| Throw in constructor + converter `validated()` | validate in one place | the class can only check local comparisons; NaN defeats comparisons — finiteness and the band-edge *sum* rule are composition-level invariants (audit F2) |
| Immutable after construction | resettable/redesignable bank | cross-thread reads need no sync; allocation and throws confined to setup; no invalid intermediate states |
| `std::bit_ceil(num_phases)` | reject non-power-of-two | phase-bit row indexing requires 2ᵏ; rounding up is strictly quality-positive |
| Raw `const Coeff*` accessor | `std::span` row | kernels take restrict pointers; span adds implied checking the per-sample path cannot spend |

## Verify it yourself

```sh
# Build, then run this chapter's direct evidence: DC gain across mu, the
# extra-row layout equality, the mu-wrap continuity invariant, and the
# fractional-delay error staircase for balanced and transparent:
cmake -B build && cmake --build build -j
ctest --test-dir build -R Polyphase --output-on-failure

# The audit's rejected-config suite (NaN rate, image-passing band edges),
# including the boundary cases that must keep constructing:
ctest --test-dir build -R ConfigValidation --output-on-failure

# The end-to-end SNR numbers the L=256 decision is quoted against
# (997 Hz / 6 k / 12 k / 19.5 k, both presets, servo in the loop):
ctest --test-dir build -R AsrcQuality --output-on-failure

# Break it on purpose: in the constructor, change `p <= phases_` to
# `p < phases_` and resize to phases_ * taps_ (no extra row), then make
# interpolate wrap p+1 to 0. DcGain still passes — DC can't see a
# one-sample shift — but MuWrapIsContinuousWithWindowShift fails loudly,
# which is exactly the gap between "looks fine on steady signals" and
# "correct at the slip."
```

The sabotage run is the section on the extra row, compressed: the wrap bug
is invisible to the easiest test and to DC reasoning, and the suite was
built by someone who knew that.
