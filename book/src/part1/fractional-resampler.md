# The fractional resampler

> God made the integers; all else is the work of man.
>
> — Leopold Kronecker

The servo chapter ended with a number: ε̂, the rate-deviation estimate,
delivered once per output block. This chapter spends it.

Somebody has to turn "consume 1.000 000 2 input frames per output frame"
into actual audio, forever, without drift, without glitches at the moments
the books balance, and within a per-sample cycle budget that must hold on
a Xeon and on a DSP with no double-precision FPU. That somebody is
`fractional_resampler`, the streaming engine at the bottom of
`polyphase_filter.h`. It owns three things: the **history** (the last T
input frames of every channel, kept where the filter can reach them), the
**phase** (where between two input samples the next output lands), and the
**slip logic** (what happens when the phase creeps across a whole-sample
boundary).

The near-unity specialization shapes everything here. A general-ratio
resampler schedules different numbers of outputs per input and needs
control flow to match. At ±1000 ppm, the conversion degenerates into a
*creeping fractional delay*: one output per input, plus a fractional
position μ that drifts by parts per million per sample and occasionally —
every few thousand samples — crosses a boundary and forces the window to
slip by one frame. The steady state is metronomic; all the difficulty
concentrates into keeping μ exact over unbounded time and making the
slips invisible. Those two problems are this chapter.

## The job, one output sample at a time

The polyphase bank chapter built the table: L + 1 rows of T coefficients,
row p holding the FIR that interpolates a signal value p/L of the way
between two input samples. `interpolate()` evaluates one output at
fractional position μ ∈ [0, 1):

1. Scale: `pos = μ · L`. The integer part picks the phase row p; the
   fractional part `fr` says how far μ sits between row p and row p+1.
2. Blend: form `c[t] = c0[t] + fr · (c1[t] − c0[t])` across the T taps —
   linear interpolation between adjacent rows, the trick that makes a
   256-row table act like a continuum (the residual falls ~12 dB per
   doubling of L).
3. Dot: multiply the blended row against the oldest-first history window
   of the newest T input samples and accumulate — in double for float
   samples, int64 for fixed point.

μ = 0 lands the output exactly on history sample T/2 − 1; μ → 1
approaches sample T/2. And the μ wrap 1.0 → 0.0 — the whole-sample slip —
is exactly where the bank's extra row L pays off: row L equals row 0
advanced by one input sample, so "μ reaches 1.0 on this window" and
"μ = 0.0 on the window shifted one frame" are *the same filter*,
bit-identically, with no branch. The slip machinery below leans on that
continuity; `Polyphase.MuWrapIsContinuousWithWindowShift` pins it.

That is the whole kernel: blend, then dot. Roughly T multiply-adds of
blending plus T of dot product per output sample, and everything else in
this chapter is about doing it cheaper, more exactly, and for more
channels — without ever changing an output bit unintentionally.

## Sharing the blend: the C1 split

The first optimization campaign result (Part III tells the full story;
`docs/PERFORMANCE.md` is the canonical record) started from an
observation you can make by reading the loop above: in a multichannel
converter, every channel of a frame is evaluated at the *same* μ. Calling
the fused `interpolate()` per channel recomputes an identical T-tap
coefficient blend N times per frame — for stereo, half the inner-loop
work is duplicate.

The fix is to split the kernel at its natural seam: blend once per frame
into a scratch row, then run a plain dot product per channel.

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_dot_row}}
```

Two things about this function beyond its arithmetic. First, the comment
at the top is a *bit-exactness contract*: given the same μ, blend-then-mac
per tap in the same order is literally the same sequence of floating-point
(or integer) operations as the fused form, so the split changes no output
bit — and the C1 entry in `docs/PERFORMANCE.md` records "outputs unchanged
bit-for-bit" as a checked result, not a hope. This library treats
bit-exactness as the boundary between an optimization (free to ship) and
an algorithm change (needs its own quality evidence); you will see the
same distinction drawn twice more in this chapter. Second, the
`SRT_RESTRICT` qualifiers are C2's contribution: without them the
compiler versioned these loops behind runtime aliasing checks (verified
with `-fopt-info-vec`, not assumed).

The measured C1 result: **stereo pipeline −36% wall-clock on x86,
8-channel −52%**, and −15/−30/−21% instructions (float/Q15/Q31) on the
Cortex-M55 — with the mono kernels count-identical as the control, since
mono keeps the fused path. One target barely moved, though: Hexagon
improved only −3.6/−3.3/−0.2%. Profiling explained why, and the
explanation became the next hypothesis: Hexagon's pipelines were not
dominated by blends or dots at all, but by **per-sample soft-double phase
math**. Which brings us to the centerpiece.

## The phase accumulator: Q0.64

Here is the failure that motivates the design. The obvious phase state is
a `double mu`, updated per output sample as `mu += 1 + eps` with the
integer part peeled off into window advances. On a Xeon that costs a few
cheap FPU ops. On Hexagon — a 32-bit audio DSP with **no double-precision
FPU** — every one of those operations is a soft-float library call, per
sample, on the hottest path in the library. C1's flat Hexagon numbers
were this cost dominating everything else. (Honest correction from the
record, because the project's documentation initially got it wrong: the
Cortex-M55 was *assumed* to share this problem, but its scalar FPU does
support FP64 — only its MVE vector unit is fp16/fp32 — so M55 float was
never soft-double-bound. The measurement that exposed the doc error is
Part III material; the resampler design below is motivated by Hexagon and
its HiFi-class cousins, where the problem is real.)

The C3 redesign eliminates the per-sample double entirely by changing
what the phase *is*:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_class_doc}}
```

