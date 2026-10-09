# Three legs, and the bridge to the ASRC

> Trust, but verify.
>
> — Russian proverb, by way of Ronald Reagan

The design brief had one idea it called *the relationship that pays for
itself*: at a pinned rational ratio, the asynchronous converter's output
should converge to the synchronous converter's, so each engine becomes a
golden reference for the other, and the period-147 structure lets the
check be exhaustive rather than statistical. The idea survived; the place
the brief put it did not. This chapter is about where it ended up, the two
other legs that keep it honest, the fixed-point numbers, and the
composition the engine exists to make possible.

## Leg one: scipy, sample for sample

For every direction at every tier, a committed reference vector:
`scipy.signal.resample_poly` over the same per-branch-normalized prototype,
in float64, cast to float32, with the generator's provenance in the
header. The float converter must match from n = 0, transient included
(`Converter.MatchesScipy*`, eight tests). This leg exists for a reason the
plan states plainly: the Kaiser designer is shared between the engine and
its sibling, so agreement between two things built on it would be a
common-mode failure. scipy did not use DspTap.

## Leg two: exhaustive, in every format

Every phase of both superblocks, in all three sample formats, for every
contract the previous chapters stated — the impulse reproducing the table
from every phase, accounting from every position, pull against process,
flush, reset, channel independence, the alias and image measurements
through DspTap's analysis instruments. The acceptance numbers are the
plan's, re-measured on the v0.3 ladder and held: at `economy` every
spurious product at least 71 dB below the source (design floors −71.5 dB
down, −71.7 dB up); at `transparent` at or below −121 dB with the
passband flat to 20 kHz within ±0.00001 dB. Two species of product are
measured separately, because they behave differently: decimation
*aliases* going down are confined above 20 kHz by the arithmetic of this
part's first chapter, while upsampling *image leakage* going up
folds in band and is bounded by the stopband — a 997 Hz tone measures
about 91 dB against its imaging floor in float. The plan records that the
engine's first phrasing, "nothing measurable below 20 kHz", overstated
`economy`: that claim holds at the `transparent` tier, and `economy`'s
honest in-band bound is the stopband.

## Leg three: the cross-validation, relocated

The brief wanted to run the ASRC with its servo pinned to 147/160 and
compare "within servo ripple". That is unworkable, for a reason Part I's
servo chapter makes obvious in hindsight: the ASRC is a *near-unity*
converter whose occupancy servo clamps at a few hundred ppm, and 147/160
is an 8.8 % offset it cannot acquire and was never meant to. The golden
reference moved one layer down, to where it works. `test_cross_validation.cpp`
drives the ASRC's `fractional_resampler` — the datapath alone, no servo —
at a *pinned* eps = L/M − 1, over the identical input and the identical
plain-Kaiser prototype, and compares against this engine phase by phase.

The alignment is the delicate part. The resampler is primed with T − 1
zeros ahead of the signal so that its first window equals the converter's
zero-primed first window exactly, and a 1/(2L) group-delay skew between
the two machines' conventions is compensated. From there both advance at
the same rational rate: the resampler's Q0.64 accumulator departs from the
exact ratio by 2⁻⁶⁴ per output, about 5 × 10⁻¹⁶ samples over the whole
test. The phase identity phase(n) = (nM mod L)/L holds for both, so the
worst disagreement is taken per phase, and every phase of both superblocks
must pass.

Measured: **1.2 × 10⁻⁵ going down (−98 dB) and 3.1 × 10⁻⁵ going up
(−90 dB)** on the v0.3 `economy` designs, against tolerances of 3 × 10⁻⁵
and 8 × 10⁻⁵ — and, the number that makes the result mean something, *the
same* with the ASRC's table at L = 512 and at L = 1024. The ASRC's
μ-interpolation residual falls about 12 dB per doubling of its L; a
disagreement that does not move when L doubles is not that residual. It is
the one deliberate difference between the two filters: this engine's
per-branch DC normalization, which the ASRC's bank does not apply. The
exact machine and the interpolated one agree down to the one thing they
were designed to disagree about. The family's rule R4 keeps this leg's
independence where it comes from: a change to its tolerances leaves the
scipy vectors untouched and does not also touch the ASRC's datapath.

## Sixteen bits, and which tier to pair them with

The fixed-point profiles are the family's, through the shared traits, and
their numbers are pinned in `test_converter_fixed_point.cpp` with the
thresholds set about 4 dB under what was measured:

- **Q31 tracks the float golden model within 5 × 10⁻⁸ per sample
  (−147 dB)** on the reference noise and measures **146 dB** SNR at 997 Hz
  through `transparent` — *exceeding* float, whose float32 input and output
  are its own bound. Q31 is the float profile with integer arithmetic.
