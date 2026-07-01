# Two crystals, one stream

Every specification of this library begins with a lie that the audio
industry tells itself daily: "48 kHz."

There is no such thing as 48 kHz. There is a quartz crystal on the capture
device's board resonating at very nearly the frequency its datasheet
promises, and a different quartz crystal on the playback device's board
doing the same, and neither of them consulted the other. Each was cut,
trimmed, and aged in its own factory; each sits at its own temperature,
warming with the electronics around it; each is divided down to a sample
clock through its own board's logic. When both devices claim 48 kHz, what
they mean is 48 kHz plus or minus some parts per million — and *whose*
parts per million is exactly the question. This library's working
envelope, inherited from the kind of hardware it targets, is a few hundred
ppm of offset per device, drifting slowly as temperatures change; the
default configuration accepts anything within ±1000 ppm, and the test
suite drives it across that range deliberately — including a 0 → 300 ppm
drift ramp at 10 ppm/s that must be tracked without losing lock.

A part per million sounds like nothing. It is worth pausing on why it is
everything.

## The integral that cannot be argued with

Suppose you capture audio from device A and play it on device B, and
suppose the two clocks disagree by +200 ppm — the offset used throughout
this project's measurements as a realistic mid-scale case: the input side
runs at 48 009.6 Hz against the output's 48 000 Hz. The rate mismatch is
0.02 %. Per sample it is invisible. But a rate mismatch does not average
out; it *integrates*. Every second, the capture side produces 9.6 more
frames than the playback side consumes. Every second, forever.

