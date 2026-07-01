# Composition: `asrc.hpp`

Every previous chapter built a component that is correct on its own terms.
This chapter is about the file that has no terms of its own: `asrc.hpp`
contains almost no algorithm, no mathematics, and fewer than three hundred
lines that mostly call other files' code. It is also where the only serious
bug in the library's history lived. Both facts have the same cause.
Composition is where each component's assumptions meet every other
component's guarantees, and the gaps between them are invisible from inside
any single file.

The cast, assembled: a `PolyphaseFilterBank` designed at construction, a
`FractionalResampler` that owns the history and the phase, a `SpscRing`
carrying interleaved frames between the two clock domains, and a `PiServo`
turning ring occupancy into a rate estimate. `BasicAsyncSampleRateConverter`
wires them together and adds the four things none of them could own alone:
a lifecycle state machine, an under/overrun policy, telemetry, and
validation.

![The composed converter: producer pushes into the ring, the servo turns
ring occupancy into a rate estimate, the resampler consumes at that rate,
the consumer pulls](../img/architecture.svg)

*The whole machine on one page. The ring is the only structure both clock
domains touch; everything downstream of it — servo, resampler, and both
their states — lives on the consumer's side, which is why `pull()` carries
all the policy and `push()` is eight lines.*

## The two-agent shape

The public surface is two functions and a contract:

- `push(interleaved, frames)` — called by exactly one producer agent, at
  the input clock's pace.
- `pull(interleaved, frames)` — called by exactly one consumer agent, at
  the output clock's pace.

"Agent" rather than "thread" is deliberate. On a workstation the two agents
are threads; on the dual-core RP2350 firmware they are two processor cores;
in the deterministic test simulator they are interleaved events on one
thread. The converter never creates a thread, never names a thread, and
never synchronizes beyond what the ring already provides — it is a passive
object that two callers animate. This is why the library contains no
`std::thread`, no executor, and no callback registration: the moment a
library owns threads it owns scheduling policy, priorities, and shutdown
order, all of which belong to the application. The cost of this design is a
sharp, documented affinity contract (push is producer-only, pull is
consumer-only, `resetFromConsumer` is consumer-only); the C-ABI header
restates it because FFI callers can't read C++ doc comments.

`push()` is eight lines and nearly trivial — clip to free space, write,
count an overrun if clipped. All composition complexity lives on the
consumer side, and that too is a decision: the producer is often an
interrupt-context audio callback with the tightest budget in the system, so
every gram of policy was moved to the puller.

## The state machine

`pull()` runs a three-state lifecycle — Filling, then a servo that is
Acquiring or Locked — plus two exceptional transitions. Here is the filling
and resync machinery as it ships:

```cpp
{{#include ../../../include/srt/asrc.hpp:asrc_filling}}
```

```cpp
{{#include ../../../include/srt/asrc.hpp:asrc_resync}}
```

Filling exists because the resampler cannot produce its first output until
a full window of `taps()` history frames exists, and the servo cannot
regulate an occupancy that is still climbing toward its setpoint. So the
converter emits silence until the backlog reaches `setpoint + taps`, primes
the resampler's window in one gulp, seeds the servo's smoothers at the
observed occupancy (so the loop starts from truth rather than slewing from
zero), and begins converting — with a fade-in, discussed below.

The two exceptional transitions are the under/overrun policy, and their
asymmetry rewards attention. **Underrun** (the consumer outran the data):
pad the rest of the block with silence, count it, return to Filling — but
call `servo_.reset(true)`, the flavor that *keeps the integrator*. The ppm
estimate is the accumulated knowledge of where the other crystal sits; a
dropout interrupts the audio, not the physics, so the estimate survives and
re-lock after a dropout takes a fraction of the original acquisition time.
**Overrun pressure** (the consumer stalled long enough for occupancy to
pass the high watermark): discard down to the setpoint in one cut, count a
resync, and re-seed the smoothers — because after a deliberate
discontinuity in the observable, letting the loop "discover" the jump would
inject exactly the transient the seed avoids. One subtlety in the resync
was wrong for months: the discard must be clamped to what the *ring*
actually holds, because the occupancy figure includes frames already staged
inside the resampler's pop scratch, which no ring discard can reach. With a
setpoint smaller than that staging buffer, the unclamped subtraction
drained the ring to zero and the converter fell into a refill-underrun
cascade. An audit found it; a regression test now pins it.

