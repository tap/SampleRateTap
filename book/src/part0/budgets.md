# Budgets: latency, quality, compute

> Perfection is achieved, not when there is nothing more to add, but when there is nothing left to take away.
>
> — Antoine de Saint-Exupéry, *Wind, Sand and Stars*

The previous chapter ended with three words used as if they were
self-explanatory: latency, quality, compute. This chapter turns each into
a number with a derivation behind it, because everything in Part I is an
expenditure against one of these three accounts, and you cannot audit an
expenditure without knowing the budget.

The three budgets are not independent. A longer filter buys stopband
attenuation (quality) at the price of group delay (latency) and
multiply-accumulates (compute). A deeper FIFO buys servo stability
(quality, indirectly) at the price of latency. A finer polyphase table
buys interpolation accuracy at the price of memory and cache traffic. The
design that ships is not the best possible point on any single axis; it is
a defensible allocation across all three, and the allocation is different
for a Xeon than for a microcontroller. That is why the library has presets
and sample-type variants rather than one configuration: same architecture,
different budget splits.

We take the three in the order of most surprising to least.

## The quality budget, denominated in picoseconds

The README makes a claim that deserves suspicion on first reading: the
phase accumulator's resolution is "far below the ~8 ps jitter budget for
120 dB transparency at 20 kHz." Eight *picoseconds* — in an audio system,
where a sample lasts twenty-one microseconds, six orders of magnitude
longer. Where does a number like that come from?

It comes from the first real mathematics in this book, and the derivation
is three lines. This library's entire datapath is, as the last chapter
established, a creeping fractional delay: every output sample is the input
signal evaluated at a slightly wrong time, deliberately. So the natural
question is: how wrong is *acceptably* wrong? If we evaluate the signal at
time `t + Δt` instead of `t`, how large may `Δt` be before the error
matters at the quality level we are targeting?

Take the worst case the audio band can offer: a full-scale sine at the top
of the band,

```text
s(t) = A · sin(2π f t),    f = 20 kHz.
```

The error caused by a small timing offset is governed by how fast the
signal can change. Differentiating, the slope is `2π f A · cos(2π f t)`,
whose magnitude peaks — at the zero crossings — at

```text
max |ds/dt| = 2π f A.
```

A timing error `Δt` therefore produces an amplitude error of at most the
slope times the error:

```text
e = 2π f A · Δt.
```

Now impose the quality target. The filter at the heart of this library is
designed with a 120 dB stopband — the "120 dB transparency" figure that
recurs throughout the project — and −120 dB as an amplitude ratio is
`10^(−120/20) = 10⁻⁶`. Demanding that the timing-induced error stay below
that, relative to full scale:

```text
2π f · Δt ≤ 10⁻⁶
Δt ≤ 10⁻⁶ / (2π · 20 000 Hz) = 7.96 × 10⁻¹² s ≈ 8 ps.
```

Eight picoseconds. Not because audio hardware keeps time that precisely —
it does not, remotely — but because *this library's job is to manufacture
sampling instants*. The two crystals define real time; the converter
invents the fractional positions in between, and any noise in those
invented positions is indistinguishable from noise added to the audio, at
the exchange rate the slope sets: one picosecond of timing error at 20 kHz
full scale costs about an eighth of a microvolt-per-volt, and 8 ps costs
−120 dB. Position error *is* amplitude error. That single sentence is the
reason a resampling library must care about time resolution that would be
absurd anywhere else in audio.

Two honest qualifications keep the number from overclaiming. First, this
is a worst-case bound — the full-scale 20 kHz zero crossing — and real
program material spends almost no energy there; at 1 kHz the same
derivation gives a 20× looser budget, which is one reason the measured SNR
table is 135 dB at 997 Hz but 105 dB at 19.5 kHz. Second, the budget
governs *random or signal-uncorrelated* timing error. Slowly varying
timing error is not noise but frequency modulation — pitch wobble — and it
gets its own, much stricter treatment when the servo chapter derives why
the Quiet stage must reject its input sawtooth to roughly −120 dBc
equivalent at 20 kHz. Same currency, different account.

## Spending the budget: sixty-four bits of phase

