# One filter per stage: nyquist.h and design.h

> Perfection is achieved, not when there is nothing more to add, but when there is nothing left to take away.
>
> — Antoine de Saint-Exupéry, *Terre des hommes*

The ASRC's filter chapter designed one prototype — a Kaiser-windowed sinc,
12,288 taps, oversampled 256× — and the whole rest of Part I was about
*selecting* from it. `rational` designs a different filter for every stage
and never selects: each stage runs its filter whole, every tap, every
output. So the cost of a stage *is* the length of its filter, and the
design question becomes the only question. This chapter is about a
structure that answers it with half the taps missing.

## The L-th-band filter

Take the ideal lowpass with cutoff exactly π/L (in radians at the filter's
own rate) — the anti-image filter for interpolation by L, and the
anti-alias filter for decimation by L. Its impulse response is
(1/L)·sinc((n − c)/L), and sinc has zeros at every nonzero integer: so
h[c ± L] = h[c ± 2L] = … = 0. Multiply by a window (Kaiser, here) and the
zeros stay zeros, because anything times zero is zero. The result is an
**L-th-band** or **Nyquist** filter — Mintzer's 1982 term, Vaidyanathan's
§4.6 — whose every L-th tap from the centre is exactly zero, and whose
centre tap is exactly 1/L.

Why that matters is a counting argument, and the engine is built on it.
Interpolation by L runs an L-phase polyphase decomposition of the
prototype; phase 0 — the branch holding the centre tap — holds *only* the
centre tap and the zeros, so it is a copy of the input, zero
multiplications. The other L − 1 phases hold everything else. For a
half-band (L = 2) filter of length N = 4m − 1, only 2m + 1 taps are
nonzero; a decimator by 2 computes 2m + 1 multiply-accumulates per output
where a generic FIR of the same length computes 4m − 1. The structure
halves the work before any optimization starts, and it does so without a
branch in the inner loop: the zeros are simply not in the table (next
chapter).

The second consequence is in frequency. An L-th-band filter's response is
antisymmetric about π/L — the Nyquist property, Σₖ H(ω − 2πk/L) = 1 — so
its passband edge and stopband edge sit mirror-image around the lower
rate's half: f_p + f_s = r. The family's coverage rule asks every stage to
keep its stopband edge at or below r − f_pass, so that no image of
passband content lands inside the passband. The Nyquist structure gives
that bound *with equality*. The structure is the rule; the engine does not
have to enforce it.

## The designer

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/nyquist.h:nyq_design}}
```

Four things to notice, in the order the code does them.

**The length is a formula, not a parameter.** N = 2mL − 1 for an integer m,
the centre c = mL − 1, and `is_nyquist_length` rejects everything else. An
odd length makes the group delay (N − 1)/2 an integer number of samples at
the filter's rate — a fact the stage reports as an exact rational and the
chain adds up exactly. The free parameter is m, *taps per branch*; the
spec search below steps it, so tap counts step by 2L.

**The window is mirrored, not recomputed.** The Kaiser loop computes the
Bessel series for the first half and copies it to the second
(`h[i] = h[mirror]`), so the two halves of the design are *bit-identical*
rather than merely symmetric-within-rounding. This sounds like a
micro-optimization; it is a reproducibility decision, and the fixed-point
chapter will show exactly how much it is worth.

**The zeros are written as zeros.** libm's sin(πk)/(πk) is on the order of
10⁻¹⁶, not 0, and the sinc is multiplied by a window and a gain before it
lands in the table. The contract says 0 — because the fixed-point tables
are bit-pinned, because a 10⁻¹⁶ tap costs a multiply in a loop that was
promised to skip it, and because "exactly zero" is a property a test can
assert with `EXPECT_EQ` where "small" needs a tolerance somebody has to
defend. So the designer tests the index, not the value, and stores `0.0`.

**Every branch is normalized to DC gain 1.** The centre's branch is the
single tap h[c] = 1.0 already (the designer sets it exactly, not as
`inv_l * sinc(0) * window`); every other branch — the taps i ≡ j (mod L) —
is scaled so its sum is 1 within double rounding. The whole filter then
sums to L, kaiser.h's `design_prototype` convention. The reason is the one
R. Bristow-Johnson gave the ASRC in the epilogue: a polyphase branch whose
taps sum to 1 passes DC with no gain error at that phase, and the row-sum
quantization of `quantize.h` carries that exactness into Q15 and Q31. Here
there is a bonus the ASRC never had: because branch 0 is literally
`{1.0}`, DC through an interpolator's phase 0 is not "within an LSB" —
it is the input.

## The spec search, and the grid that matters

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/nyquist.h:nyq_search}}
```

