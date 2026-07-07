# Appendix B: Glossary

> The limits of my language mean the limits of my world.
>
> — Ludwig Wittgenstein, *Tractatus Logico-Philosophicus*

Terms of art as this book uses them. Where the general meaning and this
project's usage differ, the entry gives the project's.

**Acquire/release** — the pair of C++ memory orderings that establishes
*happens-before* across threads: everything written before a
release-store is visible after the acquire-load that observes it. The
only synchronization in the library's ring buffer, used once per
direction; the same pair carries the converter across the RP2350's two
cores in the dual-core firmware.

**AES17** — the Audio Engineering Society's standard for measuring
digital audio equipment, defining how THD+N and dynamic range are taken
(notch the fundamental, integrate the residual over the audio band,
A-weight for DR). The comparison notebook implements an AES17-style
measurement so the library's numbers are commensurable with hardware
datasheets.

**Anti-image filter** — the lowpass that removes the spectral copies
(images) created by interpolating between sample instants. In this
library it is the Kaiser-windowed sinc prototype: pass the audio band
flat, suppress everything from the first image down by the stopband
attenuation.

**ASRC (asynchronous sample rate converter)** — a converter between two
sample streams whose clocks are *independent*: the ratio is not known in
advance, is never exactly rational, and drifts, so it must be recovered
continuously by a servo. Distinct from a resampler library, which must be
handed the ratio from outside.

**Beat frequency** — the rate at which a slow periodic alignment
recurs; here, the rate at which whole-sample slips (and hence occupancy
sawteeth) arrive: `ppm × fs` for sample-granular transfer, divided by
the block size for block transfer.

**Blend factor** — the fractional weight μ used to linearly interpolate
between the two polyphase coefficient rows adjacent to the current
fractional position. Computed once per output frame and shared across
all channels, which is why N channels cost `blend + N × dot`.

**Block-beat sawtooth** — the deterministic waveform that block-quantized
transfer imprints on the FIFO occupancy observable: one push/pull block
peak-to-peak, at the beat frequency. It is measurement quantization, not
clock movement; the servo's stage gating and the unlock threshold both
exist to keep it out of the rate estimate.

**Cache line** — the unit (64 bytes on the targets here) in which cores
move memory between their caches. Data structures shared between two
real-time threads are laid out in whole cache lines per owner.

**Cache-line ping-pong** — the performance failure where a line written
by one core and read by another migrates back and forth on every access,
costing hundreds of cycles each trip. The ring buffer's cached-index
design exists so the steady-state fast path touches no foreign line at
all.

**Cent** — one hundredth of a semitone, about 0.06% in frequency; the
unit in which the block-size study reports the low-rate FM that coarse
blocks impose (~0.9 cents rms at 32-frame blocks).

**dBc** — decibels relative to the carrier: the level of a sideband or
spur measured against the signal that carries it, used for the servo's
sawtooth-rejection figures.

**dBFS** — decibels relative to digital full scale; −1 dBFS is the AES17
measurement level, 0.5 FS (−6 dBFS) the quality suite's.

**DWT / CYCCNT** — the Data Watchpoint and Trace unit of Arm M-profile
cores and its free-running 32-bit cycle counter. Optional silicon (hence
the `NOCYCCNT` runtime check), per-core on the RP2350, and the
instrument that converts QEMU instruction baselines into real cycle
budgets.

**False sharing** — two logically unrelated variables sharing one cache
line, so writes to either invalidate both readers. Prevented in the ring
by giving producer state, consumer state, and shared read-only state a
64-byte-aligned line each.

**FIFO** — first-in-first-out buffer. In this library the SPSC ring
between the clock domains; its occupancy doubles as the servo's phase
detector, which is why it exposes exact occupancy rather than an
approximation.

**Fractional delay** — a delay of a non-integer number of samples,
realized by interpolating between stored samples. The near-unity ASRC's
datapath is a fractional delay that *creeps*: the fractional position
advances by the small rate deviation every frame.