The fractional position lives in `phase_`, an unsigned 64-bit integer
read as a pure binary fraction — **Q0.64**: the value μ = `phase_` / 2⁶⁴,
so the representable range is exactly [0, 1) and the resolution is 2⁻⁶⁴
of a sample. The key move is what it accumulates: **only ε**, the
deviation. The "1" in "advance 1 + ε input frames per output frame" is
handled by the integer machinery — consume one input frame per output
frame — and never touches the fraction. Near-unity specialization again:
because the nominal ratio is exactly 1, the fraction only has to carry
the few-hundred-ppm creep, and 64 bits of headroom below the binary point
carry it essentially forever.

Per `process()` call — once per block, not per sample — the servo's
double ε̂ is converted to fixed point:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_slip}}
```

Walk the slip logic carefully; it is the subtlest six lines in the
datapath, and the trick is that **wraparound of the unsigned add is the
slip detector**, for both signs of ε, with no comparisons against 1.0 or
0.0 anywhere:

- **ε ≥ 0** (input clock fast; the window must occasionally hurry). The
  fraction creeps upward by `eps_u` each sample. When the true position
  would cross 1.0, the 64-bit add wraps: `m = phase_ + eps_u` comes out
  *smaller* than `phase_`, which is otherwise impossible for a positive
  increment. That wrap **is** the forward slip: consume one *extra* input
  frame (`advance = 2` — the regular frame plus the slipped one), and the
  wrapped `m` is already the correct new fraction, because mod-2⁶⁴
  arithmetic subtracted exactly the 1.0 that the extra frame consumed.
- **ε < 0** (input clock slow; the window must occasionally wait).
  `eps_u` is the two's-complement reinterpretation of a negative `eps_fix`
  — a huge unsigned number — so the same add normally wraps every
  sample, and *not* wrapping is the anomaly: `m > phase_` means the
  fraction dipped below 0.0. That is the backward slip: consume **no**
  input frame this output (`advance = 0`, reuse the current window), and
  again the modular result is already the correct fraction just below
  1.0.
- Otherwise `advance = 1`: the metronomic case.

![The Q0.64 phase accumulator slipping by wraparound, for both signs of
epsilon](../img/q064-slip.svg)

*The slip logic run with the real mod-2⁶⁴ arithmetic, ε exaggerated to
0.09 so the wraps are visible (at the real |ε| ≈ 2×10⁻⁴ a slip fires once
every few thousand frames). Left: the fraction creeps upward until the add
wraps past 1.0 — consume one extra frame. Right: with ε negative the add
wraps on *every* ordinary frame, and the anomaly is the one that doesn't —
reuse the window. From `scripts/book_figures.py`.*

At +500 ppm a forward slip fires every 2 000 output samples, and thanks
to the bank's extra row the filter evaluated after `advance = 2` at small
μ is the exact continuation of the filter before it at μ ≈ 1.
`AsrcLock.WholeSampleSlipsAreGlitchFree` runs 500 ppm for seconds and
bounds the output's *second difference* by the analytic bound A·ω² of a
clean sine — a discontinuity detector that would trip on any window
mis-step at any slip.

Note also what happens between the `append_one` calls and `phase_ = m`:
if the source runs dry midway through an `advance = 2` slip, the function
returns with the history advanced by one frame but the phase *not*
updated. History and phase are now one frame apart — a state the class
cannot repair locally. That is not a bug; it is a documented precondition
(the contract section below), and the converter's dropout path always
resets and re-primes before processing again.

Downstream, the phase bits feed the kernel directly:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_blend_row_phase}}
```

