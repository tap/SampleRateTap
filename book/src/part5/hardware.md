# Real clocks: bridges and firmware

> Before enlightenment: chop wood, carry water. After enlightenment: chop wood, carry water.
>
> — Zen proverb

Everything measured so far in this book — the 135 dB residual, the lock in
~1 s, the drift ramp tracked without unlocking — came out of a simulation.
A good one: deterministic, sample-granular, reproducible to the bit, able
to synthesize a +200 ppm offset that is *exactly* +200 ppm so the servo's
estimate has a truth value to be judged against. That determinism is the
whole reason Part II's proof system works, and it is also, unavoidably, a
confession. A simulated clock is a number in a loop. It has no crystal, no
temperature coefficient, no USB host controller rescheduling its transfers,
no twelve-hour soak in a warm room. The library exists to reconcile two
*physical* oscillators, and at some point the only honest move is to plug
two of them in.

This chapter is about that move: what real hardware can prove that the
deterministic suite cannot, the three test setups the project defined for
it, and the three harnesses that shipped — an ALSA bridge for Linux hosts
and two firmware images for the Raspberry Pi Pico 2. It ends by stating
plainly which numbers exist today and which still await a physical board.

## What simulation cannot say

Be precise about the gap, because it is narrower than "simulation isn't
real." The two-clock simulator *is* the library's use case in every
algorithmic respect; nothing about the datapath or the servo mathematics
changes on hardware. What changes is the input to the control loop:

- **The offset stops being constant.** Real crystals sit typically
  20–200 ppm apart and move several ppm with temperature — slowly, over
  minutes, as the room warms or a component self-heats. The suite tests a
  *scripted* drift ramp; hardware supplies an unscripted one, forever.
- **The pacing stops being clean.** A simulated push arrives exactly on
  schedule. A USB audio dongle's data arrives when the host controller and
  the kernel get around to it — jitter that is structured, bursty, and
  unlike anything a deterministic loop generates. The FIFO setpoint rule
  ("exceed the peak occupancy excursion of your push/pull jitter") is only
  ever *exercised* by real jitter.
- **Time stops being short.** The quality suite analyzes one second of
  audio after settling. The claim a deployment actually cares about —
  *zero* underruns, overruns, or resyncs over hours — is a statement about
  the tails of every distribution at once, and the only instrument that
  measures tails is a soak. A multi-hour run on independent oscillators is
  the test no simulation honestly replaces.