- **Q15 is format-limited**, and the measurement produced the engine's
  one counter-intuitive recommendation. At 997 Hz half scale, `economy`
  measures 76.5 dB and `transparent` 72.8 dB: the *deeper* filter is
  *worse* at sixteen bits, because Q1.14 coefficient noise stacks with the
  tap count (184 against 58) while the 120 dB stopband buys nothing a
  16-bit format can express. So `economy` is the Q15 pairing — cheaper and
  quieter — and the README says so as guidance rather than as a number. The
  next part's engine reached the same conclusion for its own long
  `transparent` designs, for the same reason.
- Full-scale drive saturates without wrapping; DC emerges within one LSB
  at every phase of both directions (`FixedPoint.DcEveryPhase*`), which is
  what the per-branch normalization and the row-sum quantization were for.

## The composition: `bridge` converts the number, `async` absorbs the clock

Everything above is a converter for one clock. The case that motivated the
engine is a Bluetooth chip delivering 44.1 kHz audio *on its own crystal*
to a host running 48 kHz on another, which is both problems at once, and
the family's answer is composition rather than a third engine:

```text
receive:  BT codec (44.1 @ BT clock) → bridge 44.1 → 48 → ASRC push │ pull @ local 48 kHz
send:     local 48 kHz → ASRC push │ pull @ BT pace → bridge 48 → 44.1 → BT codec
```

`bridge` is clock-agnostic — a pure sample-count transformer — so placed on
the Bluetooth side it leaves the ASRC seeing nominal 48 kHz on both faces
with the crystal's ppm offset passed through unchanged (ppm is
dimensionless), which is exactly the near-unity regime Parts 0–V built it
for. Neither engine grows the other's scope; the capability lives at the
seam, and the seam is why this engine carries both call shapes.

`bridge/examples/bluetooth_bridge.cpp` runs the receive path against a
deterministic two-clock simulation with the Bluetooth crystal at +200 ppm,
waits for the servo to lock, and measures the recovered tone at the local
clock. On this book's tree it prints: the ASRC locked with a ppm estimate
of +200.1 against the simulated +200.0 — the fixed ratio passed the offset
through — the tone at 997.000 Hz with amplitude 0.5001 and 72.8 dB SNR, and
1.93 ms of latency end to end: 0.43 ms of it this engine's, 1.50 ms the
ASRC's. Part V stated the decision rule from the outside; this is the
recipe, measured.

## The C boundary and the ratchet

The C ABI is the family's minimal shim — seventeen functions: direction
as an integer, the profile as an integer, the format as an integer (float
for the notebook, Q15 and Q31 for the deployments the Q15 profile exists
for, each pinned bit for bit against `basic_converter`), exact accounting,
process and flush in the converter's format, latency, taps, and the
bit-packed family version — built so that the demo notebook measures the
shipping C++. It stays at K = 0 until a consumer asks for the scaled pairs
through C. The instruction-count ratchet
gates ten streaming workloads — direction × float / Q15 / Q31 at `economy`,
both `transparent` float legs, and both `super_economy` Q15 legs, two
seconds of stereo each — on the M33, the M55 and Hexagon at ±3 % two-sided,
and since M7e two construct-only legs beside them (the Q15 `economy`
converter built in each direction and one block processed), because on the
M33 the Q15 streaming totals were 55–63 % constructor. Every number in the
previous chapter's campaign is one of those ten, moved and re-recorded in
the pull request that moved it.

## What this part was about

Take an ASRC, pin its ratio, and remove everything that existed to track a
ratio that moves: the result is a table, a schedule and a dot product, and
a test discipline that can afford the word *exhaustive*. Then spend the
budget that generality used to consume on the one thing that is left — the
inner loop — one measured lever at a time, and keep the numbers, including
the ones that went the wrong way. The engine of the next part is this
machine generalized along the one axis the family needed, a small-factor
ratio instead of one fixed pair, and it inherits the schedule, the
profiles, the call shapes and the legs from here.

## Verify it yourself

```sh
# The whole engine:
ctest --test-dir build --output-on-failure -L '^bridge$'

# The cross-validation leg alone (prints the measured worst |diff| per
# direction and async L next to the tolerance):
ctest --test-dir build -R 'bridge\.CrossValidation\.' --output-on-failure

# The fixed-point numbers, printed as measured:
ctest --test-dir build -R 'bridge\.FixedPoint\.' --output-on-failure

# The composition, end to end:
cmake -S . -B build -DTAP_SR_BUILD_EXAMPLES=ON && cmake --build build -j
./build/bridge/examples/bluetooth_bridge

# Break it on purpose: in design_prototype, delete the per-branch
# normalization loop. The spec sweeps still pass — the response moves at
# the 5e-6 level — but DcEveryPhaseQ15Down fails, and the cross-validation
# floor *improves*, because you just removed the one deliberate difference
# between the two engines.
```