With the budget in hand, we can now read the library's most important
data-representation decision as the budget allocation it is. Convert 8 ps
into the datapath's native unit, fractions of a sample at 48 kHz:

```text
8 ps / 20.8 µs ≈ 3.8 × 10⁻⁷ samples ≈ 2⁻²¹ samples.
```

So the fractional position µ must be carried to about 21 fractional bits
before timing quantization alone could threaten 120 dB. Here is what the
library actually does, in the inner loop of the fractional resampler —
this is the Q0.64 phase accumulator the README describes, live from
`include/srt/polyphase_filter.h`:

```cpp
{{#include ../../../include/srt/polyphase_filter.h:p0_phase_step}}
```

The fractional position lives in an unsigned 64-bit integer interpreted as
Q0.64: all 64 bits are fraction, so the resolution is 2⁻⁶⁴ of a sample —
forty-three binary orders of magnitude below the 2⁻²¹ the budget demands.
The servo's rate-deviation estimate `epsHat` is converted from double to
this fixed-point form **once per block**, and from there the per-sample
path is pure integer arithmetic: one 64-bit addition per output sample,
with the two slip cases — the fractional position creeping past 1.0 or
below 0.0, the "whole-sample slip roughly every `1/ppm` samples" of
Chapter 1 — detected by unsigned wraparound rather than comparison against
a threshold.

Why carry 43 bits more resolution than the budget requires? Because the
excess is free, and what it buys is not resolution but *exactness*. A
phase accumulator adds a tiny ε thousands of times per second; do that in
floating point and every addition rounds, because a double near 1.0 has
2⁻⁵² of absolute resolution and a double's rounding depends on the current
magnitude of the accumulator. The earlier version of this code did exactly
that, and worked. But integer addition modulo 2⁶⁴ does not round — ever —
so the only quantization in the entire phase path is the once-per-block
conversion of ε itself, and the accumulated position between servo updates
is bit-exact. (The conversion is safe by construction: the servo clamps
|ε| to about 10⁻³, so `ε · 2⁶⁴` fits comfortably in the signed 64-bit
intermediate — the code comment above carries the argument, and the
configuration validator refuses `maxDeviationPpm` settings that could
break it.)

The project's performance log records what this decision measured when it
landed as change C3 of the optimization campaign: the *motivation* was the
compute budget — an integer-only per-sample path with no doubles is what
keeps the inner loop cheap on DSPs without double-precision floating-point
units, and it cut Hexagon's Q31 pipeline cost by 15.5 % — but quality
*improved* as a side effect, to 135.0 dB at 997 Hz, with the log noting
the phase resolution change from 2⁻⁵² to 2⁻⁶⁴. One representation change,
paid from no budget, credited to two. Those are rare, and worth designing
toward.

## The latency budget

Latency is the easiest budget to state and the easiest to spend by
accident. Here is where every frame of it is decided — the converter's
entire configuration surface, live from `include/srt/asrc.h`:

```cpp
{{#include ../../../include/srt/asrc.h:p0_config}}
```

The README's latency equation prices the defaults:

```text
latency = targetLatencyFrames + (L·T − 1) / (2L)        [input frames]
        = 48 + (256·48 − 1)/512
        = 48 + ~24  ≈ 72 frames  ≈ 1.5 ms at 48 kHz.
```

Two terms, and they are budget lines of entirely different character.

The second term is the **filter group delay**, and it is a law of physics
wearing a configuration option's clothes. The interpolation filter is a
linear-phase FIR — symmetric coefficients, which is what guarantees every
frequency is delayed equally, and waveform shape is preserved — and a
symmetric filter *must* delay the signal by half its span: with `L = 256`
polyphase branches of `T = 48` taps each, `(L·T − 1)/(2L)` is 23.998
input frames, ~0.50 ms. You cannot negotiate this term down at constant
quality; you can only buy a shorter filter. `FilterSpec::fast()` does
exactly that, cutting group delay to about 16 frames at reduced stopband,
and the `transparent()` preset spends the other way — 80 taps, 40 frames,
0.83 ms — for its extra high-frequency headroom. Quality and latency,
trading at a posted exchange rate of half a frame per tap.

