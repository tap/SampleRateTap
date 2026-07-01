# The clock servo: `pi_servo.hpp`

There is a number this entire library exists to find, and nobody will tell
it to us.

Call it ε: the fractional rate mismatch between the two crystals. The
producer's device claims 48 kHz and delivers 48 000 × (1 + ε) frames per
second; the consumer's device claims 48 kHz and takes them away at
48 000 × (1 + something else). ε is a few parts per million, it wanders
with temperature, and no API on either side will report it — the whole
premise of the problem is that both devices believe they are correct. The
resampler in the next chapter can apply any rate correction we ask of it,
to a resolution of 2⁻⁶⁴ samples. It just needs to be told the number.

The only observable we have is the elastic buffer between the domains: the
SPSC ring from the last chapter, whose occupancy was designed to be *exact*
for precisely this reason. If the producer's clock is fast by ε and we
consume at exactly the nominal rate, the buffer fills at ε × fs frames per
second — about one frame every two minutes at 200 ppm. That trickle is the
entire signal. The servo's job is to turn it into an estimate ε̂ good
enough that the resampler's output carries no audible trace of the
estimation process — and "audible trace" here means fluctuations in ε̂,
because whatever wobble the servo passes into the rate estimate
frequency-modulates every sample of the audio.

This chapter is control theory for someone who has never tuned a loop,
taught the way this file was actually designed: start with the physics of
the thing being controlled, discover why the obvious controller fails,
derive the one that works, and then spend most of our effort on the real
enemy — which turns out not to be the clocks at all, but the fact that we
can only *count*.

## The plant: a buffer that integrates

Control theory calls the thing you are controlling the *plant*. Ours is
the FIFO, and its equation of motion is one line. The producer inserts
fs × (1 + ε_true) frames per second. The converter synthesizes fs output
frames per second, and for each output frame it consumes (1 + ε̂) input
frames — that is what "phase advance = 1 + ε̂" will mean in the next
chapter. Occupancy changes at the difference of those rates:

```text
d(occ)/dt = fs · (ε_true − ε̂)
```

The buffer is a **pure integrator** with gain fs. Feed it a rate error and
it does not settle at some proportional level — it ramps, forever, until
it hits a wall (empty: dropout; full: overflow). Two consequences follow
immediately. First, doing nothing is not an option even for arbitrarily
small ε: any uncorrected mismatch is a glitch with a countdown timer on
it. Second, the plant's own integration is going to interact with whatever
memory the controller has, and getting that interaction right *is* the
design.

The servo observes the occupancy once per `pull()` — the converter calls
`update(occ, mu, dt)` with the raw backlog in frames, the resampler's
current fractional position μ (so the observable `occ + mu` moves
continuously through whole-sample slips instead of staircasing by ±1),
and the elapsed time `dt = framesPulled / fs`.

## Why proportional control is not enough

The obvious controller is proportional: measure the occupancy error
`e = occ − target`, set ε̂ = Kp·e. If the buffer is too full, consume
faster; too empty, consume slower. It even works, in the sense that it
does not fall over.

Now ask what it converges *to*. In steady state the occupancy stops
moving, so the plant equation forces ε̂ = ε_true — the estimate must equal
the true offset exactly. But a proportional controller can only produce
ε̂ = Kp·e, so the error cannot be zero: it must park at

```text
e_ss = ε_true / Kp
```

a *standing occupancy offset* proportional to the clock mismatch. Plug in
the numbers this library actually uses and the problem stops being
academic. At the steady-state loop bandwidth of 0.05 Hz (we will get to
why it is that low), Kp ≈ 1.3 × 10⁻⁵ per frame. A routine 300 ppm crystal
offset parks the buffer **23 frames** away from its setpoint — half the
default 48-frame latency budget gone, sitting one frame shy of the default
24-frame unlock threshold, and different for every unit in the field
because every crystal pair drifts differently. Latency that depends on
which two devices you happened to plug in is not a spec anyone signs.

The fix is memory. Add an integral term:

```text
ε̂ = Kp·e + Ki·∫e dt
```

