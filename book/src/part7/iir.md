# A filter that remembers: iir_stage.h

> A recursive stage's tail never ends on its own.
>
> — `docs/IIR_PLAN.md`, I6

The FIR stage of the last three chapters forgets: one window after its input
goes silent, its history is zeros and so is its output. A recursive stage
does not. Its output feeds back into its state, so the cheapest decimator by
2 the engine has, four multiplies per output at `economy` against the FIR
half-band's twenty-one, carries with it a set of problems the FIR stage never
had: a delay that depends on the frequency, a tail that never ends on its
own, and a state that, left alone in floating point, decays into the
subnormal range and stays there. This chapter is about how the engine keeps
its contracts anyway, and where it states that it cannot.

## A stage type of its own

The family plan once said that IIR tiers would be profiles. They are not
(D20): a recursive stage has a different machine, a different state and
different contracts, and A4's rule, one meaning per profile name, would have
`economy` mean two machines. So `basic_iir_stage<S, Ch, R, P>` sits beside
`basic_stage<S, Ch, R>`, with the phase variant as a type parameter:

```cpp
{{#include ../../../rational/include/tap/sr/rational/iir_stage.h:rational_iir_phase}}
```

Both variants are the two-path allpass half-band of Valenzuela and
Constantinides, H(z) = ½ [A0(z²) + z⁻¹ A1(z²)]. Decimating, the even inputs
go to one path and the odd ones to the other, each path runs at the low
rate, and the output is the half-sum; interpolating, both paths run on every
input and their outputs interleave. The nonlinear variant makes both paths
cascades of one-multiplier allpass sections, the elliptic half-band, power
complementary; the near-linear variant makes one path a pure delay and the
other an allpass that approximates it, so the passband is delayed by a whole
number of samples within a stated deviation.

The profiles are the FIR stages' four, at the same numbers, in both variants.
The nonlinear designs are compile-time constants (DspTap's constexpr Gazsi
formulas), the near-linear ones committed tables, both at the design
divisors 1, 2, 4 and 8 that a cascade by up to 16 uses. A custom nonlinear
profile designs at construction; a custom near-linear one has no design to
run, and `validate()` says so in the constructor's words:

```cpp
{{#include ../../../rational/include/tap/sr/rational/iir_stage.h:rational_iir_validate}}
```

## The machine is the engine's, the kernels are DspTap's

DspTap's kernel computes one commutator step for one channel: given the
even input and the odd one before it, it updates the recursive states and
returns the output. Everything around it is the stage's, as an FIR stage
keeps its history and calls `dot_row`: the odd latch, the near-linear delay
path's ring of even inputs, the stream's phase, the channels, the accounting
and the flush.

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/iir_kernels.h:iir_halfband_kernels}}
```

```cpp
{{#include ../../../rational/include/tap/sr/rational/iir_stage.h:rational_iir_steps}}
```

The per-block entry points are out of line and run against a cursor, as the
FIR stage's do (A12), and the count-independent part is a base compiled once
per format, ratio and variant (K12). One thing is new: an interpolator
computes both its outputs for an input at once, so a `pull()` that asks for
an odd number of frames banks the second one for the next call, the L − 1
outputs `tap::dsp::chain`'s scratch bound already allows a stage's own pull.

## A delay that depends on the frequency

An FIR stage's latency is an exact fraction of output frames, because a
linear-phase filter delays every frequency by the same amount. A recursive
stage has no such number, so it never provides `latency_output_frames()`.
It reports three numbers instead, which DspTap's chain composes over its
stages:

```cpp
{{#include ../../../rational/include/tap/sr/rational/iir_stage.h:rational_iir_latency}}
```

At `economy` the nonlinear decimator delays 1.20 output frames at DC and
2.48 at the passband edge; the near-linear one delays exactly 7 within
0.0088. That exact delay path is what lets the near-linear cascades into the
low-band round trip of the previous chapter: `tap::dsp::subband_delta` takes
T from it, and removing a tone of the usable band leaves it 64.5 to 70.0 dB
down at the 70 dB profiles and 114.8 dB down at `transparent`, past the
plan's −60 and −100 dB. The nonlinear variant has no delay path, and the
scaffold refuses it at compile time.

## A tail that never ends, and the host's flush-to-zero

`flush()` must equal zero padding bit for bit, with silence after it. A
recursive state never reaches zero on its own in exact arithmetic; in
floating point it decays to the smallest normal value, and then, if
subnormal results are flushed to zero, to an exact zero. So the window is
counted from the largest finite state down to the smallest normal, through
the stage's largest pole, plus a frame per section; in fixed point it is the
truncated decay from the full state headroom:

```cpp
{{#include ../../../rational/include/tap/sr/rational/iir_stage.h:rational_iir_window}}
```

That count leans on the host. Flushing subnormals belongs to the thread, and
so to the host that owns it: most audio hosts already set the bit on their
audio threads, and a library that set it would have to restore it on every
call. The stages never write the floating-point environment. They read it,
in a Debug build, as a precondition:

```cpp
{{#include ../../../submodules/dsptap/include/tap/dsp/subnormals.h:subnormals_flushed}}
```

What each target's host sets is a table in `rational`'s README: MXCSR's FTZ
and DAZ on x86-64, FPCR.FZ on AArch64, FPSCR.FZ on a Cortex-M with an FPU,
nothing where no bit exists (software float, Hexagon). Without it the state
sticks at a few multiples of the smallest subnormal, at most seven measured,
and the output after a flush is that residue, not silence: an honest limit,
pinned in an executable of its own, because the macro that compiles the
precondition out must be one setting per image.

One stage keeps its residue even with the bit set. The floating near-linear
stage at `transparent` and divisor 1, nine second-order sections deep, settles
into a zero-input limit cycle at about four times the smallest normal: a
section's product falls below the smallest normal and flushes, and the
section, run without it for one sample, lifts the state again. It is the
mechanism DspTap's IIR-a found in the guard band filter's Chebyshev II. The
maintainer kept it as an honest limit, with the residue pinned, for this stage
and for that filter alike (the next chapter).

## The stage that lost

The plan expected a four-path stage by 4 to replace the two half-bands at
the top of a cascade by 8, where the spike's own kernels had counted 13 to
32 % fewer instructions. Measured on the shipping machine over DspTap's
generic kernels, it was 10.7 % dearer on the M33 and 9.2 % on the M55 in
float going down, and 14.6 % on the M33 in Q15; it cleared the 10 % bar on
Hexagon going down and in a few Q15 cases, never in one format and direction
on all three targets. So it does not ship, every cascade is half-bands, and
the record of why is in
`rational/tools/spikes/iir/iirb_fourpath/`:

```cpp
{{#include ../../../rational/include/tap/sr/rational/iir_chain.h:rational_iir_by}}
```

Each stage of a cascade is designed at its own divisor, as an FIR chain's
are, so a stage further up is shorter: by 16 at `economy` the nonlinear
cascade costs 24 multiplies per output against 60 for four copies of the
profile's own design, and against 143 MACs for the FIR chain by 16.
