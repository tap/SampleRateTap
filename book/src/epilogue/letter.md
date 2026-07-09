# Epilogue: a letter from the list

> All models are wrong, but some are useful.
>
> — George Box

Twenty-one chapters ago this book promised that the codebase's history was
its curriculum. This chapter was not planned; the history kept happening.

Shortly after the library was announced on the music-dsp mailing list,
Robert Bristow-Johnson replied. If you have spent any time in audio DSP you
have already used his work — the *Audio EQ Cookbook* biquad formulas ship
inside more products than anyone can count — and when someone like that
sends you two screenfuls of specific, quantitative feedback about your
polyphase interpolator, you have been handed something precious and
slightly dangerous. Precious, because expert attention is the scarcest
resource an open project has. Dangerous, because expertise arrives wearing
authority, and authority tempts you to *believe* instead of *measure* — in
either direction. The wrong responses are reflexive deference and reflexive
defense, and they are equally cheap.

This chapter is the record of the third response. Every quantitative claim
in that email was tested against this library's actual filters; the results
were committed as an assertion-pinned notebook
(`notebooks/asrc_rbj_analysis.ipynb`); the claims that survived changed the
shipped code; the ones that didn't got measured refutations rather than
arguments. It is the shortest chapter-sized demonstration this project can
offer of its entire method — and it ends with the library measurably better
than it started.

## Verify before you argue

The email made five checkable claims. Rather than debate any of them, the
first move was a notebook that fractionally resamples test tones through
the real prototypes — the same formula-for-formula design math the book's
figures use — and measures each claim's numbers directly.

His rules of thumb were *astonishingly* good. He wrote, from memory, that
linear inter-phase interpolation yields about 108 dB of folded-image S/N at
L=256 and 120-plus at L=512, improving 12 dB per doubling (6 dB for
drop-sample interpolation). Measured, worst-case tone, our filters:
**108.6 dB, 120.7 dB, +12.0 dB, +6.0 dB.** Four figures, all inside half a
decibel of a number a man typed into an email from recollection. When
someone's calibration is that good, the claims that *disagree* with your
design deserve real work, not dismissal.

Two of them did disagree.

## The claim that didn't survive

"Ooooh, that's a lotta taps. I think you can get away with T=32."

Measured: no — not at this library's specification. T buys the FIR's own
transition width and stopband depth, and pairing T=32 (whose achievable
design at our band edges is the 96 dB `fast` tier) with even L=512 tops out
at **84 dB** at 19.5 kHz, because the FIR stopband dominates long before
phase count matters. The harris length estimate the kaiser chapter walked
through says the same thing analytically: 120 dB across an 8 kHz transition
wants 47 taps from a Kaiser design, maybe 44 from Parks–McClellan. T=48
*is* the 120 dB spec.

But hold that thought, because "at this library's specification" is doing
more work in that paragraph than it appears to.

## The claim that changed the library

The most interesting suggestion was one this project had no machinery to
even evaluate: put transmission zeros at every integer multiple of the
sample rate, by convolving the prototype with a one-sample rect.

The geometry of why this matters: a component at frequency f leaves
residual images clustered around every k·fs ± f. A 997 Hz tone's images
hug 47.0 and 49.0 kHz; a 19.5 kHz tone's sit far away at 28.5 and
67.5 kHz. Real program material concentrates its energy in the bottom
octaves — which means *the images of most of the energy in real music
cluster tightly around the k·fs points*. A zero planted exactly there
delivers enormous rejection precisely where the program's alias energy
lands, and progressively less where there is progressively less energy to
protect.

The catch he didn't dwell on: the rect multiplies the whole response by
sinc(f/fs), which is not flat below Nyquist either — **−2.64 dB at
20 kHz**, a sagging treble shelf that violates this library's ±0.01 dB
passband contract by two orders of magnitude. The naive version of the
idea is disqualified on arrival. The productionized version pre-tilts the
design target by 1/sinc(f/fs) so the composite comes out flat *and*
zeroed:

```cpp
{{#include ../../../include/srt/detail/kaiser.h:pw_comp_design}}
```

Two implementation details earned their scars. The cosine-series trick is
what keeps the design closed-form — 1/sinc as a sum of cos(2πmf) terms
turns, in the time domain, into the same brickwall sinc evaluated at a few
integer shifts, so no FFT enters a header that never needed one. And the
rect is applied as a running sum *after* windowing, which makes the zeros
exact in the discrete composite:

```cpp
{{#include ../../../include/srt/detail/kaiser.h:pw_comp_rect}}
```

One bug in between is worth its paragraph, because the test suite caught
it exactly as designed. An even-length rect delays by (L−1)/2 fine-grid
samples — half a sample off the slicing convention every other design in
this file obeys. The first build measured −72 dB of error at 1 kHz rising
6 dB per octave: the unmistakable spectral signature of a pure delay
error, printed by the fractional-delay accuracy tests within seconds. The
fix is one line (center the pre-rect kernel at n/2, not (n−1)/2); the
lesson is the spsc-ring chapter's lesson again — the failure signature of
an alignment bug is so distinctive that a test built to measure the right
thing diagnoses the bug for you.

Every preset except `fast` now ships this design:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:pw_image_zeros}}
```

At *equal tap count*, `balanced`'s passband stays flat to ±0.003 dB, its
worst-case near-Nyquist numbers are unchanged — and a 997 Hz tone's folded
images drop from −144 to −158 dB. Free, in the only currency the audio
path accepts.

## Two specifications, both defensible

Now return to "you can get away with T=32," because the zeros explain what
he actually meant.

His design philosophy, legible in retrospect: don't spend taps buying
120 dB of stopband *uniformly*, including at frequencies where real
program material has almost no energy. Buy ~96 dB uniformly with T=32,
plant the k·fs zeros, and the **program-weighted** alias performance —
the expectation over a realistic spectrum — lands near what a uniform
120 dB filter achieves, at two-thirds the per-sample compute. Under his
metric, his tap budget is right. Under this library's worst-case-sine
metric, it is refuted. The thread's disagreement was never really about
32 versus 48; it was about which promise the number T serves.

That is worth engraving somewhere, so here: **a filter specification is a
claim about which signals you promise to protect.** This library's suite
promised worst-case sines — the thing an AES17 bench or a skeptical
reviewer with a signal generator will measure. His designs promise music.
Both are coherent; they price taps differently; and honesty requires
saying which one any given number is quoting.

Which raised an uncomfortable fact: this project's proof system could not
*test* his promise. Every quality gate in the suite was a single sine. A
preset built on program-weighted reasoning would have been unverifiable —
and the third chapter of Part II was blunt about what an unverifiable
claim is worth here.

## Building the instrument

So the suite grew a new instrument: a deterministic 24-tone comb,
log-spaced 60 Hz–16 kHz, amplitudes pink (equal energy per octave — the
long-run average of real program), phases chosen for bounded crest:

```cpp
{{#include ../../../tests/support/multitone_analysis.h:pw_comb}}
```

The measurement fits every tone jointly and calls everything left over —
aliases, servo FM, noise — the residual:

```cpp
{{#include ../../../tests/support/multitone_analysis.h:pw_metric}}
```

The instrument fought back twice, and both fights are preserved in its
comments because each one is a measurement lesson. Sequential
fit-subtract — fit the loudest tone, subtract, repeat — is the obvious
implementation and it *floors at 48 dB* on synthetic, mathematically exact
tones: 24 tones on a rectangular window leak into each other far above the
−120 dB territory being measured, and Gauss–Seidel iteration over that
coupling converges too slowly to be an instrument. The joint solve fixed
that. Then the classic hygiene step of subtracting the sample mean turned
out to *also* floor the instrument at 48 dB: a finite window of pure tones
has a legitimate nonzero mean (the partial cycles of the low tones), and
removing it injects a constant the sine basis cannot absorb. DC became a
column of the joint fit instead.

How do you know your instrument is measuring the converter and not
itself? The same way the notebooks chapter answered: calibrate against a
known-perfect input. `ProgramWeighted.InstrumentFloor` feeds the analysis
synthetic exact tones (with a deliberate 0.137 ppm ratio offset, mimicking
servo settling residue) and demands the float-quantization floor back —
measured 151.9 dB, gated at 145. Both 48 dB failures above were caught by
exactly this test. *The first principle is that you must not fool
yourself,* even — especially — inside your own test support headers.

## The outcome

With the instrument in place, the suggestion could finally become a
shippable preset:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:pw_economy}}
```

And the promise could be measured instead of asserted:

```cpp
{{#include ../../../tests/test_asrc_program.cpp:pw_measure}}
```

The numbers, end to end through the full converter at +200 ppm:

| preset | program-weighted | worst-case 19.5 kHz sine | per-sample compute |
|---|---|---|---|
| `balanced` | **134.5 dB** | 105 dB | 1.0× |
| `economy` | **131.6 dB** | 77 dB | 0.67× |

Three decibels of program-weighted quality for a third of the compute and
0.16 ms of latency — with the worst-case number it trades printed in its
documentation, its tests, and this table, because the difference between
an economy tier and a dishonest one is whether the trade is visible.

A postscript on the experiment that *didn't* produce a code change: his
suggestion to swap the kernel ordering (two dot products, then interpolate
the outputs) benchmarked 1.3× slower on one host and 1.4× *faster* on
another — the ordered-double accumulation contract makes each dot a serial
dependency chain, his form carries two independent chains that overlap in
the pipeline, and which effect wins depends on the machine. It stands
recorded in the notebook as an honest draw, with the multichannel case
still settled by C1's measurements. Not every good question has a
portable answer; the record is the deliverable.

## Postscript: the script arrives

A week after the exchange, his MATLAB script did arrive — updated the same
day so the Kaiser-windowed method got the sinc² zeros too, and validated,
by his own report, at R=512 and N=32: the exact shape of `economy()`. With
it came one more claim, stated with his usual precision: *"for DC, this
should get you infinite S/N ratio... for every phase or fractional delay,
the FIR coefficients must add to 1."*

Checked, and better than checked. In double precision the property falls
out of the rect construction *automatically*: the compensated designs'
branch sums are uniform to a spread of 1.8×10⁻¹⁵ — machine epsilon —
because zeros at k·fs and branch-DC uniformity are the same fact stated in
two domains (the plain design's 4.7×10⁻⁶ spread is its stopband leakage at
fs, visible from the other side). But his accompanying warning — that
16-bit coefficient quantization needs "tricky things" — turned out to be
pointing at a live defect: independent per-tap rounding was re-breaking
the property by several LSB, a bias this suite had measured and papered
over with a widened tolerance earlier in this very chapter's story. The
tricky thing is row-sum-preserving quantization:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:pw_row_sum}}
```

After it, the measured result is the one his claim demanded: a DC input
passes through the Q15 and Q31 converters **bit-exactly at every
fractional delay** — zero LSB of deviation over a 256-point μ sweep — and
`FixedPoint.RowSumsAreExact*` pins every row's sum to exactly one, in
coefficient units, forever. Infinite S/N at DC, delivered in 16 bits.

## What this chapter is actually about

Strip the DSP away and the shape of the episode is this: an expert
stranger looked at public work and said *your numbers are bigger than they
need to be, here is a trick, and by the way what is "occupancy."* The
project's response machinery — notebook verification, adversarial
measurement of its own instrument, a spec-honest preset, and this chapter
— took about two days. Nothing in that machinery was built for this
episode; it is the same ratchet-and-verify culture every previous chapter
described, pointed for the first time at feedback from outside.

Models of quality are like models of anything: wrong, some useful. The
worst-case sine model is wrong about music and useful for guarantees. The
program-weighted model is wrong about your synthesizer's 19 kHz test tone
and useful for the power budget of a conference-room DSP. The library now
ships both models, measured, labeled, and disagreeing with each other in
public — which is the most useful thing two wrong models can do.

## Verify it yourself

```sh
# The verification notebook: every claim from the thread, assertion-pinned
jupyter nbconvert --execute --to notebook notebooks/asrc_rbj_analysis.ipynb

# The compensated designs meet spec (ripple, stopband, exact k*fs zeros),
# including at 16 kHz, plus the economy preset:
ctest --test-dir build -R 'Kaiser' --output-on-failure

# The program-weighted metric: instrument floor first, then both presets
# and the economy worst-case honesty line:
ctest --test-dir build -R 'ProgramWeighted' --output-on-failure

# The half-sample alignment sentinel that caught the rect-centering bug:
ctest --test-dir build -R 'FractionalDelay' --output-on-failure

# The postscript's claims: branch-DC uniformity at machine epsilon, exact
# fixed-point row sums, bit-exact DC at every fractional delay:
ctest --test-dir build -R 'BranchSums|RowSums|DcGain' --output-on-failure
```

And one experiment in the spirit of the thread: change `image_zeros` to
`false` in `filter_spec::economy()` and rerun `ProgramWeighted` — the
program-weighted number collapses toward the mid-90s while the worst-case
sine barely moves. That difference *is* RBJ's argument, measured; the
preset is just the argument, shipped.