The criterion is `bridge`'s: the smallest m whose design meets the
stopband with at least 1 dB of margin on a fine grid from the stopband
edge to half the filter rate. The grid is an argument, with a default, and
the default is wrong for this engine — which is a story worth the
paragraph.

`nyquist.h`'s default is 1024 points. The design spike that pinned every
(band, profile) pair ran on 16,384 points, and two rows disagreed with the
1024-point search: the 4th-band `balanced` design pinned at N = 103 on the
coarse grid and 111 on the fine one, the 8th-band `transparent` at 383
against 399. The cause is geometry, not a bug: a stopband's sidelobes are
spaced about F_hi/N apart, and a 1024-point sweep of a 383-tap design
*steps over* a sidelobe — the candidate reads −121.7 dB on 1024 points and
−119.3 dB on 8192, below spec. 16,384 points agree with 65,536 within
0.003 dB for every design in the table, so 16,384 is the number the engine
pins (`k_design_grid_points`), the search takes the grid as a parameter
(tap/DspTap#50 made it one), and the plan records the finding in the same
sentence as the pins: *a caller that pins a count states the grid it was
found on.* A pinned number without its instrument is folklore.

## Profiles: `bridge`'s four names, as fractions

```cpp
{{#include ../../../rational/include/tap/sr/rational/design.h:rational_profile}}
```

The four names — `super_economy`, `economy`, `balanced`, `transparent` —
are `bridge`'s, and so are the four passband edges: 16, 18, 19 and 20 kHz
at 48 kHz. `rational` states them as fractions of the chain's *lowest*
rate (1/3, 3/8, 19/48, 5/12) from day one, because the same profile must
serve a stage at 8 kHz and one at 384 kHz, and because a chain that
composes `bridge` and `rational` under one profile name should mean one
(f_pass, A) throughout. `economy` is the default, as the speed-first
charter requires; `transparent` is the 120 dB tier behind the same design
path. The attenuation is 70 dB for the three economy-side tiers, and that
number is going to matter in the fixed-point chapter, because 70 dB is
about what sixteen-bit coefficients can deliver.

The `taps_per_branch` array is the design spike's result, **pinned**: the
minimal m per band on the 16,384-point grid, verified against the shipping
designer by `Design.PinsAreMinimalAndMeasuredAt70dB` (which also checks
that m − 1 *misses* the spec — a pin that is not minimal is a pin that
drifted). The measured table, with the harris estimates the plan started
from struck through beside it:

| Band | super_economy | economy | balanced | transparent |
|---|---|---|---|---|
| 2 (↑2 / ↓2) | 35 taps, 19 nonzero (−72.8 dB); ~~27~~ | 43 / 23 (−71.9 dB); ~~35~~ | 51 / 27 (−71.5 dB); ~~43~~ | 123 / 63 (−121.7 dB); ~~95~~ |
| 3 (↑3 / ↓3) | 47 / 33 (−71.2 dB); ~~41~~ | 65 / 45 (−71.3 dB); ~~53~~ | 77 / 53 (−71.2 dB); ~~65~~ | 149 / 101 (−121.1 dB); ~~143~~ |
| 4 (serves 4/3, 3/4) | 63 / 49 (−71.4 dB) | 87 / 67 (−71.1 dB) | 111 / 85 (−72.3 dB) | 199 / 151 (−121.4 dB) |
| 6 | 95 / 81 (−71.5 dB) | 143 / 121 (−72.1 dB) | 167 / 141 (−72.2 dB) | 299 / 251 (−121.3 dB) |
| 8 | 127 / 113 (−71.5 dB) | 191 / 169 (−72.1 dB) | 223 / 197 (−72.1 dB) | 399 / 351 (−121.2 dB) |

The Kaiser fit needs 2–8 more taps per branch than the harris estimate at
the half band and 1–7 at the third. The estimates are kept in the plan as
the *derivation* of the factoring rules; the pins are what ships, and a
later chapter's ledger records every place the two disagreed about which
chain to build.

Two properties of the table that the tests pin and the engine leans on:

- **Up and down are the same prototype.** A Nyquist filter is its own
  transpose up to the gain convention, so `design_stage<ratio<2, 1>>` and
  `design_stage<ratio<1, 2>>` return the same vector bit for bit
  (`Design.UpAndDownOfOneBandAreTheSamePrototype`), and one pin serves both
  directions. `bridge`, whose 147/160 and 160/147 designs are genuinely
  asymmetric, has no such luck and pins each.
- **A mixed ratio's band is its larger factor.** 2/3 and 3/2 share the
  third-band design; 3/8 and 8/3 the 8th-band. The mixed stage's cost per
  output is its taps per *phase* — ⌈N/L⌉, the last phase zero-padded —
  which is how a 2/3 stage at `economy` comes to cost 32.5 MACs per
  output over a 65-tap design.

## From profile to filter

```cpp
{{#include ../../../rational/include/tap/sr/rational/design.h:rational_design}}
```

`design_stage<R>` is twelve lines because everything it needs is already a
constant: the band from the traits, m from the profile's pin (or the
search, for a custom profile that carries none), the length from the
formula, β from the attenuation. The passband fraction it hands the
designer is *of the stage's lower rate* — which is the designer's own
argument — and the reason that is correct for every ratio of the charter is
the identity the previous chapter's traits assert: the composite rate is
the band times the lower rate, going up or down.