Put a buffer between them — the obvious move, and a correct first move —
and you have only chosen where the failure happens. The surplus
accumulates in the buffer at 9.6 frames per second. A 1,024-frame FIFO
(the converter's own default capacity floor, for scale) started half full
gives you about 53 seconds before it is completely full and something has
to give. Make the buffer deeper and you buy time linearly while paying
latency for every frame of depth; a buffer deep enough to survive an
hour-long session at 200 ppm would hold about three quarters of a second
of audio, all of which you would then be monitoring through. Flip the sign
of the mismatch and the same argument drains the buffer to empty instead.
There is no buffer size that fixes a rate mismatch, because the problem is
not jitter — which a buffer genuinely absorbs — but a nonzero mean. The
README states the consequence as the library's founding fact: whole-sample
slips occur roughly once every `1/ppm` samples, and any system that moves
audio between independent clocks must either resample adaptively or
eventually glitch.

So the plain FIFO must fail. The interesting question — and the one this
project answered by measurement rather than assertion, because that is its
habit — is *how badly*.

## Measuring the do-nothing option

The comparison notebook (`notebooks/asrc_comparison.ipynb`, results
recorded in `docs/COMPARISON.md`) includes, alongside the serious
contenders, a subject called the **naive FIFO**: a buffer that simply
drops the newest samples when full, which is what "we'll deal with it
later" compiles to. It was measured under exactly the same conditions as
everything else — a 997 Hz tone at −1 dBFS crossing a +200 ppm clock
boundary, an AES17-style THD+N analysis with the fundamental removed and
the residual integrated across the 20 Hz–20 kHz band.

The naive FIFO measures **−34.7 dB THD+N** and 94.7 dB of A-weighted
dynamic range. The converter this book describes, on the same signal and
the same clocks, measures −132.1 dB.

What does −34.7 dB sound like? The number means that the error left after
subtracting the test tone sits only 34.7 dB below the tone itself — a
residual of about 1.8 % of the signal. If that residual were smooth
harmonic distortion, 1.8 % would already be far into plain audibility. But
it is worse than that, because of *how* the error is distributed in time.
At +200 ppm the buffer overflows and discards a sample about 9.6 times per
second, and each discard splices the waveform to a point one sample later:
a step discontinuity. A step is the broadest-band event a sampled signal
can contain; its energy smears across the entire spectrum. So the
subjective experience is not a haze of distortion but a steady mechanical
ticking — roughly ten clicks per second at this offset — riding on
otherwise clean audio. It is the sound that anyone who has misconfigured a
USB audio loopback already knows, and once heard it cannot be unheard. The
dynamic-range figure tells the same story from below: quiet passages sit
on a floor of click energy, tens of decibels above where the converter's
floor lies.

That row of the table is the cost of doing nothing, and it calibrates
everything else in this book. Every design decision in the chapters ahead
is ultimately justified by the distance between −34.7 dB and −132.1 dB.

## The two industry answers

The two-crystal problem is decades old, and industry converged on two
families of solution. `docs/COMPARISON.md` opens by insisting on the
distinction, because both families are marketed under the same three
letters: there are **full ASRCs** that recover the clock ratio themselves,
and **resampler libraries** that must be handed the ratio from outside.

**The hardware answer** is the asynchronous sample rate converter chip.
The canonical part is Analog Devices' AD1896 — the lineage this library's
architecture explicitly follows — joined by parts like TI's SRC4392. These
are dedicated silicon: serial audio in on one clock, serial audio out on
another, and the chip does everything, including the part that makes the
problem *asynchronous* — discovering the ratio between the two clocks by
itself, continuously, without being told. The datasheet numbers are
excellent: −117 dB THD+N minimum (−133 dB best case) and 142 dB dynamic
range for the AD1896; −140 dB typical and 144 dB dynamic range for the
SRC4392. Their ratio ranges are enormous — 1:8 up and 7.75:1 down for the
AD1896, 1:16 to 16:1 for the SRC4392 — because these chips are built to
convert 44.1 kHz material to 48 kHz and every other crossing a studio can
produce, not merely to absorb drift. The costs are the obvious ones: a
proprietary part, a place on the board, one stereo pair per chip, and no
help at all if your audio exists as bytes in memory rather than as a
bitstream between codecs. (A caveat the comparison document is careful
about, and this book inherits: those figures are datasheet values measured
through analog test loops, not this project's measurement. They are
comparable to the software numbers in definition, not in environment.)

**The software answer** is the resampler library: libsamplerate, soxr,
zita-resampler. These are superb pieces of engineering with a structural
gap that `docs/COMPARISON.md` names precisely: they must be handed the
ratio by an external servo, and so they solve *only half of the drift
problem*. A resampler library answers the question "given that the input
runs 200 ppm fast, compute the output samples" — flawlessly, at any ratio
you ask. It does not answer "how fast is the input actually running right
now?", and that is the question the two-crystal problem poses, because
nothing in your system knows the answer. The true ratio is not written
down anywhere; it exists only physically, in the beat between two
oscillators, and it moves as the room warms up. In the comparison
measurements the libraries were fed the exact ratio by an oracle — the
harness knew the true offset because it had synthesized it — and under
those conditions they measure at the format ceilings: −143.5 dB THD+N
through a 24-bit interface for libsamplerate's `sinc_best`, −143.8 dB for
soxr's `VHQ`. Real numbers, and also unobtainable in the field as stated,
because the oracle does not ship. (Near-unity is their easy regime, too:
libsamplerate's published 97 dB worst case belongs to aggressive ratios,
not this one.)

The missing half has a name: clock recovery. Somebody must observe the two
domains, estimate their ratio from evidence, and track it as it drifts — a
control problem, not a signal-processing one. The Linux/JACK ecosystem
shows what bolting that half on looks like: zita-ajbridge wraps a
delay-locked loop around zita-resampler. Operating systems solve it too,
invisibly — CoreAudio, WASAPI shared mode, and PipeWire all run ASRCs
inside their engines — with unpublished quality and typically 5–20 ms of
latency, fine for notification sounds and disqualifying for live
monitoring.

So the field, surveyed honestly: chips that solve the whole problem in
proprietary silicon; libraries that solve the easy half in portable
software at reference quality; system engines that solve the whole problem
opaquely at whatever quality and latency they choose. What did not exist —
and what this library is — is the whole problem solved in open, portable,
embeddable software at measured quality: an AD1896-shaped architecture,
polyphase FIR plus clock servo, that you can compile.

## The specialization that pays for everything

You cannot simply transcribe the AD1896 into C++ and expect it to fit on a
microcontroller; the chips' generality is exactly the expensive part.
SampleRateTap's founding decision is to refuse most of the problem the
chips solve. It handles *only* the near-unity case: two domains at
nominally the same rate, within ±1000 ppm by default. It will never
convert 44.1 kHz to 48 kHz — the README lists this first among its
limitations, and `docs/COMPARISON.md` is blunt that for genuine rate
*conversion* you should put soxr or libsamplerate in the chain.

Here is what the restriction buys. A general-ratio converter must be able
to place output samples anywhere relative to input samples, at any
spectral relationship between the rates — including downward conversions
where the filter must also band-limit, and ratios that change which parts
of its machinery dominate. In the near-unity regime none of that machinery
earns its keep. When the ratio is 1 + ε with ε a few hundred parts per
million, each output sample lands *almost exactly* on an input sample:
just a hair early or late, by a fractional offset that creeps by ε per
sample and wraps once every `1/ε` samples. The README's "How it works"
section states the consequence in one phrase: the conversion degenerates
into a **creeping fractional delay**. The datapath's job collapses to
evaluating one interpolation at a slowly sliding fractional position — a
48-tap dot product per output sample in the default configuration — plus a
servo deciding how fast the position should creep. And because the two
rates are spectrally indistinguishable, anti-imaging and anti-aliasing
collapse into a single fixed filter design, flat to 20 kHz, done once in
the constructor.

The computational tables in `docs/COMPARISON.md` measure what that is
worth. Against libsamplerate — the closest architectural analog, a
streaming time-domain polyphase resampler — at the matched ~120 dB quality
tier, SampleRateTap converts 2.9–3.6× more frames per second (mono/stereo;
2.1× at 8 channels, where both engines amortize), while carrying half the
algorithmic latency: 24 frames (0.50 ms) of filter group delay against 46
frames (0.96 ms). At the ~140 dB tier the gap widens to 6.2× in throughput
and to 40 frames against 143 in latency. That is the near-unity dividend,
and the comparison document names its mechanism exactly: a 48-tap window
with a creeping phase, instead of general-ratio machinery. On targets
without floating-point hardware the dividend compounds — the Q15
fixed-point datapath has no libsamplerate analog at all, and on a
Pico-class Cortex-M33 the cheapest libsamplerate option costs about 9.8×
what SampleRateTap's intended configuration does.

The soxr rows teach a different lesson, and reading them honestly is a
preview of the next chapter. At the ~120 dB tier soxr converts 32.4
million stereo frames per second on the same host to SampleRateTap's 10.5
million — soxr wins raw throughput, decisively, by processing in large
SIMD-friendly internal batches. The latency column is the price: 556 to
607 frames of algorithmic delay, 11.6 to 12.6 ms, rising to 777 frames
(16.2 ms) at its highest quality tier. Those are fine numbers for batch
conversion and impossible ones inside a 1–2 ms live-monitoring budget, and
— as `docs/COMPARISON.md` puts it — there is no setting that buys soxr's
throughput at SampleRateTap's latency. Throughput, latency, and quality
are not independent virtues to be maximized; they are a budget to be
allocated, and different tools have allocated it for different lives.

One more number from the measured table completes the picture, because
this book does not deal in free lunches. Fed by its own servo rather than
an oracle, running causally at 1.5 ms of total design latency,
SampleRateTap measures −132.1 dB THD+N against the oracle-fed libraries'
−143.5 dB. The ~11 dB gap is the measured price of solving the *whole*
problem — discovering the ratio from buffer occupancy in real time instead
of being told it — and the comparison document presents it as exactly
that. Eleven decibels, spent 132 dB below the signal, purchasing the half
of the problem that was actually hard. The rest of this book is an account
of how both numbers — the 132 and the 11 — were achieved, measured, and
defended.

## Watching the invisible

Before the budgets, one more thing Chapter 1 owes you: a way to *see* the
problem, because 200 ppm is below anything your ears will report until the
FIFO finally gives way. The repository's first example,
`examples/drifting_clocks.cpp`, exists for exactly this. It runs two real
threads: a producer pushing a 997 Hz sine at a virtual 48 000.0 Hz, and a
consumer pulling at 48 kHz plus 500 ppm, both paced with absolute
`sleep_until` deadlines so the long-term rates are exact even though every
individual wakeup jitters by operating-system amounts — far rougher timing
than any real audio callback delivers. A status line prints the servo's
state and its rate estimate as it converges toward the −500 ppm
consumption deviation.

Two of the example's own caveats are worth reading before you run it,
because each is a preview of a later chapter. First, since scheduler
jitter here is on the order of milliseconds, the demo configures a 20 ms
FIFO setpoint rather than the library's 1 ms default — your first sighting
of the latency budget bending to its environment, which is the next
chapter's subject. Second, the converter observes the clocks only through
whole 96-frame chunks, so its estimate of the ratio cannot firm up faster
than the chunk-beat period `1/(ppm × chunkRate)` — about four seconds per
beat cycle at 500 ppm — and the instantaneous estimate visibly wobbles at
that beat, which is why the display shows a three-second moving average.
The information available about two clocks is quantized by how coarsely
you watch them exchange data; that observation will return as the entire
justification for the servo's three-stage design.

Run it and watch the state go `Filling`, then `Acquiring`, then `Locked`,
and the ppm readout settle toward −500. Nothing about the audio would have
told you any of this for the first minute — and that is the point. The
drift is always there; the only choice is whether something in the system
is measuring it.

First, though, the budgets. Claims like "a 1–2 ms live-monitoring budget"
and "120 dB transparency" have been used here as if self-evident. They are
not. The next chapter derives each one — including why this library's
quality target works out to a timing tolerance of about eight
*picoseconds*.

## Verify it yourself

```sh
# Two real threads, two clocks 500 ppm apart; watch the servo lock and
# the ppm estimate converge:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/examples/drifting_clocks

# Reproduce the measured table — including the −34.7 dB naive-FIFO row
# and the oracle-fed library ceilings. Needs numpy, matplotlib, and the
# `samplerate` and `soxr` Python packages; the first cell builds the
# C ABI shared library if missing:
jupyter execute notebooks/asrc_comparison.ipynb

# The computational head-to-head on your own host (requires the system
# libsamplerate and soxr development packages, found via pkg-config):
cmake -B build-cmp -DCMAKE_BUILD_TYPE=Release \
      -DSRT_BUILD_BENCHMARKS=ON -DSRT_BUILD_COMPARE_BENCH=ON
cmake --build build-cmp -j
./build-cmp/bench/compare/srt_bench_compare
```

The comparison notebook pins SampleRateTap's own results with assertions,
so a regression in the library makes the reproduction fail loudly. The
numbers in this chapter are load-bearing, not decoration.