The top log₂ L bits *are* the phase-row index; the bits below, shifted
up, *are* the intra-phase blend fraction. No multiply by L, no floor, no
subtract — the Q0.64 representation makes the split between "which row"
and "how far between rows" a matter of bit fields. One conversion to the
datapath's blend-factor type per output frame (`blend_factor_from_q64`:
single-precision for float, integer for Q15/Q31) is all that remains of
the floating-point phase math. The fused mono form is the same bit
surgery around the same blend-and-mac loop:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_interpolate_phase}}
```

**Is 2⁻⁶⁴ enough?** Part 0 derived the timing-jitter budget for 120 dB
transparency at 20 kHz: about 8 picoseconds. One sample at 48 kHz is
20.8 µs; 2⁻⁶⁴ of that is ~10⁻²⁴ seconds — twelve orders of magnitude
inside the budget. The double-μ design's 2⁻⁵² was also far inside it, so
resolution was never the emergency; the deeper numerical win is
*exactness over time*. An integer accumulator adds ε with **zero
rounding error per step**, forever — the only quantization is the
once-per-block conversion of ε̂, a rate error below 10⁻¹⁹ that the servo
absorbs like any other infinitesimal drift. A double μ, by contrast,
rounds on every `+=` and carries the fraction with absolute precision
limited by its integer part's magnitude. Measured, from the C3 entry:
quality *improved* to **135.0 dB at 997 Hz** when the integer phase
landed. An optimization PR whose quality guardrail moved the right
direction — the A/B discipline (benchmarks for speed, pinned SNR
thresholds for correctness) catching a pleasant surprise instead of a
regression.

And the cost side, from the same entry: Hexagon pipelines **−10.3% (Q15)
and −15.5% (Q31)**, with float −2.6% — the soft-double phase math C1
identified was simply gone, and the Hexagon *kernels* stayed
count-identical as the control. M55: Q15 −5.3%, Q31 −4.6%, float +1.4% —
a genuine, accepted regression on one scenario, because the M55's scalar
FP64 hardware made doubles cheap and the integer phase traded them for
int64 ops; the cross-target win justified it, and the ratchet baseline
records the trade explicitly. x86 same-minute A/B: float −5.4%, Q15
−12.0%.

## Dispatching the datapath

With phase in hand, each output frame takes one of three routes:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_dispatch}}
```

Mono takes the fused `interpolate_phase` — no scratch-row traffic for a
single channel (with one exception: Q15 on SMLALD-capable Cortex-M cores
routes mono through blend + dot too, because the dual-MAC loop lives in
`dot_row`; the two paths are bit-exact by construction, which is what
makes that rerouting a non-event). Low channel counts blend once into
`row_` and dot per channel over planar histories — the C1 shape. High
channel counts on hosts take the frame-major branch, which is the next
section but one. Note the branch condition `k_channel_parallel &&
frameMajor_`: the first operand is `constexpr`, so on embedded targets
the entire branch constant-folds away. That is not tidiness — a runtime
flag in this loop measured **+6–8%** on the M55 instruction ratchet
before the compile-time gate restored every embedded scenario to exactly
0.00%. The ratchet is why the lesson is a number and not an anecdote.

## Feeding the window: history management

The filter needs the newest T frames of every channel, contiguous,
oldest-first, per channel. Input arrives interleaved, in whatever chunks
the FIFO happens to hold. Between those two facts sits `append_one`:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_append}}
```

Three mechanisms, each with an RT-safety argument:

**Chunked staging.** Frames are pulled from the caller-supplied `pop_fn`
in bulk (the converter passes 16-frame chunks) into the interleaved
`scratch_` buffer, then peeled off one frame at a time as the window
advances. Bulk pops amortize the ring's index synchronization across
many frames — the cached-index design from two chapters ago does its
best work when you ask it for blocks — while the resampler still
consumes with single-frame granularity, because slips need exactly-one
extra frame on demand. Frames staged in scratch have left the ring but
not yet entered the filter, which is why `buffered_frames()` exists: the
servo's occupancy observable must count them or the estimate would carry
a chunk-sized bias.

**Bounded compaction.** Histories are not ring buffers; they are flat
arrays with a moving end index, sized `taps + chunk_frames`. When the end
hits capacity, `memmove` slides the newest T − 1 frames back to the
front and synthesis continues. Why copy at all, when a circular buffer
would avoid it? Because the *filter* needs a contiguous window every
sample: a ring would either split the dot product at the wrap seam
(a branch and a second loop in the hottest code in the library) or copy
into a linear scratch every frame — a memmove per *sample* instead of
one per *chunk*. The flat layout pays T − 1 frames of copy once per
`chunk_frames` appends: bounded, branch-predictable, allocation-free —
worst-case cost is fixed at construction time, which is the entire
definition of RT-safe this library uses. `process()` is `noexcept`, no
locks, no allocation; every buffer was sized in the constructor, which
is allowed to throw precisely because it runs at setup time.

**Two storage shapes.** The member block records the fork:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_members}}
```