There is also one thing simulation does *better*, worth keeping: a
synthesized offset is exact, so convergence can be asserted to a tolerance.
Two real crystals give you a true offset you don't know — you can check the
estimate is *stable* and *independently corroborated* (count frames from
each device against `CLOCK_MONOTONIC` for ten minutes; the measured rate
ratio should match the servo's estimate to well under 1 ppm), but never
that it equals a known constant. The hardware plan uses both kinds of truth
deliberately, as we'll see.

`docs/HARDWARE_TESTING.md` defines three setups, in increasing order of
effort, all commodity parts:

1. **One Pi, two USB audio dongles** (~$15 of adapters). Each dongle clocks
   its own 48 kHz from its own crystal; the library bridges them. The
   canonical real-world test, and the source of the headline result the
   project wants: "locked to the real inter-crystal offset of X ppm, N
   hours, zero discontinuities."
2. **Pi + Raspberry Pi Pico 2.** Validates the QEMU-derived Cortex-M33
   numbers on an actual RP2350: real cycles against emulated instruction
   counts, and the dual-core deployment shape.
3. **Two Pis over Ethernet.** The network-audio case, where `push()` sees
   bursty UDP delivery instead of callback-paced blocks — the setpoint rule
   under genuinely hostile jitter.

Setup 1's harness is `examples/alsa_bridge.cpp`; Setup 2's are
`examples/pico2_cyccnt/` and `examples/pico2_dualcore/`. Setup 3's programs
are not yet written. Each shipped harness is worth reading closely, because
each one is the library's documented rules applied under witness.

## The ALSA bridge: two blocking threads, on purpose

The bridge is ~590 lines and structurally almost insolently simple: open a
capture device and a playback device, start two threads, and let each
thread block on its device.

```cpp
std::thread capture([&] {
    // ...
    const snd_pcm_sframes_t n = snd_pcm_readi(in.pcm, dst, period);
    // ...
    asrc.push(buf.data(), frames); // overruns counted by the converter
});

std::thread playback([&] {
    // ...
    asrc.pull(buf.data(), period); // silence-pads while filling/underrun
    // ...
    snd_pcm_writei(out.pcm, src, period - done);
});
```

The simplicity is the point. The library's runtime contract is one producer
agent and one consumer agent, each paced by its own clock — and a blocking
ALSA read *is* a clock. `snd_pcm_readi()` returns when the capture device
has delivered a period of frames, which happens at the cadence of that
device's crystal; `snd_pcm_writei()` blocks until the playback device has
made room, at the cadence of the other crystal. The two threads never
communicate except through the converter, which is exactly the interface
the whole library was designed around. No callbacks, no timers, no event
loop: the hardware paces the threads, and the converter absorbs the
difference. If you want to see the two-agent contract of
[the ring chapter](../part1/spsc-ring.md) with the abstractions removed,
this file is it.

A few decisions inside deserve attention.

**Format negotiation prefers honesty over generality.** The bridge asks
each device for `FLOAT_LE` — the converter's native sample type, no
conversion — and falls back to `S16_LE` with explicit scale-and-clamp
helpers when the hardware refuses. That is the entire format matrix. Cheap
dongles are overwhelmingly S16 devices, and a test harness that negotiated
every ALSA format under the sun would bury its purpose in plumbing. It
also *refuses* a rate it didn't ask for: if the device counters with
anything but the requested rate, the bridge errors out rather than
silently measuring the wrong experiment.

**Xrun recovery is delegated, then observed.** When a read or write
returns an error, the bridge calls `snd_pcm_recover()` and continues;
only an unrecoverable error stops the run. This is deliberate division of
labor: ALSA xruns are a *device*-level discontinuity (the OS failed to
service the hardware in time), and the converter has its own machinery —
silence-padding, refill, re-lock with the ppm estimate kept — for the
*converter*-level consequences. The bridge does not try to be clever
across that boundary; it recovers the PCM and lets the converter's
counters record whatever backlash arrives. During a soak, the once-per-
second status line is where you watch both layers at once.

**The one configuration rule in the file is the servo_config rule.** The
bridge runs with `--period` frames per ALSA transfer (default 128), and
block-quantized transfer means the FIFO occupancy legitimately excursions
by around half a block without the clocks having moved. The servo's
`unlock_threshold_frames` defaults to 24 — tuned for fine-grained transfer —
so the bridge applies the documented rule in code:

```cpp
// Per the servo_config guidance: the unlock threshold must sit
// comfortably above half the transfer block, or block-quantized
// occupancy excursions can demote the servo stage spuriously.
cfg.servo.unlock_threshold_frames =
    std::max(cfg.servo.unlock_threshold_frames, 1.5 * static_cast<double>(args.period));
```

Miss this and the harness would report spurious servo demotions that have
nothing to do with the clocks — a measurement artifact manufactured by the
measurement tool. (The next chapter returns to this rule as one of the
three scaling axes.)

**The telemetry switches are the experiment design.** Three flags turn the
bridge from a demo into an instrument:

- `--csv <path>` writes the once-per-second `status()` snapshot (state,
  ppm, smoothed fill, underrun/overrun/resync counters) as a CSV row. Beside
  it go the instruments that check the servo instead of trusting it:
  `CLOCK_MONOTONIC_RAW`, each device's cumulative frame count, and its ALSA
  xruns. Where the driver provides it, the row also carries each device's
  last hardware pointer update with its own timestamp. A least-squares line
  through each device's frames against time is that crystal's rate, and the
  ratio of the two is the offset the servo is supposed to be estimating.
  `examples/alsa_bridge_report.py` does that regression and reports the
  servo's agreement with it, the ppm wobble and the fill's breathing.
  This is the soak's evidence: the ppm trace over hours *is* the thermal-
  drift measurement, and the counters' final values *are* the
  zero-discontinuity claim. Point a hair dryer at one dongle and the trace
  should show the crystal move several ppm in real time, tracked without
  anything audible; a fast ±50 ppm step should show a stage demotion and a
  re-lock.
- `--dump <path>` has the playback thread also write the post-ASRC float
  stream to disk, raw. This exists because of an honest limitation of
  cheap hardware: a $7 dongle's analog path measures around −80 dB, and
  no quality claim about a 135 dB converter survives passage through it.
  The dump sidesteps the analog path entirely — the *clocks* are real even
  if the signal never goes analog — and the notebook tooling
  (`notebooks/asrc_comparison.ipynb` carries the AES17-style measurement
  machinery) analyzes the capture offline.
- `--tone <hz>` completes that thought. In tone mode the capture thread
  *still blocks on* `snd_pcm_readi()` — the input device's crystal still
  paces every push — but the captured samples are discarded and a clean
  synthetic sine is pushed instead. Real clocks, known signal, no trust
  placed in an ADC that hasn't earned it. The combination
  `--tone 997 --dump out.raw --csv trace.csv` is Setup 1's full
  measurement: a 997 Hz tone through two real crystals into the AES17
  notebook.

Two refusals keep the instrument honest. The bridge opens `hw:` devices
only, and turns `default` or `plughw:` away before touching the hardware: a
plugin device may resample (on a Pi, `default` is PipeWire), and a bridge
measuring PipeWire's converter would report that converter's clock with a
straight face. And `--rt`, which runs both audio threads `SCHED_FIFO` with
memory locked, fails outright if the system refuses either, rather than
quietly measuring without it.

## `pico2_cyccnt`: buying cycles with instructions

Part II built a performance ratchet on QEMU instruction counts:
deterministic, noise-free, gateable in CI at ±3%. The README's Cortex-M33
table says a 2-second Q15 stereo workload executes 484,146,844
instructions — that number will be identical tomorrow, which is what makes
it a regression gate. But it is a count of *instructions*, and silicon
budgets are spent in *cycles*. An instruction can take one cycle or ten;
memory waits, pipeline stalls, and branch penalties exist in silicon and
not in QEMU's functional model. So every deployment claim derived from the
ratchet — "Q15 mono fits a 150 MHz core with room to spare, stereo is
tight" — has been carrying an asterisk: *instruction counts are not cycle
counts; treat these as budgets pending real-silicon validation.*

`examples/pico2_cyccnt/` is the firmware that removes the asterisk. It is
a standalone flashable UF2 (deliberately *not* part of the root build —
it drags in the whole Pico SDK) that runs the exact steady-state workload
of the icount benchmarks — the same `push(32)`/`pull(32)` duplex loop —
on a real RP2350, timing every block with the Cortex-M33's DWT cycle
counter:

```cpp
bool enable_cycle_counter() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk)
        return false; // implementation without a cycle counter
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return true;
}
```

DWT — the Data Watchpoint and Trace unit — is optional silicon on
M-profile cores, so the firmware checks `NOCYCCNT` at runtime rather than
assuming; `TRCENA` gates the whole trace block and must be set first. The
counter is 32 bits free-running, which wraps in ~28.6 s at 150 MHz — fine,
because the firmware only ever takes per-block unsigned deltas, and
unsigned subtraction across a wrap is exact by the same modular-arithmetic
argument the ring buffer's indices rested on. A thousand warmup iterations
run first (past the Filling state, servo settled), then two thousand
measured blocks, reported as mean, p99, and max — the tail statistics
matter, because the workload runs with interrupts live and USB
housekeeping shows up in the max column.

The output table covers Q15 in both profiles at 1, 2, and 12 channels, plus
float at one channel. The float rows are not there in the hope of good
news; they exist to put a *measured* number on "soft-double accumulation
is the wrong datapath on an FP64-less core" — the QEMU baselines already
price float at roughly 3.8× the Q15 instruction count, and a cycle figure
makes the guidance concrete rather than rhetorical.

The deeper purpose is calibration. In steady state the M33 pipelines cost
1,138 instructions per frame for the stereo Q15 pipeline and 3,326 for the
12-channel one (the difference of 2 s and 4 s workloads; a baseline
divided by its frame count also carries the converter's construction, and
an earlier edition used those larger quotients here). Divide the
firmware's measured cycles-per-frame by
those figures and you get the constant the whole ratchet has been waiting
for: *one QEMU instruction ≈ N RP2350 cycles*. That single ratio converts
every current and future M33 instruction baseline into a real cycle
budget — the ratchet keeps its CI-grade determinism, and hardware
contributes exactly one number, measured once per silicon revision instead
of once per commit.

One scoping note recorded in the README of the harness: the cycled input
buffer is 4,800 frames rather than the icount workload's 12,000, so that
the 12-channel case fits the RP2350's 520 KB SRAM alongside the converter.
Per-block work is unchanged; the deviation is documented because an
unexplained difference between two "identical" workloads is how
calibration constants go quietly wrong.

Two more matches to the ratchet came with the roadmap's A11. The firmware
builds `Release` by default, as the workloads are built, and runs the whole
image from SRAM (the SDK's `copy_to_ram`, the `PICO2_COPY_TO_RAM` option,
on by default): QEMU's memory has no wait states and the RP2350's flash
does, behind a cache, so a ratio measured from flash would fold the cache's
misses into the constant. Turning the option off measures the layout a
deployment ships, and the difference between the two runs is what the cache
costs. And the calibration is no longer one engine's: a second table times
`bridge` in both directions and `rational` at ↑2, ↓2 and 2/3, all Q15
`economy` stereo, per `process()` of 32 input frames and per output frame.
`bridge`'s M33 steady states, its baselines less its construct-only legs,
are 424.9 instructions per output frame going down and 311.5 going up, so
its rows give the same ratio read off a different datapath — the check
that one constant serves the family.

## `pico2_dualcore`: one clock domain per core

The README's platform guidance ends with a suggestion: on Pico-class
parts, stereo `balanced()` wants either the `fast()` profile *or the
RP2350's second core*. `examples/pico2_dualcore/` is that suggestion built
and made falsifiable — the converter's two ends on the two Cortex-M33
cores, one core per clock domain, judging its own run against PASS/FAIL
gates.

- **core0 is the producer.** It pushes 32-frame blocks paced by the
  microsecond timer at `rate × (1 + 200e-6)` — a +200 ppm offset
  synthesized from the shared timebase. This is the simulation trick
  imported onto silicon, and it is what real crystals can never give: an
  offset that is *exactly* +200 ppm, so the converged estimate can be
  asserted within ±5 ppm rather than merely admired. core0 also owns all
  USB telemetry.
- **core1 is the consumer.** It pulls 32-frame blocks at exactly the
  nominal rate and times every `pull()` with DWT.CYCCNT — enabled *on
  core1*, because each RP2350 core has a private DWT behind the same
  fixed address (the 0xE000_0000 private peripheral region is core-local;
  enabling the counter from core0 would start the wrong one). core1 never
  prints: contending on the stdio mutex from the paced core would put USB
  stalls onto the output clock domain.

Is running the two ends on two *cores* even within the library's
contract? The firmware answers this in its opening comment, and the
reasoning belongs in this book: the contract is one producer *agent* and
one consumer *agent* around a lock-free SPSC ring with acquire/release
atomics. It names agents and memory ordering, not `std::thread`. The
RP2350's cores share coherent SRAM with no data caches in front of it, so
C++ atomics behave across cores exactly as they do across threads — two
cores satisfy the contract precisely as two threads do. `push()` stays
core0-only, `pull()` stays core1-only, `status()` is documented
any-thread. The chapter on the ring said the memory-ordering argument was
the proof and the tests merely raised the price of being wrong; here the
same argument, unchanged, carries the design onto a second processor.

Everything else that crosses cores is an explicit block of **32-bit**
atomics, and the width is a load-bearing decision inherited from the
library itself: on the M33, 64-bit `std::atomic` is not lock-free — it
routes through a library lock, which is exactly the failure the library's
own telemetry avoided by keeping its counters 32-bit. The firmware
`static_assert`s the lock-freedom of every cross-core type. The phase
handoff is a single release store of the converter pointer (publishing
every plain write the constructor performed) matched by an acquire load on
core1; the teardown is the mirrored pair through a `consumer_done` flag, so
destroying the converter cannot race core1's last `pull()`.

The consumer's statistics need more than individual atomicity, though: a
printed telemetry line should describe one *instant*, not a mean from this
second next to a max from the last. With 64-bit atomics off the table, the
firmware uses a seqlock — the sequence counter goes odd while the writer
updates, even when it finishes, and the reader retries until the same even
value brackets its whole read:

```cpp
void publish_snapshot(const Snapshot& s) {
    const std::uint32_t q = g.seq.load(std::memory_order_relaxed);
    g.seq.store(q + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    // ... payload stores, all relaxed 32-bit atomics ...
    g.seq.store(q + 2, std::memory_order_release);
}
```

The payload fields are themselves relaxed atomics — no torn reads, no
undefined behavior — so the seqlock adds only mutual coherence, and a
retry costs nothing at a 1 Hz read rate. It is the cheapest possible
answer to "publish five numbers atomically on a core with 32-bit
atomics," and a pattern worth stealing.

### The honest scoping decision

The firmware runs two ~30-second phases. Phase A is Q15 stereo
`balanced()` at 48 kHz — the configuration the README calls tight on one
core, now with the input domain moved off the consumer's core entirely.
Phase B is the 12-channel reference-microphone/AVB shape... at **16 kHz**,
not 48. Its README records why, and the passage is a model of how to scope
a demo honestly:

> Phase B is 16 kHz **by arithmetic, not caution**: in steady state the
> M33 QEMU count puts `pipeline12_q15` at 3,326 insns/frame against a
> 150 MHz / 48 kHz budget of 3,125 cycles/frame — over budget even at one
> instruction per cycle, and `pull()` of a single instance is one consumer
> by contract, so no core assignment can split it across cores.
> Dual-core buys one clock domain per core, not
> more datapath than one core has.

That last sentence is the chapter's most important deployment fact. The
SPSC contract that makes the converter lock-free is also a ceiling: one
consumer agent means the entire per-pull datapath — all twelve channels of
it — executes on whichever core calls `pull()`. A second core removes the
*other* clock domain's work and everything else the application does, and
that is all it removes. At 16 kHz the per-frame budget triples to 9,375
cycles and the 12-channel shape fits; and since measured cycles per block
are rate-independent, phase B still delivers the real-silicon counterpart
of the 12-channel instruction baseline. Nothing was hidden by the rate
change — 16 kHz is that configuration's actual deployment rate (the
next chapter's rate-scaling rules are applied in the phase B config,
`filter_spec` band edges and servo bandwidths scaled by 16/48) — but the
README refuses to let you believe dual-core bought compute it didn't.