The fade-in deserves its sentence of honesty, which the header also
carries: after every (re)fill the first 64 frames ramp linearly from
silence, so *recovery* never clicks — but the dropout's onset, and a
resync's splice, are unfaded cuts, because at the moment they happen there
is nothing valid to fade toward. A design can only be honest about which
discontinuities it removes.

## The bug that composition hid

Now the centerpiece, and the reason this chapter exists in its current
form.

Every component below this file was correct. The ring transferred bytes
exactly; the servo regulated occupancy to its setpoint with textbook
dynamics; the resampler synthesized precisely the frames asked of it. And
for months, a converter built from these correct parts, at default
configuration, was **silently broken for the most common audio callback
size in the world.**

The mechanism is embarrassingly simple once stated. A `pull(N)` must
synthesize N frames from data *already in the backlog* — in a real
deployment, no pushes land during the microseconds a pull executes. The
servo, meanwhile, faithfully regulates the backlog toward
`targetLatencyFrames`, which defaults to 48. If N is greater than 48, the
servo's goal and the consumer's need are in direct contradiction: the loop
steers occupancy *down* toward a level from which the next pull cannot be
served. Occupancy drains at the rate clamp, hits the floor, underruns,
refills, fades in — and repeats, forever. Measured at default
configuration: a 64-frame callback drops out every ~0.24 seconds
indefinitely, never reaching Locked, with the reported ppm pegged at a
false +1500 (the clamp, mistaken for the answer). A 240-frame callback
produced 80% silence.

![Measured FIFO occupancy for pull(64) at default configuration, before
and after the feasibility fix](../img/feasibility.svg)

*Both panels are measurements, not models: `scripts/book_figures.py`
compiles the same trace dumper against the include/ tree of the last
pre-fix commit (via `git archive`) and against HEAD, and runs the
identical scenario. Before: drain, underrun, refill — four dropouts a
second, forever. After: one adaptive raise on the first pull, then the
servo regulates the effective setpoint and the underrun count stays at
zero.*

Why didn't anything catch it? Because every artifact that exercised the
converter had, innocently, been configured just clear of the cliff. The
quality tests pull one frame at a time — the metrologically correct choice
for their purpose. The benchmarks set the setpoint to twice the block size —
the performance-measurement-correct choice. The lock tests used 32-frame
blocks against the 48-frame default — feasible. Correct component tests,
correct measurement configurations, months of green CI, and a defaults
matrix with a hole exactly where real applications live. The lesson
generalizes and is worth stating as a rule: **a test suite validates the
configurations it contains, and silence about a configuration is not
evidence about it.** It took an adversarial audit — one explicitly tasked
with constructing failure scenarios rather than confirming passing ones —
to demonstrate it.

The fix is the first thing `pull()` now does:

```cpp
{{#include ../../../include/srt/asrc.hpp:asrc_feasibility}}
```

The design choices inside those lines carry the interesting reasoning:

- **Adapt rather than reject.** The constructor cannot validate this —
  the pull size isn't known until the first pull. Throwing from `pull()`
  is forbidden by the noexcept contract, and returning an error the caller
  must check is how the original silent failure happened, one layer up.
  So the converter raises its *effective* setpoint to what the observed
  block requires and reports the raise through
  `Status::effectiveTargetLatencyFrames`. Latency follows the raised
  setpoint: the honest price, visibly labeled, instead of a dropout cycle.
- **The margin is a half block.** Feasibility strictly needs
  `setpoint ≥ N`; equality grazes, because block-quantized occupancy
  sawtooths around the setpoint. The audit's data located the boundary
  (pull = setpoint showed occasional underruns; pull comfortably below the
  setpoint was clean), and `N/2` covers the sawtooth with room.