The integrator accumulates error until the error is gone: in steady state
it holds the entire ppm estimate by itself, ε̂ = ε_true with **zero
standing occupancy error**. Control theory calls the combination a *type-2
loop* — two integrators around the cycle, the plant's and the
controller's — and type-2 is exactly the order needed to null a constant
rate offset. `tests/test_servo.cpp` pins this down against a pure
simulation of the plant equation: after settling at +300 ppm, the
occupancy must sit within 0.05 frames of the setpoint and ε̂ within 1 ppm
of the truth
(`Servo.LocksFromConstantOffsetAndNullsError`).

A type-2 loop also does something a type-1 cannot: it follows a *ramp* in
the offset — a crystal warming up, drifting at 1 ppm/s — with bounded
rather than growing error. The residual is the classic acceleration error
`e_ss = (dε/dt · fs) / ωₙ²`, about 0.49 frames for 1 ppm/s at the 0.05 Hz
bandwidth, and `Servo.TracksSlowDriftRampWithBoundedLag` holds the
measured lag under one frame while `epsHat` tracks the moving truth to
2 ppm.

If this structure sounds familiar, it should. Replace "FIFO occupancy"
with "phase difference" and this is a **phase-locked loop**: the FIFO
comparison is the phase detector, the PI filter is the loop filter, and
the resampler's μ accumulator is the numerically controlled oscillator.
The README states the analogy flatly and it is worth internalizing,
because it means every result in fifty years of PLL literature applies —
including the one that matters most here: the loop bandwidth f_L
*partitions* the input timing jitter. Components above f_L are absorbed
by the buffer and never reach the audio; components below f_L pass into
ε̂ and frequency-modulate it. Choosing f_L is choosing which noise you
eat.

## From bandwidth to gains

So the designer picks a bandwidth and a damping; the gains should follow
mechanically. Close the PI controller around the integrator plant and the
loop's characteristic equation is

```text
s² + fs·Kp·s + fs·Ki = 0
```

Match it against the standard second-order form
`s² + 2ζωₙs + ωₙ² = 0` — the form whose behavior every control textbook
tabulates — and read off the gains:

```text
ωₙ = 2π·f_L        Kp = 2ζωₙ / fs        Ki = ωₙ² / fs
```

The code computes exactly this, nothing more:

```cpp
{{#include ../../../include/srt/pi_servo.hpp:sv_gains}}
```

Note the division by `fs_` in both gains: the plant's gain is fs, so the
controller divides it back out, and the *closed-loop* behavior depends
only on f_L and ζ. That innocuous-looking normalization is load-bearing —
it is why the gains formula is rate-portable, and (foreshadowing the first
war story) why everything *else* in the config is not.

Damping defaults to ζ = 1, critical damping: the fastest settling that
never overshoots. Overshoot in this loop is not a cosmetic wiggle — an
occupancy overshoot is latency spent grazing the underrun floor, so the
choice is not stylistic.

Here is the full tuning surface, with the defaults that suit a 48 kHz
near-unity converter:

```cpp
{{#include ../../../include/srt/pi_servo.hpp:sv_config}}
```

Three bandwidths, three smoother corners, and a small state machine's
worth of thresholds. A single PI loop needs exactly two numbers; this
config carries fourteen. The rest of the chapter is about earning each of
the extra twelve.

## The enemy: a sawtooth made of counting

If the occupancy were a real number observed noiselessly, one PI loop at
a modest bandwidth would end this chapter. It is not. The occupancy is a
**count** — quantized to whole frames on the producer side, or to whole
*push blocks* when the producer delivers audio in callbacks — and that
quantization is not benign random noise. It is deterministic and
periodic.

Picture the steady state at +200 ppm with sample-granular transfer. The
true (unquantized) backlog creeps upward by ε input samples per sample
consumed; every time the creep accumulates one whole frame, the count
steps. The observable is a perfect sawtooth: one push-block peak to peak,
repeating at the *beat frequency*

```text
f_beat = ε · fs / pushBlock      (the README's "ppm × pushRate")
```

At 200 ppm and sample-granular push that is 9.6 Hz with a one-frame tooth.
With 32-frame callbacks it is 0.3 Hz with a **32-frame** tooth — the
occupancy legitimately excursions ±16 frames with neither clock having
moved. (`AsrcLock.LocksAndHoldsAtConstantOffset` averages straight
through that sawtooth and requires the *mean* fill and ppm to land on the
truth.)