Two more of the library's documented rules appear in this firmware as
lived decisions rather than advice. The FIFO setpoint is 144 frames, not
the default 48: the producer core shares its time with USB logging, whose
worst-case writer stall is capped at 2 ms in the build — 96 frames of
consumer progress at 48 kHz — so the setpoint must exceed that excursion
with margin. That is the README latency rule applied to a producer that
also logs. And the pacing schedules compute absolute due times
(`t0 + (b·num)/den` in integer microseconds), so a stall is followed by
catch-up pushes rather than permanent schedule slip — the difference
between jitter the FIFO absorbs and a rate error the servo would chase.

A PASS requires: Locked within 2 s of cold start (6 s for phase B, whose
scaled servo is proportionally slower), every 1 Hz ppm sample after the
settling gate within ±5 of the synthesized +200, and zero underruns,
overruns, *and* resyncs after first lock — overruns and resyncs gate too,
because they are the signature of a consumer that cannot keep up. The
firmware prints per-phase verdicts, an `OVERALL` line, and a sentinel
string, so a future self-hosted CI lane can parse a soak the same way the
QEMU lanes parse emulated runs.

The dual-core README also states its own limit, and it belongs here
verbatim in spirit: both domains are paced from the RP2350's one timer —
that is what makes +200.0 an exact, assertable truth — so this firmware
*cannot* prove the inter-crystal lock that Setups 1 and 2 ultimately want.
It proves the deployment shape: two cores, two clock domains, lock-free
handoff, real cycle headroom.

