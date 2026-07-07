# Channels, rates, and the rules that scale

> For every type of animal there is a most convenient size, and a large change in size inevitably carries with it a change of form.
>
> — J. B. S. Haldane, *On Being the Right Size*

Every measured number in this book so far was taken at one operating point:
48 kHz, one or two channels, fine-grained transfer. Real deployments move
along three axes away from that point — more **channels**, a different
**sample rate**, coarser **blocks** — and each axis has a rule, a failure
mode when the rule is ignored, and a measurement that pins both. This is
the chapter a deploying engineer should read twice: once before choosing a
configuration, and once after the first surprising telemetry line.

The three rules, stated up front:

1. **Channels**: one converter instance per *clock domain*, never per
   channel group; channel count is then a nearly-free multiplier on the
   dot product.
2. **Rates**: every configuration field denominated in absolute hertz must
   scale with the sample rate — start from `Config::forSampleRate()`.
3. **Blocks**: the FIFO setpoint must exceed the pull block size (the
   converter now enforces this) and the servo's unlock threshold must
   clear the block-quantization sawtooth; coarse blocks also move you into
   a measurably different quality regime.

## Channels: coherence is free, so don't pay for it

`Config::channels` is a runtime count with no architectural limit — mono
through 7.1.4 and beyond. The design rule is about instance boundaries:
**one instance per clock domain**. If a 12-channel AVB stream and a stereo
monitor feed arrive on the *same* recovered clock, they are one domain and
could share one instance per stream as convenient; but never split one
stream's channels across instances, and never funnel two clock domains
into one.

The reason to keep a stream's channels together is a property the
implementation gives you by construction. Within one instance, every
channel of a frame is resampled at *literally the same fractional
position*: the phase accumulator, the servo, and the coefficient blend are
all per-instance state, and the per-channel work is only the dot product.
There is no per-channel phase to drift, so inter-channel phase coherence
is exact — not "matched to within a specification," but bit-identical in
the only quantity that could differ. Two audiences care intensely:

- **Surround imaging.** Phantom sources between speakers are constructed
  from inter-channel amplitude and time relationships; an ASRC that
  resampled channels at even slightly different phases would smear them.
  Here there is no skew to budget for.
- **Microphone arrays.** Beamforming and cross-correlation live entirely
  on inter-channel time differences at sub-sample precision. The README
  calls out the AVB case directly: a stream bundling reference microphones
  with the program feed keeps its array geometry intact through the
  converter. (AVB Class A's 8-frame packets are also fine-grained enough
  for the Quiet servo stage — the block axis, below, cooperates.)

Split those channels across instances and you forfeit the guarantee: each
instance runs its own servo on its own FIFO, and two servos tracking the
same physical clock still produce two independently-wobbling phase
trajectories. The coherence rule costs nothing and buys exactness; its
violation costs exactness and buys nothing.

### What N channels cost

Sharing one fractional position per frame also shapes the cost. Each
output frame computes the coefficient blend — the interpolation between
adjacent polyphase rows — *once*, then reuses it for every channel's dot
product. N channels cost `blend + N × dot`, not `N × (blend + dot)`; the
fixed overhead amortizes, so the marginal channel is cheaper than the
first.

The instruction-count table in the README measures this shape. Comparing
the 12-channel Q15 pipeline against stereo across the three gated targets:

| Target | `pipeline_q15` (2 ch) | `pipeline12_q15` (12 ch) | ratio |
|---|---:|---:|---:|
| Cortex-M33 | 484,146,844 | 962,613,655 | 2.0× |
| Cortex-M55 | 127,446,817 | 387,876,968 | 3.0× |
| Hexagon | 119,847,854 | 378,858,793 | 3.2× |