Why care about a deterministic wobble in a number we only use for its
average? Because the loop does not know it is a wobble. Whatever fraction
of the sawtooth survives into ε̂ becomes a periodic modulation of the
resampling rate — FM sidebands on every tone in the program material, at
offsets of f_beat and its harmonics. And a PI controller is a terrible
filter: above f_L its proportional path passes measurement noise straight
through at gain Kp, flat, forever. Narrowing f_L does not fix this by
itself; it lowers Kp (helping linearly) while the sawtooth needs 60–120 dB
of suppression. The loop needs help *before* the loop: error prefilters.

But a prefilter is lag, and lag inside a feedback loop erodes phase
margin; you cannot smooth aggressively *and* acquire quickly with the same
settings. There is no single operating point that pulls in a cold start
within a second, rejects a 9.6 Hz sawtooth by 100+ dB, and follows a
warming crystal. So the servo refuses to pick one point. It picks three.

## Three loops, one integrator

| Stage | Loop bandwidth | Error prefilter | Role |
|---|---|---|---|
| **Acquire** | 10 Hz | 1-pole, 50 Hz | pull in from a cold start (~1 s to lock) |
| **Track** | 1 Hz | 1-pole, 5 Hz | robust lock; terminal stage for coarse-block transfer |
| **Quiet** | 0.05 Hz | 3-pole cascade, 0.5 Hz | steady state for fine-grained transfer |

Each stage is the same PI structure with gains from the same
`computeGains`, differing only in bandwidth and in how hard the
measurement is smoothed before the loop sees it. The update begins by
maintaining *both* kinds of smoothed error on every call:

```cpp
{{#include ../../../include/srt/pi_servo.hpp:sv_update_smooth}}
```

Two details here repay attention. The smoothing coefficient
`alpha(cornerHz, dt) = 1 − exp(−2π·f·dt)` is the exact discrete step of a
one-pole lowpass over an arbitrary interval, so the filter corners are
honest frequencies in Hz regardless of how large or irregular the pull
blocks are — the same property the gain formulas have via `dt` in the
integrator. And the three-pole quiet cascade (`q1_ → q2_ → q3_`) runs
**always**, even in Acquire and Track where its output does not drive the
loop. That costs three multiply-adds per block and buys two things: the
promotion gate into Quiet has real data to judge (next section), and at
the instant of promotion the cascade is already settled on the observable
— no filter warm-up transient handed to the narrowest, most fragile
stage.

Why a *cascade* of three identical poles rather than one pole three times
lower, or something sharper? Rolloff. One pole buys 6 dB/octave above its
corner; three poles buy 18 dB/octave. Against the 9.6 Hz sawtooth, a
0.5 Hz three-pole cascade provides roughly (9.6/0.5)³ ≈ 77 dB of rejection
before the loop even sees the error — while adding only manageable lag at
the 0.05 Hz loop bandwidth two decades below. The file header states the
net result as a system-level figure: in Quiet, a one-frame sawtooth is
rejected to roughly −120 dBc equivalent at 20 kHz, while the loop still
follows a 1 ppm/s drift ramp with under half a frame of standing error.
Sharper IIR shapes (resonant poles, elliptic-style) would trade that
clean, phase-predictable lag for ringing inside a feedback loop — exactly
the wrong place for it.

## The promotion machine

Three stages need transitions, and transitions are where multi-mode
controllers usually betray you — a bandwidth switch with mismatched state
is a step input injected into your own loop. Here is the whole state
machine:

```cpp
{{#include ../../../include/srt/pi_servo.hpp:sv_update_stages}}
```

Reading it as a protocol: promotion out of Acquire requires the *fast*
smoothed error to stay inside one frame for half a second; promotion out
of Track requires the *cascade* error to stay inside one frame for two
full seconds. Demotion is the same test run backwards with a much wider
threshold — 24 frames — and drops exactly one stage. The asymmetry
(narrow gate up, wide gate down, long holds) is hysteresis by
construction: the servo would rather linger a stage wide than oscillate
between modes.

