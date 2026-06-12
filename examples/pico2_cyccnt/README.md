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
firmware closes that loop: dividing its measured cycles/frame by the
committed M33 instruction baselines (`pipeline_q15` 484,146,844 insns per
96,000 frames = 5,043/frame; `pipeline12_q15` 962,613,655 = 10,027/frame)
calibrates the "1 QEMU instruction ≈ N RP2350 cycles" ratio, turning every
current and future M33 baseline into a real cycle budget. It also tests the
README's claim directly: Q15 mono fits a 150 MHz core with room to spare,
stereo is tighter.

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
float) case that cannot allocate prints a `SKIP` row instead. The run ends
with:

```
SRT_PICO2_DONE
```

## Reading the numbers

- **cyc/frame ÷ 5,043** (Q15 balanced 2ch) and **÷ 10,027** (12ch) give the
  silicon cycles-per-QEMU-instruction ratio — the calibration constant for
  all M33 instruction baselines in the README table.
- **%core@48k** is the headline budget figure. The float rows exist to put a
  measured number on "soft-double accumulation is the wrong datapath here";
  Q15/Q31 are the intended paths on FP64-less cores.
- p99/max vs. mean shows the (small) jitter from FIFO compaction, servo
  block-rate work and whole-sample slips; the workload runs from SRAM with
  interrupts live, so USB housekeeping shows up in the tail.

Deviation from the icount workload: the cycled input buffer is 4,800 frames
(0.1 s) instead of 12,000 so the 12-channel case fits comfortably in the
RP2350's 520 KB SRAM; per-block work is unchanged.