## `pico2_uac2`: a clock the host does not own

Setup 1 assumes its two devices run on two clocks. A cheap USB dongle often
does not: an adaptive or synchronous endpoint locks its sample clock to the
host's USB frames, and two such dongles share one clock. The servo then
locks, flawlessly, to an offset of zero, and a hair dryer changes nothing.
`examples/pico2_uac2/` is the side that cannot fail that way: a Pico 2 that
enumerates as a USB Audio Class 2.0 device in asynchronous mode, with its
sample clock taken from its own crystal.

The clock is one line of integer arithmetic over the RP2350's microsecond
timer, which counts the 12 MHz crystal:

```cpp
std::uint64_t device_frames(std::uint64_t us) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(us) * 6 * (1'000'000 + kPpm) / 125'000'000);
}
```

That is 48,000 frames a second, times (1 + ppm / 10⁶), and it sets both
directions:

- **Playback.** The firmware drains the endpoint's FIFO at that rate, and
  the feedback endpoint tells the host how fast to send. TinyUSB derives
  the feedback from the FIFO's level and holds it half full, so the host
  follows the crystal.
- **Capture.** Each packet carries the frames the clock produced since the
  last one: 47, 48 or 49 a millisecond. The packet callback runs late by
  whatever the main loop was doing, so its times pass through the
  delay-locked loop JACK and PipeWire use (Adriaensen, 2005) before
  they become a frame count. Modelled over 600 s of packets with up to
  0.4 ms of lateness and a 3 ms stall every ten seconds, every packet
  was 47, 48 or 49 frames. The count ran a constant 58 frames behind the
  clock, the filter's lag at start-up, so the rate is exact.