The choice of *which* error gates the Track→Quiet promotion is the
subtlest line in the file, and it earns the second war story below.
Gating on the cascade-smoothed error means the promotion asks precisely
the question that matters: *after the smoothing Quiet would actually use,
is the observable quiet enough to run a 0.05 Hz loop?* When a large block
beat dominates the occupancy, the answer is naturally and persistently
no — the cascade output wobbles by more than a frame at the beat
frequency, the hold timer keeps resetting, and the servo stays in Track.
Nobody wrote a rule that says "coarse-block configurations must not enter
Quiet." The physics writes it.

Both promotions share their hold logic, and it does double duty:

```cpp
{{#include ../../../include/srt/pi_servo.hpp:sv_hold}}
```

While the hold window runs, the servo is not just waiting — it is
averaging its own output ε̂ with a time constant of a fifth of the hold.
Here is why that average exists. The wide stages do not *reject* the
quantization sawtooth; they phase-track it, riding the wobble with their
whole loop. Their instantaneous ε̂ is therefore a good estimate wrapped in
a periodic error. Averaging over the hold window (many beat cycles)
strips the wobble and leaves the clean central value — and at the moment
of promotion, *that* is what gets loaded into the narrower stage's
integrator (`integ_ = clamp(epsAvg_, ...)` in the state machine above).

Recall what the integrator *is* in steady state: the entire rate
estimate. Handing the next stage a clean integrator means handing it a
loop that is already essentially converged; the proportional path only
has to clean up residuals. That is the transient-free handoff — "to first
order," as the header says, because the smoothers keep their state and
the observable keeps its continuity, so nothing steps.
`Servo.BandwidthSwitchIsTransientFree` runs the plant through lock and
across both promotions and requires the occupancy never to leave the
one-frame lock threshold afterwards: a handoff you cannot find in the
data.

## The output stage, and why the clamp is inside

The last lines of `update()` are the PI itself:

```cpp
{{#include ../../../include/srt/pi_servo.hpp:sv_update_out}}
```

The clamp appears twice, and the first one — on the integrator, not just
the output — is the anti-windup that every practical PI needs and every
first implementation forgets. Consider a consumer stall: the occupancy
error goes huge and stays huge for seconds while the converter waits for
the high-watermark resync. An unclamped integrator would spend that whole
time charging toward a rate estimate of thousands of ppm — a number no
crystal pair can produce — and then, after the disturbance clears, the
loop would have to *discharge* all of that false conviction through its
narrow bandwidth, dragging the occupancy through a huge excursion for tens
of seconds. Clamping the integrator at 1.5 × `maxDeviationPpm` bounds the
lie the loop can tell itself: the estimate can never leave the range
physics allows, so recovery from any disturbance starts at most one clamp
width from the truth. The output clamp then bounds what the resampler is
asked to do per sample (which also protects the Q0.64 conversion in the
next chapter). `Servo.ClampsToMaxDeviation` feeds a 10 000-frame error and
requires the output to saturate exactly at 1.5× the configured range.

## Knowing when not to chase: `seed()` and `reset()`

```cpp
{{#include ../../../include/srt/pi_servo.hpp:sv_reset}}
```

A feedback loop's reflex is to chase every step in its input. Some steps
carry no information, and the API encodes each such case explicitly:

- **`seed(occPlusMu)`** snaps all four smoothers onto the current
  observable. The converter calls it when the occupancy jumps *for a
  known reason* — acquisition start, a hard resync discard. Without it,
  the smoothers would report the jump as a genuine multi-frame error and
  the loop would obediently swerve.
- **`reset(keepIntegrator=true)`** re-arms the state machine after a
  dropout but preserves the integrator — because a dropout says nothing
  about the crystals. The ppm estimate from before the glitch is still
  the best available number, and relock becomes a formality
  (`Servo.DropoutResetKeepsPpmEstimate` pins both flavors: `true`
  preserves the estimate to 5 ppm, `false` zeroes it).
- **`setTarget()`** moves the setpoint while keeping the integrator *and*
  the smoothers' tracking state, so the loop slews to the new occupancy
  at its clamped rate with no discontinuity — used by the converter's
  adaptive pull-block setpoint raise, where the setpoint moves but,
  again, the clocks have not.

The shared principle: the integrator is the loop's knowledge and the
smoothers are its perception. Each event handler keeps exactly the state
that is still true and resets exactly the state that is not.

## War story one: 16 kHz, minus 32 decibels