- **The raise is bounded by capacity**, computed once in the constructor —
  a setpoint the FIFO cannot sustain would just move the failure. The
  auto-sized FIFO's floor was raised to 1024 frames (21 ms of stereo float
  costs 8 KB — memory is the cheap resource here) so that callbacks up to
  roughly 340 frames work with zero configuration; beyond that, the
  documentation now says plainly: size `fifoFrames` yourself.
- **Feasible configurations are untouched.** The 32-frame-against-48
  default keeps its exact behavior — verified not just by tests but by the
  instruction-count ratchet: every scenario on every embedded target
  measured within ±0.07% across the change, which is construction-cost
  noise. The adaptation is invisible until the moment it is needed.

The audit's failing scenarios became the regression suite
(`Feasibility.Pull64LocksCleanly` and siblings), so the bug's exact shape
is now permanently load-bearing.

## Validation: what the constructor refuses to build

The same audit rewrote `validated()`, and the before/after is a compact
study in what config validation is *for*. The original checked three
fields for zero. The current version rejects, with reasons recorded in a
comment: NaN or infinity anywhere in the numeric config (a NaN sample rate
previously flowed into the filter designer and constructed a converter
that emitted NaN audio — construction succeeding is worse than throwing
when what it constructs is poison); band-edge sums above the sample rate
(an anti-image filter whose cutoff exceeds input Nyquist passes images
wholesale — numerically fine, acoustically wrong); a deviation clamp large
enough that the Q0.64 conversion in the resampler would overflow an
`int64` (undefined behavior guarded at the only gate that sees the value
early enough); and size products that would wrap 32-bit `size_t` on the
embedded targets before `bad_alloc` could save anyone. The principle:
**validate at the boundary where throwing is allowed, against the
invariants of every component downstream** — the resampler can't defend
itself against a config it never sees whole.

One postscript from the portability chapter belongs here too: on one
supported toolchain (Hexagon's static-musl configuration), C++ exceptions
cannot unwind at all, so even this careful `throw` terminates the process
there. Validation still protects — a loud death beats NaN audio — but
callers on that target are documented to validate before constructing.
Contracts end where toolchains do.

## Telemetry that cannot lie about being lock-free

`status()` may be called from any thread, which makes it the one place a
third agent touches the object. Every field crosses via a relaxed atomic,
single-writer, individually coherent but deliberately not mutually so — a
snapshot for humans and supervisory logic, not a synchronization
primitive. The type choices encode a portability fact worth remembering:
the counters are 32-bit atomics because on the 32-bit targets a 64-bit
`std::atomic` falls back to lock-based emulation, and a converter whose
*telemetry* takes a lock has quietly broken the lock-free promise its hot
path makes. The counters wrap at 2^32; the doc comment says so and says
what to do about it. Precision was traded for the contract, and the trade
is written down.

## The underrun tail, end to end

```cpp
{{#include ../../../include/srt/asrc.hpp:asrc_underrun}}
```

Read this excerpt slowly and you can see the whole chapter in ten lines:
the resampler asked to do exactly one job; the fade applied only when
there is something real to fade; the silence pad honoring `pull()`'s
always-fills guarantee; the integrator-preserving reset encoding what a
dropout does and does not destroy; the telemetry publish last, so
observers see states, not mid-transition fictions.

## Verify it yourself

```sh
# The composed state machine, end to end:
ctest --test-dir build -R 'AsrcLock' --output-on-failure

# The feasibility bug's exact former shape, now a regression gate:
ctest --test-dir build -R 'Feasibility' --output-on-failure

# What the constructor refuses to build (NaN, image-passing bands,
# UB-range ppm, undersized FIFOs):
ctest --test-dir build -R 'ConfigValidation' --output-on-failure

# Resync clamping, consumer reset, fade behavior, degenerate calls:
ctest --test-dir build -R 'Resync|Reset|Fade|EdgeCalls' --output-on-failure
```

And one experiment worth running because it *shows you the bug*: check out
any commit before the feasibility fix, build the lock test with
`chunkOut = 64`, and watch a fully green library drop audio four times a
second. Correct parts. Broken whole. That gap is what this file is for.