**Frame** — one sample per channel at one time instant; interleaved
buffers store frame after frame. Latency and occupancy are denominated
in frames so they are channel-count-invariant.

**Group delay** — the delay a filter imposes on signal envelopes; for
the linear-phase FIR here it is a constant (T−1)/2 taps ≈ 24 input
samples for the default filter, the fixed half of the converter's
latency budget.

**Header-only** — a library shipped entirely as headers, compiled into
each consuming translation unit. It buys trivial integration and full
inlining, and costs ABI fragility discipline (see the rejected
`hardware_destructive_interference_size`).

**Interleaved** — channel-multiplexed sample layout
(`L R L R …`), the wire format of `push()`/`pull()`.

**Kaiser window** — the near-optimal FIR design window with one shape
parameter β trading main-lobe width against sidelobe level, plus
published closed-form fits from stopband attenuation to β and to filter
length. Chosen because the design math is a page of code with known
error bounds, evaluated once at construction.

**Latency breathing** — the slow wander of the FIFO term of end-to-end
latency (a fraction of the block size) as the servo phase-tracks the
block beat in Track stage; benign, and distinct from an actual setpoint
change.

**Lock-free** — progress guarantee: every operation completes in a
bounded number of steps regardless of what other threads do, including
being suspended at the worst instruction. Required of everything on the
audio path; asserted at compile time for every atomic the hot path
touches.

**Memory model / `std::memory_order`** — the C++ rules defining which
values a load may observe across threads, controlled per-operation by
ordering annotations. This codebase's idiom is *sufficiency as
documentation*: each annotation is exactly as strong as the proof needs,
so each one tells the reader why it exists.

**MVE / Helium** — Arm's M-profile Vector Extension (Cortex-M55 class):
128-bit SIMD including fp32, but no double precision. Its presence or
absence gates which Q15 kernel the library compiles.

**NCO (numerically controlled oscillator)** — an accumulator whose
increment sets its frequency. The converter's μ phase accumulator is the
NCO of its PLL: the servo's ε̂ sets the increment, wraps mark whole-sample
slips.

**Near-unity** — the regime this library specializes in: conversion
ratios within a few hundred ppm of 1.0 (two "48 kHz" clocks), where the
general resampling problem degenerates into a creeping fractional delay.
The specialization is what buys the 48-tap datapath and sub-millisecond
filter delay.

**Occupancy** — the number of frames currently buffered between the
domains (ring plus staged frames). The servo's only sensor; its
quantization is the fundamental measurement limit of the design.

**Phase accumulator** — the unsigned Q0.64 integer holding the
fractional resampling position. It accumulates only the rate *deviation*
per output sample, in integer arithmetic (resolution 2⁻⁶⁴ samples), and
detects whole-sample slips by 64-bit wraparound.

**Polyphase decomposition** — factoring one long interpolation filter
into L short branches, one per fractional-delay phase, so each output
sample evaluates T taps instead of L·T. The table stores L+1 rows so the
μ wrap 1→0 is branch-free and exactly continuous.

**ppm (parts per million)** — 10⁻⁶, the natural unit of crystal
tolerance and drift. Consumer crystals sit tens of ppm from nominal; the
converter accepts ±1000 ppm by default.

**Q-format (Q0.15, Q1.14, Q1.30, Q0.64 …)** — fixed-point notation:
Qm.n has m integer bits and n fractional bits in a signed word (the
project writes the unsigned 64-bit phase as Q0.64). Q15 audio samples
are Q0.15; the corresponding coefficients are Q1.14 so values slightly
above 1.0 survive; accumulation is int64.

**Ratchet** — the CI mechanism that compares deterministic instruction
counts against committed baselines at ±3% in *both* directions: a
regression fails, and an unexplained improvement also fails until the
baseline is deliberately re-committed. Two-sided so that numbers can
only change on purpose.

**Semihosting** — a debug protocol by which a bare-metal program calls
into its host/debugger for I/O; how the Cortex-M test binaries print
results and exit under QEMU system emulation.

