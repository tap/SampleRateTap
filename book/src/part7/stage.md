# Two machines, one filter: stage.h

> There are two ways of constructing a software design: One way is to make it so simple that there are obviously no deficiencies, and the other way is to make it so complicated that there are no obvious deficiencies.
>
> — C. A. R. Hoare, Turing Award lecture, 1980

`basic_stage<S, R>` is the engine's whole datapath: the table, the delay
lines, the schedule, `process`, `pull`, `flush`, `reset`, and the latency.
The ASRC's equivalent took three headers and two chapters, because the
ASRC had a fractional position to track. Here the position is an integer
that cycles through L values, so the resampler collapses into a lookup —
and the interesting decisions move into *which* lookup. There are two,
chosen at compile time by the ratio, and the first thing to understand is
why one machine would not do.

## The L-phase machine

For an interpolator or a mixed ratio (L ≥ 2), the stage runs `bridge`'s
polyphase schedule (Part VI). Output n dots phase (n·M) mod L of the table against
the T newest inputs — T = ⌈N/L⌉ taps per phase — and then consumes
⌊(n + 1)M/L⌋ − ⌊nM/L⌋ inputs. Over a superblock of L outputs every phase
is visited exactly once and exactly M inputs are consumed, so the schedule
is a table of L entries that the compiler builds:

```cpp
{{#include ../../../rational/include/tap/sr/rational/stage.h:rational_schedule}}
```