The first term, the 48-frame **FIFO setpoint**, is not physics but control
headroom, and it is the term you own. The FIFO between the clock domains
must never run empty (an audible underrun) and never hit its high
watermark (a resync), so the servo regulates its occupancy around a
setpoint — and that standing occupancy is buffered audio you are listening
through. Forty-eight frames is one millisecond at 48 kHz: enough to absorb
the push/pull phase jitter of real callbacks with margin, small enough to
keep the total design latency at 1.5 ms.

The setpoint carries a feasibility rule that the README states in bold and
the constructor-plus-`pull()` logic enforces, because violating it does
not degrade the system — it destroys it: **the setpoint must exceed the
pull block size.** A `pull()` synthesizes output only from frames already
buffered; if the callback asks for 128 frames while the servo holds the
buffer at 48, every callback drains the FIFO through empty, and the
converter falls into a permanent dropout cycle that no amount of servo
cleverness can escape, because the geometry is simply infeasible. Rather
than document a footgun, the converter adapts: when it observes pull
blocks larger than the configured setpoint, it raises the effective
setpoint to the block size plus about half a block of margin (bounded by
FIFO capacity — callbacks above ~340 frames also need `fifoFrames` sized
explicitly), reports the raised value in
`Status::effectiveTargetLatencyFrames`, and lets latency follow. The
latency budget, in other words, has a hard floor set by your callback
size, and the library will spend up to that floor without asking — the
one budget line it refuses to let you underfund. On top of the rule sits
its softer sibling: the setpoint must also stay above the peak occupancy
excursion of your push/pull jitter, and the FIFO term breathes by a
fraction of the block size as the servo tracks drift, so 1.5 ms is a
design center, not a guarantee etched per-sample.

`designedLatencySeconds()` reports the resulting figure at runtime, and
`tests/test_latency.cpp` closes the loop the project's way: it pushes an
impulse through a locked converter and asserts that the impulse emerges
where the equation said it would.

## The compute budget

The third budget is the one whose *unit* changes with the deployment. On a
server, compute is a fraction of a core; on a microcontroller, it is a
question of existence — does the workload fit under the clock rate or not.
This library targets both ends simultaneously, which is why its
performance culture is unusual, and why `docs/PERFORMANCE.md` is one of
the two canonical history documents this book draws on.

Start at the comfortable end. On the shared 2.80 GHz Xeon that produced
the README's benchmark table, the default float converter processes a
stereo 48 kHz stream at 107.8 ns per frame — 193× faster than real time,
meaning one live stream costs about half a percent of one core. At that
end the compute budget is not about survival but about citizenship: how
many streams per core, how much headroom the rest of the audio graph
inherits.

Now the other end. The README's platform matrix ends at the Arm
Cortex-M33 — the Raspberry Pi Pico 2's core, bare metal, no FP64 hardware,
no vector unit — and the project publishes, in the README's
instruction-count table, exactly what every workload costs there. The
numbers are *executed instructions*, measured by running fixed workloads
under QEMU with a counting plugin, and they are brutal and instructive.
The float interpolation kernel that costs the Cortex-M55 99.5 million
instructions costs the M33 1.90 **billion** — about 19× — for one reason:
the float datapath accumulates in double precision by design, and on a
core with no double-precision FPU every one of those accumulations becomes
a software floating-point library call. The compute budget on such a
target is not tightened; it is a different budget entirely, and the
Q15/Q31 fixed-point datapaths exist precisely as the correctly-denominated
response — integer-only inner loops that make the M33's cost land near the
M55's instead of 19× above it.

What does an instruction budget *mean* on a 150 MHz M33? Divide. A 150 MHz
core executing (optimistically) one instruction per cycle retires 150
million instructions per second, and a 48 kHz stream demands a frame every
20.8 µs — about 3,100 instructions of total budget per frame, forever,
before the rest of the firmware has run at all. Against that, the measured
comparison workloads put the full Q15 converter — servo and FIFO included
— at roughly 5,043 instructions per stereo frame on the M33: about 242
million instructions per second for stereo, over the core's ceiling even
at ideal IPC. Mono, at roughly half that, fits. This is exactly the
README's guidance, now visible as arithmetic rather than advice: 48 kHz
Q15 mono fits a 150 MHz M33; stereo wants the `fast()` preset or the
RP2350's second core. On a Xeon the same library is a rounding error; on
the M33 the default preset is *infeasible in stereo*, and knowing that
before flashing hardware is the entire point of keeping the budget in a
table.

