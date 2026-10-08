# The phase table and the schedule

> Everything should be made as simple as possible, but no simpler.
>
> — attributed to Albert Einstein

Part I's polyphase chapter was about a table with one extra row nobody
asked for, every row stored backwards, and a linear blend between adjacent
rows for a position μ with 2⁻⁶⁴-sample resolution. This chapter is about
the same table with the blend gone, the extra row gone, and — the one
thing this engine adds — half the rows gone too. The data structure is
simpler than the ASRC's because the problem is: the position is never
between two rows, and it comes back to the same row every L outputs.

## The schedule: period L, known at compile time

Output n of a rational L/M converter dots polyphase branch (n·M) mod L
against the newest taps-per-phase input samples, then consumes
⌊(n + 1)M/L⌋ − ⌊nM/L⌋ inputs. Those two numbers depend only on n mod L,
so the whole schedule is a table of L entries that repeats forever, and
the compiler can build it:

```cpp
{{#include ../../../bridge/include/tap/sr/bridge/schedule.h:rt_schedule}}
```

Going up (L = 160 > M = 147) the advances are 0 or 1: most outputs consume
one input, thirteen per superblock consume none. Going down (L = 147 <
M = 160) they are 1 or 2. Either way they sum to exactly M over the
superblock — L outputs always consume M inputs — and every phase is
visited exactly once, because gcd(M, L) = 1. `Schedule.UpExhaustive` and
`Schedule.DownExhaustive` check all 160 and all 147 entries against the
formula, which is the first appearance of the discipline this engine is
built on: with a period of 147, *exhaustive* costs nothing, so nothing is
sampled.

Two consequences, both of which the ASRC could not have. `frames_needed`
is arithmetic — from position pos, the next k outputs need
⌊(pos + k)M/L⌋ − ⌊pos·M/L⌋ inputs, never a walk of the table — so a
pull-style consumer can ask for exactly N frames and be told exactly how
much input that takes, and `Schedule.FramesNeededIsPositionInvariantOverSuperblocks`
holds that the answer depends on pos only modulo L. And there is no drift,
ever: the ASRC's Q0.64 accumulator departs from an exact rational by 2⁻⁶⁴
per output, which the cross-validation chapter will have to account for;
this table departs by nothing.

## Phase-major, no blend, no extra row

```cpp
{{#include ../../../bridge/include/tap/sr/bridge/phase_table.h:rt_phase_table}}
```

The layout decisions, against Part I's:

- **Phase-major, one contiguous row per output.** Each output is one dot
  of `taps()` coefficients against `taps()` history samples, straight into
  the `tap::dsp` kernels — the SMLALD pair-loads on Cortex-M33, Helium on
  the M55, HVX on Hexagon — with no gather. Rows are tap-reversed for the
  forward kernel, as the ASRC's are.
- **Exactly L rows, no blend.** The ASRC's μ falls between rows and the
  bank interpolates coefficients linearly, paying a second row read and a
  blend per tap and accepting a residual that rises 12 dB per octave. Here
  phase(n) is an integer; the row is read, not interpolated. The entire
  coefficient-interpolation machinery of Part I's `blend_row_phase` has no
  counterpart.
- **No extra wrap row.** Part I's cleverest allocation — row L stored as
  row 0 advanced one sample, so the blend at μ → 1 is branch-free — solved
  a problem this engine does not have. The schedule wraps at L to phase 0
  against a window the advance has already moved. There is nothing to
  blend across the seam, so there is no seam.

What the table keeps from Part I is the single quantization point: the
constructor is where the double prototype becomes `sample_traits<S>::coeff`,
through DspTap's row-sum-preserving quantizer, so that every stored row's
Q1.14 or Q1.30 sum is the format's unity exactly. `phase_table_test` runs
that check over *every* phase of all eight (direction × tier) tables in all
three formats — row sums, the DC guarantee — rather than a sample of them.

## Half the rows: the symmetry lever

The prototype is linear-phase: h[n] = h[LT − 1 − n]. Read per branch,
that says branch p's taps are branch L − 1 − p's taps in reverse order. So
the table stores only the low half of the branches, ⌈L/2⌉ rows — 74 of 147
going down, 80 of 160 going up — and a mirrored phase dots its partner's
stored row *backwards*, through `tap::dsp::dot_row_reversed`, a kernel that
landed in DspTap first because that is where kernels live. Same products,
same accumulation order, bit-identical output to a full table, half the
bytes. L odd (147) has a self-symmetric middle branch, stored normally.

The subtlety is quantization, and it is the reason the halving is exact
rather than approximately exact. Quantizing both halves independently and
then asserting they mirror would fail by an LSB wherever the
largest-remainder correction landed on different taps of the two rows.
Instead quantization is *canonical over the stored half only*: a mirrored
row is its partner's reversal by construction, and since reversal
preserves the multiset of coefficients, every stored row's exact-unity sum
carries over to its mirror unchanged. There is no second quantization to
disagree with the first. (The same question — what does a quantizer do
with a mirrored pair? — comes back in the next part with a sharper edge,
when a mirrored pair lands *inside* one row.)

The storage numbers are pinned (`PhaseTable.StorageBudgetsArePinned`):
`economy` is 74 × 58 × 4 = 16.8 KiB going down in float and 80 × 38 × 2 =
5.9 KiB going up in Q15; `transparent` 53.2 KiB going down in float, which
was 105.7 before the halving. On a Pico-class part with a few hundred
kilobytes of RAM, that is the difference between a tier that fits and one
that does not. The *compute* cost of the halving measured at zero on all
three targets — with one codegen subtlety the next chapter tells, because
the ratchet caught it.

## Why this table looks the way it does

| Decision | Alternative rejected | Reason |
|---|---|---|
| `constexpr` schedule of L `(phase, advance)` entries | compute `(n·M) mod L` per output | no modulo, no division, no drift; the superblock walk of the next chapter needs the cursor in a register |
| Exactly L phases, integer row | the ASRC's L + 1 rows and linear blend | the position is never between rows; the blend's second row read and residual have nothing to buy |
| Phase-major contiguous rows | strided prototype | one dot per output straight into the shared kernels, no gather |
| Store ⌈L/2⌉ rows, dot the mirror reversed | store all L | half the bytes (transparent 105.7 → 53.2 KiB); `dot_row_reversed` is bit-identical to the materialized mirror |
| Quantize the stored half only | quantize both halves and assert symmetry | the mirror is the partner's reversal by construction; one quantization, so unity rows carry over exactly |
| Exhaustive phase tests | sampled phases | the period is 147 or 160; sampling a set that small would be a choice to know less |

## Verify it yourself

```sh
# Every entry of both superblocks, and frames_needed's position invariance:
ctest --test-dir build -R 'bridge\.Schedule\.' --output-on-failure

# Every phase of every table in every format: row sums, DC, the mirror
# identity; and the pinned storage budgets:
ctest --test-dir build -R 'bridge\.(phase_table_test|PhaseTable)\.' --output-on-failure

# Break it on purpose: in stored_index(), return ph for every ph and size
# the table at k_phases rows. Everything still passes — you have un-halved
# the table at twice the bytes. Now instead return k_phases - 1 - ph for
# the low half too: DownEconomyEveryPhase fails on the very first phase,
# because branch 0 is not branch 146 reversed.
```