What the function does **not** do is as deliberate as what it does. It does
not call kaiser.h's `design_prototype_compensated` — the sinc²-zero variant
the ASRC's epilogue adopted after Robert's letter — because that design
convolves the prototype with a rectangle, which *fills the structural
zeros*. A compensated L-th-band filter is a different filter with a
different contract, and the plan lists it as a risk (S2) with the property
test in DspTap as the tripwire. Nor does it touch DspTap's `decimate.h`:
that primitive keeps its own design (an asymmetric transition, no zero
taps, 16 kHz output) for the wake-word front end that owns it, and because
the two designs differ, the stage test uses `decimate.h` as a *second
golden* at ↓2, ↓3 and ↓6 — agreement within the documented difference
between the two designs, and anything beyond it is a finding.

## Why this design looks the way it does

| Decision | Alternative rejected | Reason |
|---|---|---|
| One L-th-band filter per stage | a generic lowpass of free length | zeros at every L-th tap cost nothing (phase 0 of ↑L is a copy; ↓2 computes 2m + 1 of 4m − 1); the symmetric transition is the coverage rule with equality |
| N = 2mL − 1, odd | any length | integer group delay at the filter rate; the zeros land on c ± kL by construction |
| Zeros written as exact `0.0` | leave libm's ~10⁻¹⁶ | the fixed-point tables are bit-pinned; the trimmed rows must skip exactly the taps the contract says are zero |
| Mirror the window | compute both halves | bit-identical halves; see the fixed-point chapter for what that buys across libms |
| Per-branch DC normalization | normalize the whole filter | each branch passes DC with no gain error; branch 0 of ↑L is exactly `{1.0}` |
| Pins per (band, profile) on a 16,384-point grid | search at construction with the 1024-point default | the default steps over a sidelobe of the long designs (383 → 399 at band 8 `transparent`); construction stays cheap; the grid is part of the pin |
| Profile edges as fractions of the lowest rate | hertz | one profile serves a stage at any rate; a composed chain means one (f_pass, A) under one name |
| Four named tiers, `economy` default | a continuous (f_pass, A) | the matrix pins only the named four; a custom profile exists but is searched and unpinned |
| Keep `decimate.h` separate | adopt the Nyquist designer there | different contract for a different consumer; the two designs cross-check each other |

## Verify it yourself

```sh
# The designer's property test lives in DspTap (exact centre and zeros,
# per-branch unity, the shifted responses summing to 1):
cmake -S submodules/dsptap -B build-dsp && cmake --build build-dsp -j
ctest --test-dir build-dsp -R Nyquist --output-on-failure

# The engine's design battery: the pins are the M2 table and minimal
# (m - 1 misses), every band meets the spec with the margin, up equals
# down bit for bit, the coarse grid would under-pin two rows:
ctest --test-dir build -R 'rational\.Design\.' --output-on-failure

# The independent numpy leg that found the pins, re-executed:
jupyter nbconvert --to notebook --execute --inplace rational/notebooks/design_spike.ipynb

# Break it on purpose: change k_design_grid_points to 1024 in design.h.
# Design.PinsAreMinimalAndMeasuredAtTransparent fails: the 8th-band pin
# is no longer minimal on the coarse grid, because the grid can no longer
# see the sidelobe that made it necessary.
```
