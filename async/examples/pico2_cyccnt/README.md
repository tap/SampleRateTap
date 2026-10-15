# pico2_cyccnt — RP2350 cycle measurement firmware

The DWT.CYCCNT deliverable of [docs/HARDWARE_TESTING.md](../../docs/HARDWARE_TESTING.md)
Setup 2: runs the library's fixed pipeline workload (the same steady-state
`push(32)`/`pull(32)` loop as `bench/icount/icount_main.cpp`) on a real
Raspberry Pi Pico 2 and times each 32-frame block with the Cortex-M33's
hardware cycle counter.

## Why

[docs/PERFORMANCE.md](../../docs/PERFORMANCE.md) gates regressions on QEMU
*instruction* counts because they are deterministic and noise-free — but
silicon budgets are spent in *cycles*, and QEMU cannot provide those. This
firmware closes that loop: dividing its measured cycles/frame by the M33
steady-state instruction counts (`pipeline_q15` 1,138/frame,
`pipeline12_q15` 3,326/frame) calibrates the "1 QEMU instruction ≈ N RP2350
cycles" ratio, turning every current and future M33 baseline into a real
cycle budget. The steady state is the difference of the workload built at
2 s and at 4 s (`-DTAP_SR_ASYNC_SC_SECONDS=4`); a committed baseline divided by its
96,000 frames also carries the converter's one-time construction (~0.9 G
instructions of soft-double filter design), which this firmware does not
time. It also tests the README's claim directly: Q15 stereo fits a 150 MHz
core with room to spare, 12 channels at 48 kHz does not.

The family's two synchronous engines are measured beside `async` (the
family roadmap's A11): `bridge` in both directions (48 → 44.1 and
44.1 → 48 kHz) and `rational` up 2, down 2 and 2/3, all Q15 at the default
profile (`economy`), stereo, each `process()` of 32 input frames timed. Their
QEMU M33 steady states, from `bridge/bench/baselines.json` less the
construct-only legs (2 s of stereo), are 424.9 instructions per output frame
down ((173,755,176 − 136,279,322) / 88,200) and 311.5 up
((133,914,472 − 104,011,630) / 96,000): the firmware's cycles per output
frame divided by those is the same calibration ratio, read off a second
datapath. `rational`'s construct leg is its chain's (`down2_down2_q15_eco`),
so its single-stage totals still carry their own construction, and the
firmware's cycles are the measurement there.

## Build

This is a standalone project — it is *not* part of the root CMake build.
Requires `cmake` ≥ 3.24, `arm-none-eabi-gcc` (tested with 13.2), and network
access on first configure (fetches the Pico SDK 2.1.1 plus its TinyUSB
submodule; several minutes and a native compiler for the SDK's `picotool`
build).

```sh
cd examples/pico2_cyccnt
cmake -B build -DPICO_BOARD=pico2
cmake --build build -j
```

Produces `build/pico2_cyccnt.uf2`. Options:

- `-DPICO2_MEASURE_FLOAT=OFF` — skip the float (soft FP64) cases.
- `-DPICO2_COPY_TO_RAM=OFF` — execute in place from flash (the SDK's default
  layout, what a deployment would ship) instead of the default here, the
  SDK's `copy_to_ram` binary type: the whole image copied to SRAM at boot,
  as the ratchet's QEMU M33 runs from zero-wait-state memory, so the cycles
  measure the code and not the flash's XIP cache. Running both is the
  measure of what the cache costs.

The build type defaults to `Release` (`-O3`), as the ratchet's workloads are
built. The one flag that differs is the float ABI: the SDK compiles
`-mfloat-abi=softfp`, the ratchet's toolchain file `hard`; the Q15 cases
pass no floating-point arguments in their hot loops, so it moves only the
float rows.

## Flash

Either hold BOOTSEL while plugging the Pico 2 in and copy the UF2 onto the
`RP2350` mass-storage drive:

```sh
cp build/pico2_cyccnt.uf2 /media/$USER/RP2350/
```

or use picotool (no BOOTSEL dance needed if the firmware is already running):

```sh
picotool load -f build/pico2_cyccnt.uf2
picotool reboot
```

## Run

Open the USB CDC serial port; the firmware waits for a terminal before
printing anything, so nothing is lost:

```sh
picocom /dev/ttyACM0   # or: minicom -D /dev/ttyACM0
```

Expected output: a header with the sys clock (150 MHz default), then one row
per case — Q15 × {fast, balanced} × {1, 2, 12} channels, plus float 1ch —
with mean/p99/max cycles per 32-frame block, derived cycles/frame, and the
percentage of a 150 MHz core that one 48 kHz stream costs. A 12-channel (or
float) case that cannot allocate prints a `SKIP` row instead. A second table
follows for the synchronous engines — `bridge` 48>44.1 and 44.1>48,
`rational` up2, down2 and 2/3, Q15 economy stereo — with mean/p99/max cycles
per `process()` call, cycles per *output* frame, and one stream's share of
the core at the engine's output rate (stated on the row). The run ends
with:

```
TAP_SR_PICO2_DONE
```

## Reading the numbers

- **cyc/frame ÷ 1,138** (Q15 balanced 2ch) and **÷ 3,326** (12ch) give the
  silicon cycles-per-QEMU-instruction ratio — the calibration constant for
  all M33 instruction baselines in the README table.
- **cyc/out ÷ 424.9** (`bridge` 48>44.1) and **÷ 311.5** (44.1>48) read the
  same ratio off the synchronous datapath; the two ratios agreeing is the
  check that one constant serves every engine.
- **%core@48k** is the headline budget figure. The float rows exist to put a
  measured number on "soft-double accumulation is the wrong datapath here";
  Q15/Q31 are the intended paths on FP64-less cores.
- p99/max vs. mean shows the (small) jitter from FIFO compaction, servo
  block-rate work and whole-sample slips; the workload runs with interrupts
  live, so USB housekeeping shows up in the tail.

Deviation from the icount workload: the cycled input buffer is 4,800 frames
(0.1 s) instead of 12,000 so the 12-channel case fits comfortably in the
RP2350's 520 KB SRAM; per-block work is unchanged. Under `copy_to_ram` the
image's code shares that SRAM too: 140,712 B of `.text` and 12,224 B of
`.bss` (arm-none-eabi-gcc 13.2.1, Release, exceptions on; 131,908 B of
`.text` without exceptions), which leaves the 12-channel case its room.
