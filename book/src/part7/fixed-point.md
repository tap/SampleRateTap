# Sixteen bits, per stage

> The purpose of computing is insight, not numbers.
>
> — Richard Hamming, *Numerical Methods for Scientists and Engineers*

Part I's sample-traits chapter set up the family's ladder — double is the
golden model, float the embedded profile, Q15 and Q31 the format-limited
embedded profiles — and `rational` inherits it unchanged: `basic_stage<S,
R>` is one template over `tap::dsp::sample_traits`, so the Q15 and Q31
datapaths were there the day the float one worked. What this chapter adds
is the measurement discipline the plan calls M5: every stage, every
profile, every format, numbers stated and pinned, and *two honest limits*
that a sixteen-bit coefficient imposes which took two further rounds of
engineering to lift. It ends with a bug that was not in the fixed-point
code at all.

## What the ladder promises here

- **Q31 tracks double within 3.4 × 10⁻⁹** of full scale on the reference
  noise, against float's 5.1 × 10⁻⁸: a decade under float's floor, as
  `bridge` found, so Q31 is for all purposes the float profile with
  integer arithmetic.
- **Every row sums to exact unity in both formats** — each phase row's
  Q1.14 coefficients to 16,384 and Q1.30's to 2³⁰ — through `quantize.h`'s
  row-sum preservation, applied to the stored row (`FixedPoint.EveryRowSumsToExactUnityInBothFormats`). Consequence: full-scale DC of either sign
  comes out at exactly full scale from every phase
  (`FullScaleDcIsExactFromEveryPhase`), and ±full-scale noise lands within
  8.8 × 10⁻⁴ (Q15) and 1.6 × 10⁻⁸ (Q31) of the clamped double model —
  saturation, never a wrap.
- **The tables are bit-pinned**: an FNV-1a-64 hash per (ratio, profile,
  format), 112 pins at M5. A change that moves a pin is a numeric change
  to the fixed-point datapath, by definition, and the plan says so.
- **Q15 is format-limited and stated per stage.** At `economy` the
  attained stopband — the quantized Q1.14 table's worst response on the
  16,384-point design grid — is between −70.0 and −72.3 dB, RMS deviation
  from double −92 to −99 dBFS. At `transparent` it is −76 to −83 dB, not
  120: the outer taps of a long design round to zero in Q1.14 and are
  trimmed, so Q15 MACs fall *below* float's (55 of 63 for ↑2). The 120 dB
  promise is float's and Q31's; at Q15, `economy` is the pairing, as
  `bridge`'s battery concluded before this engine existed.

Those are the numbers after the two limits were lifted. Here is how they
looked before, and what lifting them cost.

## Limit one: a decimator's row is M times too small

A decimator's table at M5 was h/M: the whole filter at sum 1, quantized as
one row so that unity survived the format exactly. Each coefficient is
therefore M times smaller than the interpolator's of the same band, on the
same Q1.14 LSB — which is to say it carries log₂ M fewer bits — and the
quantization noise floor of the response rises by about 20·log₁₀ M. ↓2
lost 6 dB of headroom it could afford; ↓6 reached −64.7 dB and ↓8 −62.9 dB
at `economy`, and ↓8 −61.1 dB at `transparent`. The plan recorded these
as a stated limit and moved on, because the fix needed substrate.