The honesty clause matters as much as the numbers, and `docs/PERFORMANCE.md`
states it in its metrics table: instruction counts are deterministic to
the instruction, noise-free, and well-correlated with real cost *for
scalar code* — and they are still not cycles. They know nothing of wait
states, flash caches, or dual-issue. Cycle truth requires vendor
simulators or real silicon, which is why the repository carries
`examples/pico2_cyccnt/`, a flashable RP2350 harness that measures
DWT.CYCCNT cycles per block against these same instruction baselines, and
why the README explicitly frames the counts as "budgets pending
real-silicon validation." What determinism *does* buy is enforcement: the
counts are committed to `bench/baselines.json` and CI re-measures every
push, failing on any drift beyond ±3 % in either direction — a regression
is rejected, and an unexplained improvement is also rejected until the
baseline is re-recorded in the same diff, so stale slack cannot accumulate
to hide the next regression. Wall-clock numbers, by contrast, are never a
hard gate: shared runners are too noisy, and a gate that flakes teaches
people to ignore it. Instructions are gated because they are exact;
wall-clock is reported because it is real. Both disciplines are the same
policy — publish only what you can re-measure — applied to metrics of
different reliability. Part II returns to this machinery in detail.

## Each budget line becomes a file

Part 0 has now done its work: a physical problem (two crystals), a
measured cost of ignoring it (−34.7 dB), and three budgets with numbers
attached. Part I walks the library's headers in dependency order, and the
tour is really the budget ledger read line by line:

`kaiser.h` is the quality budget's opening entry — the 120 dB stopband
that made the 8 ps derivation's target, purchased with a windowed-sinc
design whose tap count is the latency and compute budgets' first expense.
The polyphase bank spends memory to make one branch-pair evaluation per
output sample possible at all, and its `L = 256` branch count is sized by
the interpolation-residual rule the README quotes (−12 dB per doubling of
`L`, +12 dB per octave of signal frequency) — the reason the measured
table slopes from 135 dB at 997 Hz to 105 dB at 19.5 kHz.
`sample_traits.h` is the compute budget's answer to the M33 column
above: the Q15/Q31 datapaths as a customization point rather than a fork.
`spsc_ring.h` holds the latency budget physically — its occupancy *is*
the 48-frame line item — and doubles as the servo's sensor. `pi_servo.h`
polices the quality budget's FM account, rejecting the occupancy sawtooth
to the −120 dBc figure this chapter bounded. The fractional resampler
carries the Q0.64 accumulator you have already read. And `asrc.h`
composes the whole, enforcing the feasibility rule so the latency budget
can never be underfunded into a dropout cycle.

Every number in those chapters traces back to one of this chapter's three
accounts. When a design choice seems baroque — a 64-bit integer phase, an
extra row in a coefficient table, a third servo stage — the question to
ask is always the same: *which budget is it spending, and which is it
defending?*

## Verify it yourself

```sh
# The 8 ps budget, re-derived in one line:
python3 -c "import math; print(1e-6 / (2 * math.pi * 20000))"

# The quality budget, enforced: the pinned SNR thresholds behind the
# README's 135/120/112/105 dB table:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build -R AsrcQuality --output-on-failure

# The latency budget, enforced: an impulse must emerge exactly where
# designedLatencySeconds() promises (48 + ~24 frames by default):
ctest --test-dir build -R Latency --output-on-failure

# The host compute budget (Google Benchmark; the README table's source):
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DSRT_BUILD_BENCHMARKS=ON
cmake --build build-bench -j
./build-bench/bench/srt_bench

# The embedded compute budget: fixed workloads under QEMU, compared to
# the committed baselines at ±3% (needs the cross toolchain and a
# TCG-plugin-capable QEMU — docs/PERFORMANCE.md has the mechanics):
python3 scripts/icount.py --target m33 --build-dir <build> --plugin <plugin.so>
```

The instruction-count and benchmark tables in the README regenerate from
these same commands (`scripts/update_icount_docs.py`,
`scripts/update_perf_docs.py`), and CI fails if the published tables drift
from the measured baselines — the budgets in this chapter are audited on
every push.