Planar — one delay line per channel — below the channel-parallel
threshold: each channel's dot product walks its own contiguous line, and
the deinterleave happens once per frame at append time (a scalar loop
over channels). Frame-major — a single interleaved line — at or above
it: appends become one contiguous `memcpy` per frame and the compaction
one `memmove` per line-fill, but the real reason for the layout is the
kernel it enables.

## The channel axis: C6, briefly

For high channel counts the per-frame cost is dominated by N dot
products, and the float dot product has a vectorization problem you can
now state precisely: its accumulation order is contractual (strict
per-channel double accumulation — reassociating it changes output bits),
so the *tap axis* may not be vectorized without breaking bit-exactness.
The C2 audit verified GCC obeys: float `dot_row` compiles scalar, by
design.

But nobody said anything about the *channel* axis. Channels are
independent accumulators; computing eight of them in lockstep, one tap
at a time, keeps every channel's tap order identical to `dot_row`'s while
filling SIMD lanes with channels instead of taps. That requires the
history to deliver all channels of tap t contiguously — the frame-major
layout — and a register-blocked kernel:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_dot_rows_frame_major}}
```

The measured C6 results, condensed (the full campaign, including the
callgrind profile that justified targeting the dots and the negative
results that bounded the design, is Part III's last chapter): **float
8/12/16-channel pipelines −38/−38/−42% wall-clock with AVX2+FMA**, only
−4–5% on baseline SSE2 — the gain scales with SIMD width, as it must if
the mechanism is what we claim. Bit-exact versus planar, hash-verified
over 30 000 blocks × 4 configs. The gate is deliberately narrow, each
edge measured rather than assumed:

- **Float-only**: fixed-point channel-parallel measured ~1.5× *slower*
  than planar — integer accumulation is exactly reassociable, so the
  planar Q15/Q31 dots already auto-vectorize over taps, and the tap axis
  beats the channel axis when both are available.
- **Channels ≥ 4** (`SRT_CP_MIN_CHANNELS`, overridable for A/B runs):
  below that, lane utilization loses to the planar path's simplicity.
- **Hosts only**: the embedded targets keep their proven codegen (Helium
  on M55, SMLALD on M33-class, Hexagon's measured scalar floor); the
  compile-time macro gate keeps their binaries byte-for-byte ignorant of
  the mode.

And one lesson worth carrying out of context: the first channel-parallel
attempt — accumulators in a plain array the compiler kept in memory —
measured **2.8× slower than planar**. Register-block or don't bother;
`dot_tile_frame_major`'s `constexpr`-size tiles of 8/4/2/1 are that lesson
in code form.

## The contract: prime, process, and the one-frame lie

`fractional_resampler` is deliberately not foolproof; it is *fast*, and
its safety is a documented protocol that the converter — its only
in-tree caller — upholds. The documentation is the code's own:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_process_doc}}
```

**Prime before process.** `prime()` fills the window with T real frames
(or reports dry and stays unprimed). Call `process()` unprimed and
`window()`'s pointer arithmetic `end_ − taps()` underflows a `size_t` —
the converter guarantees priming by construction, since it only leaves
its Filling state once the backlog exceeds setpoint + taps.

**Reset after any dry return.** You now know exactly why from the slip
walk-through: a `process()` that runs dry on the *second* append of an
`advance = 2` forward slip has already advanced the history when it
returns, but never executed `phase_ = m`. History says one frame passed;
phase says none did. Every output synthesized after resuming would be
computed one frame late relative to its nominal position — not a crash,
a *silent sub-window skew*. The class cannot un-append (the frame is
deinterleaved into the histories) and does not try to special-case it;
it defines the recovery protocol instead: `reset()` clears phase,
history, and staged scratch (stale across a discontinuity anyway), then
re-prime. The converter's underrun path does exactly this, with the
servo keeping its ppm estimate and a fade-in masking the splice.