The fix is a bookkeeping change that moves one division from the table
into the rounding. Every branch of a Nyquist filter sums to 1 (previous
chapters), so quantize each of the M branches *at its own unity* — which
makes the Q15 decimator's table bit for bit the Q15 interpolator's table
of the same band (`FixedPoint.Q15DecimatorTableIsTheInterpolatorsTable`
pins exactly that) — and apply the 1/M where the stage already has one
rounding point, the finalize:

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/sample_traits.h:st_finalize_divided}}
```

For M = 2 and 8 the divide is a shift folded into the finalize's own: one
rounding, exact. For M = 3 and 6 there is no shift, and the function
multiplies by a fixed-point reciprocal chosen so that the result is exact
*at DC*: full-scale DC through ↓3 still comes out at exactly full scale,
which the test checks from every input phase. The stage's side of the
contract is the `k_table_gain` constant the previous chapter showed: it
states what the rows sum to (M, for a Q15 decimator), and
`finalize_output` is the one function that divides it back out.

Measured after it: every Q15 decimator attains its band's interpolator
stopband — ↓6 −71.5 dB, ↓8 −71.7 dB at `economy`; −76 to −79 dB at
`transparent` — and the Q15 RMS deviation from double fell by 5–12 dB.
One cell moved against the trend and is recorded as such: ↓3 at `economy`
went from −72.4 to −70.2 dB, still the tier. Q31 and float were
untouched, as they should be: the lever changes the Q15 table and the Q15
finalize and nothing else, and `FixedPoint.TablesAreBitPinned` is the
instrument that proves the "nothing else".

DspTap's own `decimate.h` — the wake-word front end's decimator, a
different design — had the same shrinkage and took the same lever in the
same week: its Q15 economy stopbands by 2, 3 and 6 went from
−68.8 / −66.6 / −61.1 dB to −72.3 / −71.7 / −71.0 dB. The substrate rule
earned its keep in the obvious direction (the helper landed in
`sample_traits.h` once) and in the less obvious one (a limit found in one
consumer was a limit in the other).

## Limit two: a mixed ratio going down

A mixed ratio going down — 2/3, 3/4, 3/8 — has the same shrinkage at a
smaller factor, from a different cause. Its L-phase rows are normalized to
unity each, and a row of a band-M design strided by L has its centre tap
near L/M: 0.67 for 2/3, 0.75 for 3/4, 0.38 for 3/8. Part of the Q1.14
range sits unused, and the three stages attained −68.2 to −69.7 dB at the
70 dB tiers — the last Q15 cells short of their tier.

The remedy is the decimator's at a power of two: hold each row at a gain
G, the largest power of two with G·L/M < 2 (so every tap still fits
Q1.14) — 2 for 2/3 and 3/4, 4 for 3/8 — and shift it back out in the
rounding. `k_mixed_down_gain` computes G at compile time;
`finalize_divided` by a power of two is a shift. Measured after it, every
Q15 stage attains the 70 dB tier at every 70 dB profile: the mixed ratios
going down at −70.0 to −72.4 dB, their RMS from double 4–10 dB lower. And
again one cell went the wrong way and is written down: 3/4 at `economy`
moved from −72.8 to −70.02 dB, its tightest cell, in the tier with no
margin left, while 3/4 at `balanced` moved from −68.2 to −72.4. Twelve
Q15 pins moved, as the lever's PR lists them.

## The table that differed between host and target

This is the bug the chapter was leading to, and it is worth the detail
because it is the kind of bug the family's test discipline exists to
catch and the kind that looks, at first, like something else.

Every change that moves a pinned table is run on the Cortex-M33 and M55
QEMU legs as well as on the host, and the mixed-ratio lever's first run
on the M33 pinned a *different* 2/3 `super_economy` Q15 table from the
host's. Not wrong — both rows summed to exactly 2 × 16,384, both passed
every contract test — just different, by two coefficients:

```text
host  row 1:  …  -200  361  …  361  -201  …
M33   row 1:  …  -201  361  …  361  -200  …
```

A mirror pair, one LSB moved from one member to the other. The quantizer's
largest-remainder step had gone to index 4 on the host and index 20 on
the target. `quantize_row_preserving_sum` breaks a tie by taking the first
(lowest) index — it is a strict comparison — so on the host the two
remainders were exactly equal and on the target they were not. The
immediate fix in that PR snapped the scaled row to a 2⁻³⁴ grid before
quantizing, which made the tie exact everywhere; the finding — *the same
could happen to any table with a mirrored pair in one row* — was handed
upstream.

The upstream investigation is why this chapter can tell you the actual
cause. The designer mirrors its Kaiser window bit for bit, the sinc is
even, and the mirror pair's values *were* bit-identical in the design — up
to the per-branch normalization. Branch j of an L-th-band design and its
mirror branch (−2 − j) mod L hold the same values in reverse order, and
each is summed in increasing index order to get its gain. Two sums of the
same numbers in opposite orders differ in the last bit or they don't,
depending on the numbers; under glibc the third-band m = 8 design's two
sums happened to agree, and under newlib, whose libm leaves the
design's last bits a little different from glibc's, they happened not to. The quantizer was
never at fault. It had been handed a row whose mirror pair was an ulp
apart, and it did what its contract said.

The root fix is one loop in `nyquist.h`: after the normalization, copy the
second half of the design from the first, so h[i] == h[N − 1 − i] is a
contract point rather than a probability (`Nyquist.IsSymmetricBitForBit`
holds it for every band at every tier's length). Each branch still sums to
1 within double rounding; the quantizer's tie rule is now stated in its
docstring and pinned by `Quantize.TiesStepTheLowestIndex`; and the snap in
`stage.h` is gone, because a mitigation for a cause that no longer exists
is a trap for the next reader. The tables this book's tree ships are the
same on the host and under QEMU on both Cortex-M cores, and the sentence
in the plan that pins them says why.

Two lessons, neither about fixed point. A bit-pinned table across targets
is a stronger test than a tolerance across targets, because it fails on
the ulp and makes you find it. And "the tie-break is platform-fragile" was
a true description of the symptom and a wrong description of the cause;
the general fix that description suggested — a tolerance in the quantizer
— would have hidden the asymmetric design under it.

## Helium, briefly

One more Q15 number belongs here because it changed what "the flagship
embedded profile" means. At M6, Q15 on the Cortex-M55 was *no faster than
float* — ↓2 at 25.07 M instructions against float's 24.58 M — where on
the M33 it ran in 7.7–15× fewer instructions. The substrate's Q15 dot was
one scalar SMLALBB per tap under GCC 13, which does not auto-vectorize it
for Helium. DspTap's MVE kernel (eight lanes per VMLALDAVA, a predicated
tail, bit-exact against the scalar form at every tap count) took the M55
Q15 workloads down 21 % (↓2), 28 % (↑2), 32 % (↑3) and 48 % (the by-4
chain), with every output bit and every other target's count unchanged.
Q15 is now 19–32 % under float on the M55. What remains per output is the
stage's own bookkeeping around the dot, which the instruction-count
chapter of Part II taught you how to read.

## Why the fixed-point design looks the way it does

| Decision | Alternative rejected | Reason |
|---|---|---|
| Q15 decimator branches quantized at unity, 1/M in the finalize | the whole filter at h/M quantized once | h/M costs 20·log₁₀ M dB of Q15 stopband; the branches' table is the interpolator's, and the single rounding point can carry the divide |
| Exact-at-DC reciprocal for M = 3, 6 | floor division | full-scale DC must still be exactly full scale from every phase |
| Mixed-down rows at a power-of-two gain G | rows at unity | the centre tap near L/M wastes Q1.14 range; a shift is free in the finalize |
| Bit-pinned tables per (ratio, profile, format) | tolerance against double | a pin fails on the ulp; it is how the host/target difference was found at all |
| Mirror the design in the designer | a tolerance tie-break in the quantizer | the asymmetry was in the input; a tolerance would have hidden it and left float tables an ulp apart across libms |
| Remove the 2⁻³⁴ snap once the cause is fixed | keep it as belt and braces | a mitigation without a cause is a trap for the next reader |
| State Q15 `transparent` at −76 … −83 dB | widen the format or drop the tier | the outer taps round to zero; the tier is float's and Q31's; `economy` is the Q15 pairing |

## Verify it yourself

```sh
# The fixed-point battery: unity rows, the pins, the decimator's table is
# the interpolator's, zeros out of the dot, Q31 vs double, the attained
# stopbands (a 16384-point DFT per table: the slow one), saturation, DC:
ctest --test-dir build -R 'rational\.FixedPoint\.' --output-on-failure

# The same tables on a Cortex-M33 under QEMU (the pins are the same hashes):
cmake -S . -B build-m33 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake
cmake --build build-m33 -j && ctest --test-dir build-m33 -R rational --output-on-failure

# The designer's exact symmetry and the quantizer's tie rule, in DspTap:
ctest --test-dir build-dsp -R 'IsSymmetricBitForBit|TiesStepTheLowestIndex' --output-on-failure

# Break it on purpose: in design_nyquist, delete the mirror loop at the end.
# Every host test still passes — glibc's branch sums agree — which is the
# whole point: a bug you can only see from a second libm needs a second
# libm in CI, and the M33 leg is it.
```