For a long time this library's defaults were "the defaults," full stop —
designed, tested, and shipped at 48 kHz. Then a real deployment shape
arrived: 16 kHz reference-microphone processing. Same code, same presets,
a third of the sample rate. The quality suite was duplicated at 16 kHz,
expecting boring numbers.

The numbers came back **~32 dB worse at every tone**, falling a further
6 dB per octave of signal frequency. That frequency signature is the
fingerprint of small-index FM — phase modulation of the resampling
position, whose sidebands grow with the modulated signal's frequency —
which pointed at the servo, not the filter.

The mechanism, worked out in
`tests/test_asrc_quality_16k.cpp`'s header comment and now baked into the
config comment: servo bandwidths and smoother corners are **absolute
hertz**, but the disturbance they exist to reject is not. The slip-beat
sawtooth sits at ε × fs — 9.6 Hz at 48 kHz, only **3.2 Hz at 16 kHz**.
The three-pole 0.5 Hz cascade whose rejection goes as f³ therefore does
(16/48)³ ≈ 28.6 dB *less* damage to the beat at 16 kHz, and the
measurement becomes servo-FM-limited: predicted ≈ 28.6 dB, measured
≈ 32 dB. The loop was not misbehaving. It was doing exactly what its
absolute-Hz constants said, against a disturbance that had moved.

The rule that fixes it is now a method, so it cannot be half-remembered:

```cpp
{{#include ../../../include/srt/pi_servo.hpp:sv_scaled_to}}
```

Every field with units of Hz scales with the rate — keeping the loop
identical in *normalized*, per-sample terms, which is the frame the
disturbance lives in. Every field denominated in frames or ppm
(`lockThresholdFrames`, `unlockThresholdFrames`, `maxDeviationPpm`) is
already normalized and stays put. And the hold times scale *inversely*:
a loop with a third the bandwidth has time constants three times longer,
so waiting "2 seconds" before promoting would mean waiting a third as
many loop time constants — the gates would fire on less evidence. The
original hand-scaled 16 kHz configuration missed the hold-time rule;
adding it re-measured identical within noise, and the test suite now
covers the factory (`Config::forSampleRate`, which applies this and the
matching `FilterSpec::scaledTo`) both structurally
(`AsrcQuality16k.ForSampleRateScalesHzFieldsOnly` checks exactly which
fields move) and behaviorally: through the factory, 16 kHz measures
136.6 dB at 333 Hz — within ~1 dB of 48 kHz at the same normalized
frequency, the 32 dB fully recovered.

One more cost of scaling, honestly: at 16 kHz the Quiet loop runs at
~0.017 Hz, so the quality tests run for 120 seconds of simulated audio
instead of 40 — the same number of loop time constants. Slow loops are
slow everywhere, including in CI.

## War story two: when Track is the ceiling

The block-size study (`notebooks/asrc_block_size_study.ipynb`) asked what
happens as transfer granularity coarsens from sample-granular toward the
32- and 240-frame callbacks real audio APIs deliver. The finding shapes
how you should read the stage table: with blocks of 32 frames and up,
**the servo never promotes to Quiet — and must not.**

The information-theoretic version of the argument: at a 32-frame block,
the occupancy observable updates a few hundred times per second with a
±16-frame deterministic sawtooth on top of a sub-frame-per-second signal.
Quiet-level performance means resolving the backlog trend to a small
fraction of a frame *through* that tooth using counts alone; the counts
simply do not carry the information. The promotion gate discovers this
without being told: the cascade-smoothed error keeps excursing past one
frame at the beat frequency, the two-second hold never completes, and
Track becomes the terminal stage — the discriminator working as designed.

What does Track-forever sound like? The 1 Hz loop phase-tracks the block
beat: most of the sawtooth is absorbed as **latency breathing** — the
buffer level, and hence the delay, swaying by a fraction of the block at
the beat rate, inaudible by construction. The remainder leaks into ε̂ as
low-rate FM, and the study put calibrated numbers on it: **~0.9 cents rms
of frequency wobble (61 dB wideband quality) at 32-frame blocks, ~1.3
cents / 53 dB at 5 ms blocks**, as the README reports. Cent-scale wobble
at sub-hertz rates is at the edge of perception for sustained pure tones
and irrelevant for program material — but it is a real ceiling, and it is
a *sensor* ceiling, not a servo defect. The README's limitations section
draws the forward-looking conclusion: breaking it requires a better
observable (per-block timestamps for sub-sample phase observation), not
a cleverer filter behind the same counts.