Finally, the small read-side API that closes the control loop:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:rs_mu}}
```

`mu()` converts the phase to double **once per pull, not per sample** —
the block-rate boundary where doubles are cheap even on Hexagon, the
same boundary the ε̂ conversion crosses in the other direction. The
servo adds it to the frame count so the observable `occ + mu` moves
*continuously* through slips: at the instant a forward slip fires, the
count drops by one exactly as μ wraps from ~1 to ~0, and the sum crosses
smoothly. Without μ in the observable, every slip would inject a
one-frame staircase into the servo's error at the beat frequency —
manufacturing the very sawtooth the previous chapter spent three filter
poles suppressing. `buffered_frames()` completes the accounting for the
staged scratch. Two accessors, and the sensor the whole control system
reads is honest to sub-sample resolution.

## Why this file looks the way it does

| Decision | Alternative rejected | Reason |
|---|---|---|
| Q0.64 integer phase, ε-only | `double mu += 1 + eps` per sample | soft-double per sample dominated Hexagon pipelines (C1 finding); integer add is exact forever; measured −10/−15% Hexagon, quality up to 135.0 dB |
| Slips by unsigned wraparound | compare/floor against 1.0 and 0.0 | the mod-2⁶⁴ result *is* the corrected fraction; both slip directions fall out of one add |
| Blend once per frame + per-channel dot | fused interpolate per channel | N×(blend+dot) → blend + N×dot; bit-exact by identical per-tap order; stereo −36% wall-clock (C1) |
| Flat history + bounded memmove compaction | circular history | the dot needs a contiguous window every sample; one bounded copy per chunk beats a seam branch per sample |
| Chunked pop_fn staging | pop one frame at a time | amortizes ring synchronization; staged frames stay visible to the servo via `buffered_frames()` |
| Frame-major + channel-parallel dots (float, ≥4ch, hosts) | vectorize the float tap axis | tap-axis SIMD changes accumulation order = output bits; the channel axis is free and bit-exact (−38…−42% at 8–16ch) |
| Compile-time mode gate | runtime `if (frameMajor_)` alone | a hot-loop runtime flag cost +6–8% M55 instructions; `constexpr` restored embedded codegen to 0.00% |
| Documented preconditions + `reset()` | internal auto-repair of dry slips | the failure needs a reprime anyway (stale window); a repair path would be untestable dead weight on the hot path |

## Verify it yourself

```sh
# Quality with the Q0.64 phase in the loop — the pinned thresholds
# include the 135 dB figure C3 improved:
ctest --test-dir build -R 'AsrcQuality\.' --output-on-failure

# Slip continuity: the second-difference bound at +500 ppm (a slip
# every 2000 samples), plus lock/drift behavior:
ctest --test-dir build -R 'AsrcLock\.' --output-on-failure

# The mu-wrap/extra-row continuity the slips depend on:
ctest --test-dir build -R 'Polyphase\.' --output-on-failure

# Channel independence at 12/16 channels — on a host float build this
# exercises the frame-major channel-parallel path:
ctest --test-dir build -R 'MultiChannel' --output-on-failure

# A/B the channel axis yourself: benchmark, then rebuild with the
# threshold pushed out of reach and benchmark again (use -march=native
# to see the AVX2 headline; SSE2 shows a few percent):
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DSRT_BUILD_BENCHMARKS=ON \
      -DCMAKE_CXX_FLAGS="-march=native"
cmake --build build-bench -j && \
  ./build-bench/bench/srt_bench --benchmark_filter='Pipeline_Float.*(8|12|16)ch'
cmake -B build-planar -DCMAKE_BUILD_TYPE=Release -DSRT_BUILD_BENCHMARKS=ON \
      -DCMAKE_CXX_FLAGS="-march=native -DSRT_CP_MIN_CHANNELS=999"
cmake --build build-planar -j && \
  ./build-planar/bench/srt_bench --benchmark_filter='Pipeline_Float.*(8|12|16)ch'

# Break it on purpose: change `advance = 2` to `advance = 1` in the
# forward-wrap branch of process(), rebuild, and watch
# AsrcLock.WholeSampleSlipsAreGlitchFree fail its second-difference
# bound — every slip becomes an audible one-frame stutter.
```

The last experiment is worth actually running once. The slip logic is
six quiet lines that look like integer bookkeeping; breaking them turns
a 135 dB converter into a machine that clicks every forty-two
milliseconds. That gap — between how little the code looks like it is
doing and how much the measurements say it is — is the fractional
resampler in one sentence.