Six times the channels for 2.0–3.2× the instructions. The spread itself is
informative: the M33's 2.0× says its per-frame cost is dominated by
shared work (the servo's soft-double arithmetic on an FP64-less core), so
extra channels are nearly half price; the M55 and Hexagon, whose shared
work is cheap, sit closer to the pure dot-product slope. On the host, the
same shape: Q15 stereo at 56.0 ns/frame versus 12-channel at 189.1 —
3.4× for 6× the channels, with a 12-channel stream still running at 110×
realtime on one Xeon core.

### The proof that channels don't leak

Coherence and cost say nothing about *correctness* — an interleave bug or
a channel permutation would sail through every single-channel quality
metric in the suite. `tests/test_multichannel.cpp` exists for exactly
that blind spot: every channel of one instance gets its own tone (600 +
731·c Hz — distinct, non-harmonically related, all inside the flat
passband up to 16 channels), and after a +200 ppm crossing each channel
must contain its own tone at full quality and nothing measurable of any
neighbor's.

"Nothing measurable" is made rigorous the way this project usually is: the
channel's own tone is removed by tracked least-squares fit before the
other channels' frequencies are fitted on the residual, so the own tone's
spectral leakage (about −67 dB over a 1 s rectangular window at these
spacings) cannot masquerade as crosstalk. The gated results: worst
crosstalk below **−100 dB** for 12-channel float, below **−72 dB** for
16-channel Q15 — the latter sitting at the 16-bit format's own floor,
which is the honest bound for that datapath. Amplitude and per-channel
SNR are asserted in the same run, so a permutation, a gain error, and
crosstalk are all caught by one test.

One coverage note is worth repeating because of how it was found. The
host's channel-parallel float kernel tiles channels in blocks of 8/4/2/1,
and an audit noticed that no test ever ran the K=2 and K=1 remainder
tiles — every configured count happened to decompose without them. The
suite now runs 5- and 7-channel variants (5 = 4+1, 7 = 4+2+1) precisely
to execute those tiles. The general lesson from Part II recurs: coverage
you haven't verified reaches the code is coverage you don't have.

## Rates: hertz-denominated defaults are a 48 kHz assumption

The library's defaults read as innocently portable — until you notice
which fields carry units. `FilterSpec::balanced()` places the passband
edge at 20,000 Hz and the first image to suppress at 28,000 Hz;
`ServoConfig` sets loop bandwidths of 10/1/0.05 Hz and smoother corners of
50/5/0.5 Hz. Every one of those is an *absolute frequency chosen for
48 kHz operation*, and the two misconfigurations they invite fail in
instructively different ways.

The filter misconfiguration fails loudly, by design. Default band edges at
a 16 kHz rate would put the anti-image cutoff far above the input Nyquist
— a filter that passes images wholesale — and the constructor's validation
rejects the geometry outright (`passbandHz + stopbandHz` must not exceed
the sample rate), so you cannot ship it by accident. The servo
misconfiguration is the dangerous one, because nothing forces you to
notice: scale the filter (you must, to construct at all), keep the default
servo, and the converter builds, locks, tracks, and converts — while
silently costing about **32 dB** of quality at 16 kHz. That number is
measured, and the mechanism is worth understanding because it is the whole
rate-scaling story in one incident.

At 16 kHz with a 200 ppm offset, the whole-sample slips arrive at
`ppm × fs` = 3.2 Hz instead of 9.6 Hz. The servo's three-pole Quiet
smoother has an absolute 0.5 Hz corner, so a beat at one-third the
frequency is rejected `(16/48)³` ≈ 28.6 dB *less* — the slip sawtooth
walks out from under the smoother, leaks into the rate estimate, and
frequency-modulates the audio. The measurement wears the FM signature
openly: roughly 32 dB below the 48 kHz figures at every tone, falling
6 dB per octave of signal frequency, exactly as small-index FM sidebands
scale. Nothing was wrong with the filter; the *control loop* was mistuned
by a factor of three because its tuning was written in hertz.

The remedy is the `scaledTo` trio, and the factory that applies it:

```cpp
srt::Config cfg = srt::Config::forSampleRate(16000.0);
cfg.channels = ...;            // then adjust as usual
```

`FilterSpec::scaledTo` multiplies the band edges by `fs/48000` — same L
and T, so the same table size and per-frame cost, with the identical
response at every *normalized* frequency. `ServoConfig::scaledTo` does the
same to the six bandwidth/corner fields, keeping the loop identical in
per-sample terms — and scales the two hold times *inversely*, so the
promotion gates wait the same number of loop time constants rather than
the same number of wall-clock seconds. (That last refinement postdates the
first hand-scaled fix; re-measured, it changed nothing within noise, and
the test asserting it exists so the equivalence stays checked rather than
remembered.) Frame-denominated fields — lock and unlock thresholds,
`targetLatencyFrames`, ppm limits — are rate-invariant and stay put,
though their *duration* in milliseconds scales inversely with the rate.

`tests/test_asrc_quality_16k.cpp` runs the full quality methodology
through the factory, and the outcome is the point of the design: 16 kHz
matches the 48 kHz *normalized-frequency structure*. The tones sit at the
same f/fs as the 48 kHz suite's 997 Hz / 6 k / 12 k / 19.5 k, and measure
136.6 / 121.9 / 114.3 / 106.5 dB against the 48 kHz suite's 135.0 /
120.0 / 112.8 / 105.8 on the same host — within about 1 dB down the line,
confirming that interpolation noise depends only on f/fs. Two consequences
deploy with you: group delay at the same tap count stays ~24 *input
samples*, which is three times as many milliseconds at 16 kHz (1.5 ms vs
0.5 ms); and the scaled Quiet loop at ~0.017 Hz settles proportionally
slower — the 16 kHz test runs 120 s where the 48 kHz one ran 40 s, the
same number of samples and of time constants.

## Blocks: feasibility, then observability

The block-size axis has two boundaries, one hard and one
information-theoretic.

### The hard one: a pull can only synthesize from what is buffered

`pull(frames)` produces output from frames already in the FIFO. If the
occupancy setpoint sits at or below the pull block size, the loop is
infeasible: each pull drains the buffer past the setpoint, the servo
steers to refill it, the next pull drains it again — a permanent underrun
limit cycle, dropouts every few hundred milliseconds, never locking. Early
versions documented the rule ("the setpoint must exceed the pull block
size") and trusted the integrator between chair and keyboard; the current
converter enforces it. When `pull()` observes a block larger than the
setpoint in force, it raises the *effective* setpoint to the block plus a
margin — half a block, at least one pop chunk — sized so the entry
occupancy never grazes the pull size even at the bottom of the block-beat
sawtooth, and bounded by FIFO capacity:

```cpp
const std::size_t needed = frames + std::max<std::size_t>(frames / 2, kPopChunkFrames);
const std::size_t newTarget =
    std::clamp(needed, cfg_.targetLatencyFrames, maxTargetFrames_);
```

Configurations that already satisfy the rule are left exactly as
configured; the servo slews to a raised setpoint glitch-free (integrator
kept — the clocks haven't changed, only the target). The cost is not
hidden: latency follows the raised setpoint, `designedLatencySeconds()`
reports it, and `Status::effectiveTargetLatencyFrames` differs from the
configured value exactly when the adaptation has occurred — a field worth
plotting in deployment telemetry, because it is the converter telling you
your latency budget and your callback size disagree. Capacity bounds the
raise: the default ring (a 1024-frame floor) accommodates pull blocks up
to ~340 frames; larger callbacks need `fifoFrames` sized explicitly.

### The soft one: what a coarse count can tell a servo

The servo's only sensor is FIFO occupancy, and occupancy is quantized —
to whole frames at best, to whole *blocks* with block transfer. At
deviation ε the observable carries a deterministic sawtooth, one push
block peak-to-peak, at the beat frequency `ε × fs / block`. Whatever the
loop passes into its estimate frequency-modulates the audio. With
sample-granular transfer the sawtooth is one frame and the Quiet stage's
three-pole cascade rejects it to roughly −120 dBc equivalent at 20 kHz.
With ≥32-frame callbacks, that level of quiet is
**information-theoretically unavailable from counts alone** — no filter
recovers sub-sawtooth phase from an observable whose quantization *is*
the sawtooth, not while still tracking real drift.

The design response is to stop pretending. Promotion from Track to Quiet
is gated on the cascade-smoothed error staying small, which is naturally
false while a large block beat dominates the observable — the gate is
itself the discriminator between the two regimes, so coarse-block
operation deliberately stays in Track. There the block beat is mostly
phase-tracked as benign *latency breathing* (the FIFO term of the latency
wanders by a fraction of the block as the servo follows the beat), and
the remainder appears as low-rate FM measured in cents:
`notebooks/asrc_block_size_study.ipynb` puts it at ~0.9 cents rms over a
61 dB wideband floor at 32-frame blocks, ~1.3 cents rms over 53 dB at
5 ms (240-frame) blocks. Those are honest numbers for a different regime,
not a degradation of the headline ones — the 135 dB figures are for
fine-grained transfer, and the comparison document says so plainly. If
your deployment pushes hardware-DMA-sized blocks and needs studio
transparency, the current converter is not information-limited by
accident, and the limitations section of the README sketches the eventual
answer (per-block timestamps for sub-sample phase observation).

One more block-denominated rule closes the loop with the previous
chapter. The servo's `unlockThresholdFrames` (default 24) is the
excursion that demotes a stage; block-quantized occupancy legitimately
excursions by about half a block without the clocks having moved. The
guidance in `pi_servo.h` — keep the threshold comfortably above half
the block — is applied literally in the ALSA bridge (`1.5 ×` the period),
and ignoring it produces the most confusing failure on this axis: a
converter that locks, runs cleanly, and "spuriously" demotes itself on
schedule, at the beat frequency, forever.

## The configuration walk, in order

The axes compose, so a deployment configures them in dependency order:

1. Start from `Config::forSampleRate(rate)` — never raw defaults at a
   non-48 kHz rate.
2. Set `channels` to the full width of each clock domain's stream; one
   instance per domain.
3. Set `targetLatencyFrames` above your pull block *and* your worst
   push/pull jitter excursion (the dual-core firmware's 144-frame
   setpoint against a 2 ms logging stall is the worked example); set
   `fifoFrames` explicitly past ~340-frame callbacks.
4. Raise `unlockThresholdFrames` above ~1.5× your transfer block.
5. Then watch `Status::effectiveTargetLatencyFrames` and the resync
   counters in production — they are the converter's own opinion of
   whether steps 3 and 4 were done right.

## Verify it yourself

```sh
# Channel independence: 12ch float (< -100 dB crosstalk), 16ch Q15
# (< -72 dB), plus the 5/7-channel remainder-tile variants:
ctest --test-dir build -R MultiChannel --output-on-failure

# The rate-scaling rule and the 16 kHz measurements (slow: each case is
# a 120 s simulated run; the first test checks the factory arithmetic
# deterministically):
ctest --test-dir build -R AsrcQuality16k --output-on-failure

# The -32 dB failure itself, reproduced: in test_asrc_quality_16k.cpp,
# keep Config::forSampleRate(kFs) but overwrite the servo with unscaled
# defaults (cfg.servo = srt::ServoConfig{};) — the converter still builds
# and locks, and every threshold fails by ~30 dB, falling 6 dB per octave
# of tone frequency: the FM signature. (Restoring the unscaled *filter*
# instead fails fast: the constructor rejects band edges above the input
# Nyquist.)

# The block axis, measured: latency breathing and the cents-scale FM
# decomposition at 32/64/240-frame blocks:
jupyter nbconvert --execute notebooks/asrc_block_size_study.ipynb

# The feasibility rule live: run the drifting-clocks example, then rerun
# with cfg.targetLatencyFrames set below kChunk in the source — the
# adaptive raise reports itself in effectiveTargetLatencyFrames instead
# of dropping out:
./build/examples/drifting_clocks
```

The break-it-on-purpose suggestions are, as ever, the chapter in
miniature: each rule here was learned from a measured failure, and each
failure is still one edit away from being watched happening.
