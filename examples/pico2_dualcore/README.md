# pico2_dualcore — RP2350 dual-core deployment firmware

The dual-core deliverable of [docs/HARDWARE_TESTING.md](../../docs/HARDWARE_TESTING.md)
Setup 2: the converter's two ends run on the RP2350's two Cortex-M33 cores,
one core per clock domain, and the firmware judges its own run — servo lock,
ppm convergence against a synthesized truth value, clean counters, and
measured `pull()` cycles — against PASS/FAIL gates.

## The deployment shape it validates

The README's platform guidance says that on Pico-class parts the Q15/Q31
paths are the right datapaths, 48 kHz mono fits a 150 MHz core with room to
spare, and stereo `balanced()` wants the `fast()` preset *or the RP2350's
second core*. This firmware is that second-core deployment, built the way a
real one is:

- **core0 = producer (input clock domain).** Pushes 32-frame blocks of a
  997 Hz sine, busy-paced on the microsecond timer at
  `rate × (1 + 200e-6)` — a +200 ppm clock offset synthesized from the
  shared timebase, so the servo's converged estimate has an exact truth
  value to be judged against (the one thing two real crystals cannot give
  you). core0 also owns USB telemetry, printing once per second.
- **core1 = consumer (output clock domain).** Pulls 32-frame blocks paced
  at exactly the nominal rate and times every `pull()` with its own
  DWT.CYCCNT (each RP2350 core has a private DWT — the 0xE000_0000 PPB
  region is per-core, so the counter must be enabled *on* core1).
- The library's `push()`/`pull()` contract is one producer agent and one
  consumer agent around a lock-free SPSC ring with acquire/release atomics —
  the contract names agents, not threads, so two cores sharing the RP2350's
  coherent SRAM satisfy it exactly as two threads do. Everything else that
  crosses cores is an explicit block of 32-bit atomics (32-bit because
  64-bit `std::atomic` is not lock-free on the M33 — the same constraint
  that shapes the library's own telemetry).

Two phases, ~30 s each:

| Phase | config | Rates | Why |
|---|---|---|---|
| A | Q15 stereo `balanced()` | 48 kHz out, +200 ppm in | the config the README calls tight on one core |
| B | Q15 12-channel, `balanced()` band edges and servo scaled ×16/48 | 16 kHz out, +200 ppm in | the reference-microphone/AVB 12-channel shape at its deployment rate |

Phase B is 16 kHz **by arithmetic, not caution**: the M33 QEMU baseline puts
`pipeline12_q15` at 10,027 insns/frame against a 150 MHz / 48 kHz budget of
3,125 cycles/frame — more than 3× over, and `pull()` of a single instance is
one consumer by contract, so no core assignment can split it across cores.
Dual-core buys one clock domain per core, not more datapath than one core
has. At 16 kHz the budget is 9,375 cycles/frame. The measured cycles/block
is rate-independent, so phase B still produces the real-silicon counterpart
of the 12-channel baseline.

## Build

Standalone project — *not* part of the root CMake build. Requires
`cmake` ≥ 3.24, `arm-none-eabi-gcc` (tested with 13.2), and network access
on first configure (fetches Pico SDK 2.1.1 plus its TinyUSB submodule;
several minutes, and a native compiler for the SDK's picotool build).

```sh
cd examples/pico2_dualcore
cmake -B build -DPICO_BOARD=pico2
cmake --build build -j
```

Produces `build/pico2_dualcore.uf2`.

## Flash and run

Hold BOOTSEL while plugging in and copy the UF2 onto the `RP2350` drive, or
use picotool:

```sh
cp build/pico2_dualcore.uf2 /media/$USER/RP2350/   # or:
picotool load -f build/pico2_dualcore.uf2 && picotool reboot
```

Open the USB serial port (`picocom /dev/ttyACM0`); the firmware waits for a
terminal before starting, so nothing is lost.

## Expected output

A header, then one telemetry line per second per phase:

```
[A t= 9s] Locked    ppm=+200.05 fill= 144.2 und=0 ovr=0 rsy=0 | pull/blk mean=... p99=... max=... (..% core) late<=..us
```

and per phase a verdict line:

```
SUMMARY A q15 2ch balanced @48000: PASS lock_ms=... ppm_final=+200.0 post_lock_und=0 ovr=0 rsy=0 pull_cyc_blk mean=... p99=... max=... cyc_frame=... core_pct=... late_max_us=...
```

PASS requires: Locked within 2 s (phase A; 6 s for B, whose servo is scaled
3× slower), every 1 Hz ppm sample after 10 s (A) / 15 s (B) within ±5 of
+200, and zero underruns/overruns/resyncs after first lock. The run ends
with an `OVERALL` line and:

```
SRT_PICO2_DUALCORE_DONE
```

## Reading the numbers

- **core_pct** is the headroom figure: one stream's share of core1 at the
  reported sys clock, `cyc_frame × rate / 150 MHz`. It prices `pull()` only
  — by design, since `push()` is a ring write and the producer core's real
  budget goes to whatever feeds it (here: telemetry).
- **Relation to the QEMU baselines** (`bench/baselines.json`, 2 s = 96,000
  frames per workload): `pipeline_q15` 484,146,844 insns = **5,043
  insns/frame**, `pipeline12_q15` 962,613,655 = **10,027 insns/frame**.
  Those figures amortize one-time setup (soft-double Kaiser design, input
  synthesis) over the workload, so they are upper bounds for the
  steady-state loop this firmware times; `cyc_frame ÷ insns/frame` from the
  sibling `examples/pico2_cyccnt` run gives the cycles-per-instruction
  calibration that converts every M33 baseline into a cycle budget.
- Against the budgets: 3,125 cycles/frame buys one 48 kHz frame at
  150 MHz, 9,375 one 16 kHz frame. Phase A's `core_pct` is the measured
  version of "stereo balanced() is tight on one core": whatever it reads,
  that is the share of core1 a deployment must reserve — and on a
  *single*-core deployment the same cycles would contend with the producer
  side and the rest of the application, which is exactly why the input
  domain lives on core0 here.
- **late_max_us** is the consumer's worst schedule slip. If `pull()` ever
  exceeded the block period, lateness shows here long before the FIFO
  counters do.
- The FIFO setpoint is 144 frames (3 ms at 48 kHz) rather than the default
  48: the producer core shares its time with USB logging, whose worst-case
  writer stall is capped at 2 ms in the CMakeLists. The README latency rule
  — the setpoint must exceed the peak occupancy excursion of push/pull
  jitter — applied to a producer that also logs.

This firmware cannot prove the inter-crystal lock that
HARDWARE_TESTING.md Setup 1/2 ultimately want (both domains here are paced
from the RP2350's one timer, which is what makes ppm = +200.0 an exact,
assertable truth); it proves the *deployment shape*: two cores, two clock
domains, lock-free handoff, and real cycle headroom numbers.
