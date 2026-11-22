# The band the half-band leaves: guard_band.h

> It suppresses the guard band, from the passband edge to the low rate's
> Nyquist.
>
> — `docs/IIR_PLAN.md`, section 1

Every half-band in this engine crosses over at a quarter of its higher rate,
which is the lower rate's Nyquist. There the recursive half-band is 3 dB down,
because it is power complementary, and the FIR Nyquist filter 6 dB down.
Decimating, the half-band's transition band folds onto the band between the
passband edge and the low Nyquist: the *guard band*. Interpolating, low-rate
content in the guard band images into the transition band. No profile
promises anything there, and the passband stays alias-free to the profile
either way, since the half-band's stopband is exactly what folds onto it. But
a low-rate output that is listened to as it is, a nonlinear process at the
low rate, or an FFT bin near Nyquist sees the aliases, at nearly full level.

## A stage that changes no rate

The fix is a low-pass at the low rate, after the decimator or before the
interpolator. It serves FIR and recursive stages alike, and it does not
touch either: an option on every stage would have doubled their surface. So
it is a stage of its own, `guard_band_filter<S, Ch, P>`, whose ratio is 1:1.
`ratio<L, M>`'s charter still refuses L = M; the filter is not a ratio, and
the plan admits it beside the ratios. DspTap's `chain` already took a 1:1
stage, and its battery pins that it does.

Its form is in the type, as a recursive stage's phase is:

```cpp
{{#include ../../../rational/include/tap/sr/rational/guard_band.h:rational_guard_phase}}
```

The nonlinear form is an odd-order Chebyshev II: a flat passband, an
equiripple stopband, a steep transition, and, because its order is odd, a
first-order section whose zero sits exactly at z = −1, the crossover itself.
The linear form is an even-length symmetric FIR, type II, whose symmetry puts
the same zero at Nyquist. The first is the cheaper; the second keeps a
chain's phase linear, which is the only reason it exists.

## Three numbers and an order

A Chebyshev II has no single corner frequency. It has a passband edge, the
profile's, shared with the stages so that `economy` means one thing
everywhere; a stopband edge, set by its order; and its zero at Nyquist. The
settings name the first two and the order:

```cpp
{{#include ../../../rational/include/tap/sr/rational/guard_band.h:rational_guard_band_settings}}
```

The order is a rule's. The spike proposed it and the maintainer took it: the
lowest odd order whose worst alias in the guard band, behind the nonlinear
half-band, is at or below −A/2. That is 5 at `super_economy` and 7 at the
other three, and the battery recomputes it from the designs: −40.1, −43.5,
−40.2 and −61.6 dB at those orders. The linear form reaches A from the same
stop edge, from DspTap's committed Parks–McClellan rows: 30, 50, 58 and 72
taps.

The rule belongs to the half-band it was computed behind. Behind the
near-linear half-band or the FIR one, whose transitions are shaped
differently, the same orders leave the guard band's worst alias at −31 dB at
`super_economy` and −46 to −49 dB at `transparent`, above −A/2; the rule would
pick 7 and 11 there. Nothing the filter promises moves (the passband, and A
from the stop edge, hold behind every half-band), so the battery pins the
numbers and the plan puts the choice to the maintainer.

## The same machine, twice

The filter is two machines in one template, chosen at compile time by the
form. The nonlinear one calls DspTap's `biquad_cascade` per channel per frame,
as a recursive stage calls its commutator step; the linear one keeps a
history per channel and calls `dot_row`, as an FIR stage does:

```cpp
{{#include ../../../rational/include/tap/sr/rational/guard_band.h:rational_guard_band_run}}
```

Everything else follows the house pattern: a base compiled once per format
and form, `process`, `pull` and `flush` out of line against a cursor, and a
`validate()` that states every rejection in the constructor's words. A custom
setting designs at construction: the Chebyshev II through the same constexpr
functions the named settings ran at compile time, the linear form through
DspTap's Kaiser designer, which is not equiripple and so runs longer (82 taps
where the committed row has 64). Every valid Chebyshev II setting fits the
fixed-point formats, so `validate()` has no format rejection: the battery
scans the settings and finds no coefficient at 2.

## Two latencies

The Chebyshev's phase is nonlinear, so it reports what a recursive stage
reports, and the linear form what an FIR stage reports:

```cpp
{{#include ../../../rational/include/tap/sr/rational/guard_band.h:rational_guard_band_latency}}
```

At `economy` the Chebyshev II delays 0.54 frames at DC and 5.04 at the
passband edge, where its poles are; the FIR delays 49/2 frames at every
frequency, exactly. That is the trade the two forms are for. An all-FIR chain
holding the linear form keeps its exact latency (after the FIR half-band
decimator, 19/2 + 49/2 = 34 frames), and a near-linear stage followed by it
keeps an exact delay, so that chain still enters the round-trip scaffold.

## The offering

The plan's decision was not to name IIR chains or give them rows in the
coverage matrix, but to state the choices in a small table: by 2, in each
recursive variant, with no filter or either form. At `economy`, going down:

| ↓2 at `economy` | multiplies per output | nominal latency, frames (deviation) |
|---|---:|---:|
| nonlinear | 4 | 1.20 (1.28) |
| nonlinear + Chebyshev II | 24 | 1.73 (5.79) |
| nonlinear + linear form | 54 | 25.70 (1.28) |
| near-linear | 7 | 7 (0.0087) |
| near-linear + Chebyshev II | 27 | 7.54 (4.52) |
| near-linear + linear form | 57 | 31.5 (0.0087) |

The full table, every profile and both directions, is in `rational`'s README
and is pinned cell by cell by `iir_composition.TheOfferingTableIsMeasured`.
At `economy` the Chebyshev II costs five times the nonlinear half-band it
follows and the linear form twelve and a half: the price of a guard band at
70 dB, still below the FIR half-band's 21 MACs for the Chebyshev.

## The residue the maintainer kept

The flush window is the recursive stage's rule: in floating point, the
count from the format's largest state down to its smallest normal through the
largest pole; in fixed point, the truncated decay from the full headroom,
twice over:

```cpp
{{#include ../../../rational/include/tap/sr/rational/guard_band.h:rational_guard_band_window}}
```

DspTap's IIR-a had already found where that count cannot keep its promise.
Under flush-to-zero, once a biquad's |a2 y| falls below the smallest normal
its product flushes, and the section, for that one sample a first-order
recursion with |a1| > 1, lifts the state again. A Chebyshev II of order 7 or
more settles there, from some states and not others, a few times the smallest
normal above zero: at most 8.7 times in float and 10.4 in double here, over
twenty starts per design. Orders 3 and 5 drain. So does fixed point, always,
because magnitude truncation leaves no limit cycle.

The plan offered three answers: a deadband in the hot loop, designs that
drain, or the honest limit. The maintainer kept the limit, for the guard band
filter and for the near-linear `transparent` stage of the previous chapter
alike. The hot loops stay as they are. The stage's own `flush()` still equals
its zero padding bit for bit, because it runs the same block on the same
zeros; after it, the output is the residue, 1e-37 in float, not silence. A
chain whose later stages follow a cycling one flushes within that residue of
zero padding, because the chain feeds each later stage exact zeros where
padding feeds it the residue. The batteries pin each case where it applies,
and nowhere else.
