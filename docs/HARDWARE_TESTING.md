# Hardware test setups

The test suite simulates two drifting clock domains deterministically; what
it cannot provide is two *genuinely independent* crystal oscillators. Real
hardware can, cheaply: every USB audio dongle, Raspberry Pi, and Pico has
its own crystal, typically 20–200 ppm apart and drifting with temperature —
exactly the regime this library is designed for. Three setups, in
increasing order of effort, all built from commodity parts.

## Setup 1 — one Cortex-A Pi, two USB audio dongles

*The canonical real-world test. One Pi 4/5 plus two of the cheapest USB
audio adapters available (~$15).*

Each dongle clocks its own 48 kHz from its own crystal:

- **Capture thread**: ALSA reads from dongle A, paced by A's clock →
  `push()`
- **Playback thread**: ALSA writes to dongle B, paced by B's clock →
  `pull()`
- A loopback wire (or a tone generator into A's input) provides signal.

This is the library's use case running on real clocks. Log `status()` once
per second and check:

1. **The ppm estimate converges to a stable, repeatable number** — the
   actual offset between the two dongles' crystals. Verify it
   independently: count frames delivered by each device against
   `CLOCK_MONOTONIC` for ten minutes; the ratio of the two measured rates
   should match the servo's estimate to well under 1 ppm.
2. **Zero underruns/overruns over hours.** A multi-hour soak is the test
   no simulation honestly replaces — slow temperature drift, USB
   scheduling jitter, the works.
3. **Thermal drift tracking**: point a hair dryer at one dongle (or touch
   its crystal). Crystals move several ppm with temperature; the ppm
   estimate should track the drift in real time with nothing audible —
   the Quiet-stage servo doing its job. A fast ±50 ppm step should
   demote the servo stage and re-promote after re-lock.

For quality numbers, do not trust the analog path of cheap dongles
(−80 dB-ish). Instead have the playback thread also write the post-ASRC
stream to a file and analyze it offline with the notebook tooling
(`notebooks/asrc_comparison.ipynb` has the AES17-style measurement
machinery) — the clocks are real even if the signal never goes analog.

## Setup 2 — Pi (Cortex-A) + Raspberry Pi Pico 2: the M33 target on real silicon

*Validates the QEMU-derived Cortex-M33 numbers on an actual RP2350.*

- The Pi streams a known signal (997 Hz sine) to the Pico 2 over USB CDC
  or UART, paced by the Pi's clock.
- The Pico runs the Q15 path and outputs via I2S (PIO) or PWM, paced by
  the RP2350's crystal.
- The two ends are genuinely asynchronous; the ASRC on the M33
  reconciles them.

Two things this proves that emulation cannot:

- **The cycle budget.** [PERFORMANCE.md](PERFORMANCE.md) notes that QEMU
  gives deterministic *instruction* counts, not cycles, and real cycles
  need hardware counters. The RP2350 has DWT.CYCCNT: wrapping `pull()`
  in CYCCNT reads gives real cycles-per-block at 150 MHz — directly
  testing the README's claim that Q15 mono fits comfortably and stereo
  is tight on one core. Correlating CYCCNT against the QEMU instruction
  baselines also calibrates the ratchet ("1 QEMU instruction ≈ N RP2350
  cycles") for all future M33 numbers.
- **Dual-core deployment.** The README suggests dedicating the RP2350's
  second core to stereo; an actual core1-runs-ASRC build verifies that
  guidance.

## Setup 3 — two Pis over Ethernet

*The network-audio (Snapcast/AoIP-style) case.*

A sender Pi captures or generates audio paced by its sound card and ships
raw frames over UDP; the receiver Pi runs the ASRC in front of its own
output device. The clock mismatch is receiver-sound-card vs.
sender-sound-card, and `push()` sees bursty, jittery network delivery
rather than smooth callback-paced blocks — good for validating the FIFO
setpoint guidance (`targetLatencyFrames` must exceed the peak occupancy
excursion of the arrival jitter) under real network conditions.

## Suggested order

Start with Setup 1: it is an afternoon of work, needs no firmware, and
produces the headline result ("locked to the real inter-crystal offset of
X ppm, N hours, zero discontinuities"). Then Setup 2, because
real-silicon CYCCNT numbers close the loop on everything the M33
emulation work predicted.

The code each setup needs:

- **Setup 1**: an ALSA duplex bridge example (two threads around
  `push()`/`pull()`, telemetry logging to CSV, optional post-ASRC capture
  to disk) plus a script to plot the ppm trace and analyze the captured
  stream.
- **Setup 2**: a small Pico SDK firmware project wrapping the header-only
  library — the M33 toolchain support already proves the code compiles
  for that core (`cmake/arm-cortex-m33-mps2.cmake` shows the required
  flags: `-mcpu=cortex-m33 -mthumb -mfloat-abi=hard`).
- **Setup 3**: two small programs (UDP sender, receiver-with-ASRC) reusing
  the Setup 1 bridge's output half.