For an interpolator the advances are L − 1 zeros then a 1 — every input
yields L outputs; for 2/3 they are `{1, 2}` with phases `{0, 1}`; for 3/8
the three entries advance by 2, 3, 3. That is scipy's `upfirdn(h, x,
up=L, down=M)`, sample for sample from n = 0 — the alignment contract is
the formula in the file header, y[n] = Σₖ x[⌊nM/L⌋ − k] · h[phase(n) + kL]
with x[< 0] = 0, and the golden test checks it against committed scipy
vectors with no transient skipped.

Because the schedule is a `constexpr std::array` indexed by a position
that wraps at L, `outputs_for` and `frames_needed` are *arithmetic*, not
simulation: `schedule_frames_needed` is one line of integer division from
the current position. The ASRC could only ever estimate how many outputs a
block would yield, because μ moved; `rational` promises the exact count
from any position, and `Stage.AccountingExactFromEveryPosition` holds it
to that from every entry of every superblock.

### Rows are trimmed, and the zeros are not in the table

Each phase row is stored tap-reversed, like the ASRC's, and then
**trimmed to its nonzero span**. This is where the previous chapter's
structural zeros are cashed. Going up, the L phases of the table *are* the
design's L branches, and the branch holding the centre is `{1.0}` with
zeros on either side — so its row trims to one tap, and an interpolator's
centre phase is a copy that costs one multiply by 1. A mixed ratio's last
phase is zero-padded (N is not a multiple of L) and trims to its real taps.
Nothing in the dot product knows any of this: it is handed a pointer, a
length, and a window offset, and `m_first`/`m_count` per row carry the
span. Skipping a product whose coefficient is exactly zero changes no bit
in any format — x·0 adds 0 to the accumulator — so the trimmed dot *is* the
full formula, and `FixedPoint.StructuralZerosNeverEnterTheDot` pins the
MAC counts per stage at the design's nonzero counts.

## The M-branch commutator

A decimator could run the same machine with L = 1: one phase, every input
consumed, one output per M inputs, a dot of all N taps each time. It would
work, and it would multiply every zero. The zeros of an M-th-band filter
sit at c ± kM — in *one* polyphase branch of the input stream — so the
cheap decimator is the textbook commutator: input n goes to sub-line
n mod M, and output k, produced as x[kM] arrives, is

```text
y[k] = Σ_j dot(branch j, sub-line (M − j) mod M)
```

with branch j the taps h[j + sM], s ascending toward older samples. The
branch holding the centre is the centre tap and its zeros, trimmed to one
tap, so a half-band decimator computes 23 of its 43 taps at `economy` and
↓8 169 of 191. The ASRC never had this option because its input had no
fixed phase structure; here the structure is the ratio.

One detail is load-bearing for the fixed-point chapter. The M branches are
summed *in j order through one accumulator* — DspTap's `accumulate_row`,
which is `dot_row` without the finalize — and finalized once:

```cpp
{{#include ../../../rational/include/tap/sr/rational/stage.h:rational_emit}}
```

A Q15 output is therefore rounded exactly once, whether it came from one
row or from M of them. Had each branch been dotted and finalized
separately and the results added, a decimator would carry M rounding
points and the "DC gain exactly 1 in every format" promise would be off by
up to M − 1 LSB. The single rounding point is also what makes the
`finalize_output` hook in the next chapter possible: a divisor can be
folded into the one place the accumulator becomes a sample.

## Table gain: what a row sums to

The design's B branches each sum to 1. Going up, the table is the design's
vector bit for bit — `stage_test.TableIsTheDesignAndRowsSumToUnity` —
and DC gain 1 at every output follows from the branch normalization. Going
down, the rows are rescaled so that each still sums to 1: a decimator's
whole filter at h/M (one row, quantized once so the fixed-point sum lands
on unity exactly), a mixed ratio's L phases each normalized at the
branch-spread level, `bridge`'s argument. The structural zeros stay
exactly zero through every rescaling because zero times anything is zero.

```cpp
{{#include ../../../rational/include/tap/sr/rational/stage.h:rational_gain}}
```

The constants above encode the two places Q15 departs from "rows sum to
unity" — a decimator's branches quantized at unity with the 1/M applied in
the rounding, and a mixed ratio going down holding its rows at a
power-of-two gain G. Both exist because Q1.14 has too few bits to hold a
row scaled down by M, and both are the fixed-point chapter's story; what
this chapter owes you is that `k_table_gain` states the convention as a
number and `finalize_output` is the one function that honours it.

### Building the table

```cpp
{{#include ../../../rational/include/tap/sr/rational/stage.h:rational_build_table}}
```

Three branches of `if constexpr`, one per machine and format family, and
the quantizer is called per row in every one of them — the row-sum
preservation of `quantize.h` is applied to exactly the row the kernel will
dot, so "every row sums to the format's unity" is a property of the
*stored* table, not of the design it came from.

## The honest limit: a mixed ratio going down

There is one shape the two machines serve imperfectly. 2/3, 3/4 and 3/8
run the L-phase machine over a band-M design: the design's zeros stride by
M, the rows stride by L, and L and M are coprime, so every row crosses
every M-th zero and the trimmed span cannot exclude them — 20 of 65 MACs
per superblock for 2/3 at `economy`, 20 of 87 for 3/4, 22 of 191 for 3/8.
Interpolators, decimators and the mixed ratios going *up* (band L, every
zero in the centre phase) multiply none.

A fix was built, measured and declined, and the plan records the numbers
rather than the intention. Feeding input i to sub-line i mod M makes each
row's M residue classes of lag contiguous, so the zeros' class drops out of
the dot and the MAC count falls to the design's nonzero count (2/3 at
`economy`: 32.5 → 22.5 per output), with float, Q15 and Q31 outputs
bit-identical. But a third of the MACs is not a third of the work when the
dot is short: the per-output bookkeeping — M delay lines, a sub-dot per
class — ran the 2/3 workload −22 % on the M33 and −25 % on Hexagon in
float, but **+13 % on the M55** and **+22 / +60 / +24 %** in Q15 on
M33 / M55 / Hexagon, the flagship embedded profile. The dense rows stay;
the matrix's MAC figures count what runs.

## The contract, as numbers

Everything the ASRC's converter promised about real-time behaviour holds
here, and two things it could not promise hold too:

- **Zero-primed and causal.** `outputs_for(n)` and `frames_needed(k)` are
  exact from the current position; `pull()` with a source that delivers
  exactly `frames_needed(k)` frames yields exactly k outputs, and is
  bit-identical to `process()` on the same stream for any chunking
  (`stage_test.ChunkingIsBitIdentical`, `Stage.PullMatchesProcessBitExact`).
  A dry source returns short and the partial state is exact: the pull
  loop consumes inputs one at a time so that a short pop leaves the stage
  mid-superblock in the state `process` would have left it.
- **Latency is an exact rational.** (N − 1)/2 samples at the composite
  rate is (N − 1)/(2M) output frames, returned as a `tap::dsp::exact_ratio`
  — numerator and denominator, reduced — rather than a `double`. For ↑2 at
  `economy` that is 21 output frames; for 2/3, 32/3. The impulse test
  measures the peak and finds it there
  (`Stage.LatencyIsTheExactRationalAndMatchesTheImpulse`); the chain of the
  next chapter adds these up without ever rounding.
- **Flush equals zero padding.** `flush()` feeds `window_frames()` zeros —
  the longest history any row reads — and writes every output that becomes
  ready, `flush_output_frames()` of them, bit for bit what padding the
  input with zeros would have produced. The stage is left mid-stream;
  `reset()` before reuse.
- **`noexcept` and allocation-free after the constructor.** The histories
  are planar per channel (M sub-lines for a decimator, one otherwise),
  appended in place and compacted with one `memmove` every `k_hist_slack`
  = 64 frames; interleaved frames at the API, every channel sharing the
  coefficient row — the channel rule of Part V, unchanged.

## Why this header looks the way it does

| Decision | Alternative rejected | Reason |
|---|---|---|
| Two machines selected by `if constexpr` on the ratio | one general L/M machine | a decimator through the L-phase machine would multiply every zero; the commutator computes the nonzero count |
| Rows trimmed to their nonzero span, zeros not stored | branch on zero coefficients in the loop | the dot takes a pointer and a length; skipping an exact-zero product changes no bit; the centre phase of ↑L is one tap |
| M branches through one accumulator, finalized once | dot and finalize per branch, then add | a single rounding point: DC exactly 1 in Q15 / Q31, and a divisor can be folded into the finalize |
| `constexpr` schedule of L entries | compute phase and advance at runtime | accounting is arithmetic from the position; the loop's shape is a compile-time fact |
| Latency as `exact_ratio` | `double` seconds | the chain sums it exactly; the test pins it as a number, not a tolerance |
| Dense rows for mixed ratios going down | sparse rows by residue class (built, measured) | −22 … −25 % in float on two cores but +13 % on the M55 and +22 … +60 % in Q15; the bookkeeping outweighs a third of a short dot |
| `pull()` consumes one frame at a time | bulk-pop the whole need | a dry source leaves exactly the partial state `process` would; the short return is resumable |

## Verify it yourself

```sh
# Every ratio of the vocabulary against the committed scipy vectors,
# sample for sample from n = 0 (float within 6e-8 of the float64 reference):
ctest --test-dir build -R 'rational\.golden_test' --output-on-failure

# The stage contract: impulse reproduces the table from every phase,
# accounting exact from every position, pull == process, chunking,
# flush == zero padding, latency at the impulse, MACs are the nonzero counts,
# DC exactly 1 in every format:
ctest --test-dir build -R 'rational\.(Stage|stage_test)\.' --output-on-failure

# DspTap's decimate.h as a second golden at ↓2 / ↓3 / ↓6 (a different design;
# agreement within the documented difference, anything beyond is a finding):
ctest --test-dir build -R AgreesWithDspTapDecimators --output-on-failure

# Regenerate the reference vectors (only when the design or the gain rule
# changes — the generator is the provenance):
python3 rational/tools/reference/make_reference_vectors.py

# Break it on purpose: in emit(), finalize each decimator branch separately
# and add the samples. Every float test still passes. DcGainIsExactlyOne
# fails in Q15 by up to M - 1 LSB, which is exactly the claim the single
# accumulator exists to keep.
```