The practical corollary is the config comment you may have skimmed past
on `unlockThresholdFrames`: it must sit comfortably above **half the
push/pull block size**, because a coarse-block sawtooth legitimately
excursions that far with the clocks standing still. The default 24 clears
a 32-frame transfer's ±16 with margin. Undersize it — say, 8 against
32-frame callbacks — and the healthy beat itself trips demotion:
Track→Acquire, re-lock, promote, trip again, a mode limit cycle
manufactured entirely in configuration. If you change one servo number
for an embedded deployment, this is the one to check.

## The whole life cycle, measured

Everything this chapter described is visible in one trace: the converter
driven at +200 ppm in deterministic virtual time (1-frame pushes — the
long tests' methodology), with a 50 ms producer stall injected at t = 28 s.

![Measured occupancy and ppm estimate through acquire, lock, a 50 ms
dropout, and re-lock](../img/servo-lock.svg)

*Acquiring's 10 Hz loop rings clamp-to-clamp on the quantized occupancy —
the sawtooth of the "enemy" section, live — yet the smoothed occupancy
never strays two frames from the setpoint, and promotion lands in half a
second. After the stall, `reset(true)` keeps the integrator, so the
re-acquire rings around 200 ppm rather than starting over from zero.
Generated by `scripts/book_figures.py`, which compiles a small trace
dumper against the real headers and runs exactly this scenario.*

## The shape of the design

| Decision | Alternative rejected | Reason |
|---|---|---|
| PI (type-2) loop | proportional-only | P parks a ppm-dependent occupancy offset (≈23 frames at 300 ppm in Quiet); the integrator nulls it |
| Gains derived from (f_L, ζ) via 2nd-order matching | hand-tuned constants | tuning surface is two physical numbers; `computeGains` is the textbook formula, verifiable by inspection |
| Three stages | one compromise bandwidth | pull-in wants 10 Hz, sawtooth rejection wants 0.05 Hz + heavy smoothing; no single point does both |
| Cascade error gates promotion | timer or lock-counter | asks the exact question ("could Quiet's own filtered error hold lock?"); auto-excludes coarse blocks |
| Integrator seeded from hold-window average | reset on transition | wide stages phase-track the sawtooth; the average is the clean estimate — handoffs transient-free |
| Integrator clamp (anti-windup) | clamp output only | disturbances must not charge the estimate past physics; recovery starts near the truth |
| `seed()`/`reset(keepIntegrator)` API | let the loop chase every step | known-cause jumps carry no clock information; keep the knowledge, refresh the perception |
| `scaledTo()` for other rates | reuse 48 kHz defaults | absolute-Hz constants vs a rate-proportional disturbance: measured −32 dB at 16 kHz |

## Verify it yourself

```sh
# The five servo unit tests against the pure plant equation
# (type-2 nulling, ramp tracking, transient-free handoff, clamp, reset):
ctest --test-dir build -R 'Servo\.' --output-on-failure

# The servo inside the real converter: lock/hold through the 32-frame
# block beat, drift-ramp tracking, slip continuity, stall recovery:
ctest --test-dir build -R 'AsrcLock\.' --output-on-failure

# War story one, end to end (long: 120 s simulated per tone; prints the
# measured SNRs — compare against the thresholds in the file):
ctest --test-dir build -R 'AsrcQuality16k\.' --output-on-failure

# War story two: regenerate the block-size study (32 / 64 / 240 frames,
# latency breathing and the cents-rms FM decomposition):
jupyter nbconvert --to notebook --execute notebooks/asrc_block_size_study.ipynb

# Break it on purpose: in tests/test_asrc_quality_16k.cpp, replace
# Config::forSampleRate(kFs) with a default-constructed Config (keeping
# cfg.sampleRateHz = 16000.0) and watch ~32 dB vanish from every tone.
```

As with the ring buffer, the last item is the chapter in one line. The
three stages, the cascade, the scaling rule — none of it is decoration.
Take any piece away and a measurement, not an opinion, tells you what it
was holding back.