The `kPpm` in that line is the trick `pico2_dualcore` used, turned
around. There, an offset synthesized from one shared timebase gave an
exact truth but proved nothing about two crystals. Here the crystal is
real and unknown, and `PICO2_UAC2_PPM` adds a *known* offset on top. Two
runs at different settings must differ by exactly that much in the
report's fit, which checks the bench's own arithmetic against an exact
number without knowing either crystal.

## What is measured, and what is not yet

The project's culture is that numbers are measured or absent, so here is
the ledger as it stands:

- **Shipped and measured on real clocks: nothing yet.** All quality and
  performance figures in this book so far come from deterministic
  simulation, host benchmarks, and QEMU instruction counting.
- **Shipped and awaiting hardware:** all four harnesses build — the ALSA
  bridge wherever ALSA exists, the three Pico 2 firmwares as flashable UF2s —
  but `docs/HARDWARE_TESTING.md` says it plainly: *the measured numbers
  await a physical Pico 2*, and the multi-hour dongle soak awaits an
  afternoon with a Pi. The cycles-per-instruction calibration constant,
  the real `%core@48k` figures, the hour-scale zero-discontinuity claim,
  and the thermal-drift trace are all, today, well-instrumented empty
  columns.
- **Not yet written:** the script that runs the notebook analysis over a
  `--dump` capture (the `--csv` report, `alsa_bridge_report.py`, shipped
  with the roadmap's A11), and both Setup 3
  programs (UDP sender, receiver-with-ASRC — the plan is to reuse the
  bridge's output half).

A book that inherited this project's habits could not end the chapter any
other way. The harnesses are the falsifiable form of the library's
deployment claims; until a board runs them, the claims stay labeled as
budgets.

## Verify it yourself

```sh
# No hardware: two OS threads 500 ppm apart, lock and estimate on live
# (jittery) scheduling — the software rehearsal of the bridge:
cmake -B build -DTAP_SR_BUILD_EXAMPLES=ON && cmake --build build -j
./build/async/examples/drifting_clocks

# Setup 1 (Linux + two audio devices; tap_sr_async_alsa_bridge builds when ALSA
# is found). Real clocks, synthetic tone, telemetry + capture:
./build/async/examples/tap_sr_async_alsa_bridge --in hw:1,0 --out hw:2,0 \
    --rt --tone 997 --csv trace.csv --dump post_asrc.f32 --seconds 3600
# Then: the crystals' measured offset against the servo, the wobble and the
# breathing (--self-test checks the arithmetic without hardware):
python3 async/examples/alsa_bridge_report.py trace.csv --plot trace.png
# and analyze post_asrc.f32 with the AES17 machinery in
# notebooks/asrc_comparison.ipynb.

# Setup 2 firmware (standalone builds; arm-none-eabi-gcc + network for
# the Pico SDK fetch):
cd examples/pico2_cyccnt   && cmake -B build -DPICO_BOARD=pico2 && cmake --build build -j
cd examples/pico2_dualcore && cmake -B build -DPICO_BOARD=pico2 && cmake --build build -j
# Setup 1's independent clock (a UAC2 asynchronous-mode device; status on UART0):
cd examples/pico2_uac2     && cmake -B build -DPICO_BOARD=pico2 && cmake --build build -j
# Flash the UF2s, open the USB serial port, and wait for the sentinel
# lines: TAP_SR_PICO2_DONE / TAP_SR_PICO2_DUALCORE_DONE with per-phase PASS/FAIL.
```

If you have the hardware this project's authors did not have on their
bench, you are holding the most valuable contribution available: run the
soak, and turn the empty columns into numbers.