**Seqlock** — a reader-retry publication scheme: the writer makes a
sequence counter odd, writes the payload, makes it even; readers retry
until one even value brackets a whole read. Used by the dual-core
firmware to publish multi-word statistics coherently with only 32-bit
atomics.

**Servo** — a feedback controller steering a plant toward a setpoint;
here the PI controller that steers FIFO occupancy to the target by
adjusting the resampling rate, thereby *becoming* the clock-ratio
estimator.

**Setpoint** — the target FIFO occupancy (`target_latency_frames`),
i.e. the buffering half of the latency budget. Must exceed the pull
block and the peak jitter excursion; the converter raises its
*effective* value when it observes otherwise.

**Sine-fit metrology** — measuring quality by least-squares-fitting the
known test tone (amplitude, phase, frequency) and analyzing the residual
after exact subtraction. Sharper than FFT bins for single-tone tests and
immune to window leakage — leakage of the fitted tone cannot masquerade
as noise or crosstalk.

**Slip** — the whole-sample event in near-unity conversion: after
roughly 1/ppm samples the accumulated fractional position crosses a
sample boundary and the read window shifts by one input sample. The
extra polyphase row makes the slip exactly continuous in the output.

**SNR (signal-to-noise ratio)** — here, the fitted test tone's power
against everything else in the analysis window (a THD+N-style residual,
so distortion counts as noise), in dB.

**Soft float / soft double** — floating-point arithmetic emulated in
integer instructions because the hardware lacks the format — FP64
everywhere on Cortex-M33 and Hexagon. The reason the fixed-point
datapaths exist and the reason the servo's double math is budgeted per
block, not per sample.

**SPSC (single-producer single-consumer)** — the concurrency restriction
of the library's ring: exactly one pushing agent and one pulling agent.
The restriction is what makes lock-freedom cheap — and it is a contract
about agents, not threads, which is what lets two CPU cores satisfy it.

**TCG plugin** — an instrumentation hook in QEMU's Tiny Code Generator;
the project's counting plugin observes every executed guest instruction,
yielding the deterministic per-workload counts the ratchet gates.

**THD+N (total harmonic distortion plus noise)** — everything that is
not the test signal — harmonics, spurs, noise — integrated over the
audio band and expressed relative to the signal. The AES17 measurement
the comparison document reports (−132 dB at the 24-bit interface).

**ThreadSanitizer (TSan)** — a compiler-instrumented data-race detector
that observes the ordering annotations actually used. It certifies only
the interleavings a run produces, which is why the project also runs it
on genuinely weakly-ordered arm64 hardware.

**Type-2 loop** — a control loop with two integrators around the cycle
(here: the PI's integrator plus the FIFO, which integrates rate error
into occupancy). Type 2 is what nulls a *constant* rate offset with zero
standing occupancy error.

**UF2** — the drag-and-drop flashing format of Raspberry Pi boards; the
build artifact of both Pico 2 firmware harnesses.

**Underrun / overrun / resync** — the converter's three accounting
events: a pull found too little data (output silence-padded, refill and
re-lock), a push found the FIFO full (newest frames dropped), and the
consumer-side hard discard back to the setpoint after the high watermark
is reached. All three are counted, published in `converter_status`, and expected
to be zero after lock.

**VLIW (very long instruction word)** — an architecture that packs
several operations into one issue packet scheduled by the compiler, as
on Qualcomm's Hexagon DSP. Why "instructions executed" and "packets
executed" differ there, and part of why instruction counts are budgets
rather than cycle counts.

**Wraparound arithmetic** — unsigned integer arithmetic modulo 2^N,
which C++ defines exactly. The ring's monotonic indices and the DWT
cycle deltas both rely on the same theorem: a difference that fits the
word is computed exactly *through* the wrap, so the wrap is not an edge
case but a non-event.

**xrun** — ALSA's collective name for a device-level underrun or overrun
(the OS missed the hardware's deadline). Handled in the bridge by
`snd_pcm_recover`; distinct from the converter's own underrun/overrun
accounting, which sits one layer up.
