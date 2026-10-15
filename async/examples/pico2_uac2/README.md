# pico2_uac2 — the bench's independent clock

A Raspberry Pi Pico 2 (RP2350) as a USB Audio Class 2.0 device in
asynchronous mode, clocked by its own crystal. It is the clock the hardware
bench needs and a cheap dongle often is not: many USB audio adapters are
adaptive or synchronous endpoints locked to the host's USB frame clock, and
two of them share one effective clock (the family roadmap's B0). This one
shares nothing with the host but the cable. The roadmap's Q1 chose it as the
bench's asynchronous-mode device, and A11 is the item that wrote it.

## What it is

- **One fixed format:** 48 kHz, stereo, 16-bit, at USB Full Speed. A
  playback path (the host plays to it) and a capture path (the host records
  from it), each on an **asynchronous** isochronous endpoint.
- **The device's clock** is the RP2350's 1 MHz timer, which counts the
  12 MHz crystal through the tick generator. At t µs into a stream the
  device has produced or consumed
  `floor(t · 6 · (10⁶ + ppm) / (125 · 10⁶))` frames: 48,000 a second
  times (1 + ppm / 10⁶), in exact integer arithmetic.
- **Playback** drains the endpoint's FIFO at that clock. Its explicit
  feedback endpoint (USB 2.0, 5.12.4.2) tells the host the rate. TinyUSB
  computes the feedback from the FIFO's level, held half full (its
  `AUDIO_FEEDBACK_METHOD_FIFO_COUNT`), so the host follows the crystal.
- **Capture** sends, each millisecond, exactly the frames the clock
  produced since the last packet: 47, 48 or 49, averaging the device's
  rate.
  - The packet callback runs in the main loop, up to a few hundred µs late,
    so its times pass through a second-order delay-locked loop first
    (F. Adriaensen, *Using a DLL to filter time*, LAC 2005; 1 Hz
    bandwidth).
  - The signal is a stereo tone, indexed by device frame: 997 Hz left,
    441 Hz right, −6 dBFS.
- **Status:** core 1 prints a line a second on UART0 (GP0/GP1,
  115200 8N1). Core 0 only queues the numbers, so a serial line never
  stalls the USB loop. The USB port is the audio device, so there is no USB
  serial.

## Build

Standalone, like the other Pico firmwares (not part of the root build).
Needs `cmake` ≥ 3.24, `arm-none-eabi-gcc` (tested with 13.2.1) and network
access on first configure, which fetches the Pico SDK 2.1.1 with its
TinyUSB 0.18.0.

```sh
cd async/examples/pico2_uac2
cmake -B build -DPICO_BOARD=pico2
cmake --build build -j
```

This produces `build/pico2_uac2.uf2`; flash it as `pico2_cyccnt`'s README
says. One option:

- `-DPICO2_UAC2_PPM=<n>` offsets the device's clock by n ppm on top of the
  crystal's own error. It must be an integer in −1000..1000, `async`'s
  charter; the default is 0.

## On the bench

1. Plug it into the Pi and find it with `aplay -l` / `arecord -l`. The
   product string is *SampleRateTap Pico 2 UAC2*.
2. **Check that it is asynchronous** (B0): `/proc/asound/cardN/stream0`
   should report both endpoints `ASYNC`, and the playback endpoint's
   feedback.
3. **Run the bridge** with the Pico as one side and another device as the
   other:
   - `--in hw:<pico>` records its tone;
   - `--out hw:<pico>` plays into it.

   **Never use the Pico as both sides:** both directions run on one
   crystal, and the bridge would measure a clock against itself.
4. **Read the UART** for the device's own account, a line a second:

   ```
   TAP_SR_UAC2 t=… out=on frames=… fifo=…..… underruns=0 (0 frames) peak=-6.0 dBFS in=on frames=… skipped=0 resyncs=0
   ```

   - `fifo` is the playback FIFO's range over that second, which the
     feedback holds near its middle (196 of 392 frames).
   - `underruns`: the FIFO ran dry and playback re-primed.
   - `skipped`: capture frames dropped because the host stopped polling for
     more than three packets.
   - `resyncs`: the DLL lost the frame clock and restarted.
   - On a healthy run, all three stay 0.
5. **Check the bench against a known offset.** The crystal's own error is
   unknown, but the difference between two runs is not. Run once at
   `PICO2_UAC2_PPM=0` and once at, say, `+200`. The offsets that
   `alsa_bridge_report.py` fits for the two runs should differ by 200 ppm,
   within each fit's uncertainty, less whatever the crystal drifted with
   temperature in between.

## What has been checked, and what has not

- **Built** with arm-none-eabi-gcc 13.2.1 against the Pico SDK 2.1.1 at
  `-Wall -Wextra` (`-Wconversion` for C++), with no warning, at
  `PICO2_UAC2_PPM` 0 and −250. CI's Pico 2 job builds it on every PR.
- **The configuration descriptor** was dumped from the ELF and walked, one
  descriptor at a time:
  - its length is the declared 218 bytes;
  - the IAD groups the three interfaces;
  - the clock is entity 16, the terminals 1→2 (playback) and 3→4 (capture);
  - both data endpoints are isochronous, asynchronous, 196 bytes;
  - the feedback endpoint is 0x81, 4 bytes, every frame.
- **The capture schedule** was modelled with the firmware's arithmetic over
  600 s of packets, with up to 0.4 ms of callback lateness (2 ms in one
  case), a 3 ms stall every 10 s, host offsets of 0 to +50 ppm and device
  offsets of ±1000 ppm.
  - Every packet was 47, 48 or 49 frames, with none skipped and no resync.
  - The count stayed a constant 58 frames (the filter's lag at start-up)
    behind the device's clock at 300, 600 and 1200 s, so the rate is
    exact.
- **Not checked:** it has never been enumerated by a host. Linux's
  acceptance of the descriptors, the feedback's convergence on a real bus,
  and the UART lines are Block B's first minutes.

## Limits

- **48 kHz, 16-bit, stereo only.** The clock reports one fixed rate, and a
  request for any other rate stalls.
- **Feedback is 16.16 in 4 bytes**, which Linux and Windows read at Full
  Speed. macOS wants 10.14 in 3 bytes at Full Speed, which this firmware
  does not serve; the bench is a Pi.
- **The vendor ID is 0xCafe**, TinyUSB's test ID: fine on a bench, never for
  a device that ships.
- **Capture is a tone, not an input.** The firmware has no ADC; a run that
  needs real audio uses another device as the capture side.
- **One stream per direction.** Interrupting a stream (alternate setting 0)
  restarts that direction's clock origin. The device's clock runs on; only
  the count restarts.
