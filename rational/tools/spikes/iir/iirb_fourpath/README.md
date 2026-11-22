# IIR-b: the four-path stage against the two half-bands it would replace (I12)

[`docs/IIR_PLAN.md`](../../../../../docs/IIR_PLAN.md) I12, as the maintainer settled it for IIR-b:
the nonlinear four-path ↓4 / ↑4 ships only where it beats the half-band pair it replaces by more
than R3's 10 % in instructions, **measured on the shipping code**, on the M33, the M55 and Hexagon,
at the ×8 top and the ×16 middle, at the three 70 dB profiles. IIR-0 had measured 13 to 32 % fewer
instructions there with its own kernels. This directory is the measurement; nothing here ships, and
nothing here is in a build CI runs.

**Verdict: it does not ship.** On the shipping stage machine over DspTap 0.3.0's kernels
(`iir_kernels.h`) the four-path stage beats the pair by more than 10 % on Hexagon going down and in
Q15 going up, on the M33 in Q15 going up and on the M55 in Q15 going down (−10.1 %), but loses on the M33 and the M55 in float going down
(+10.7 % and +9.2 %) and in Q15 going down on the M33 (+14.6 %). No format, direction and placement
clears the bar on all three targets, so every cascade (`iir_chain.h`) is half-bands and
`basic_iir_stage` runs by 2 only.

## What was measured

- **The code.** [`include/tap/sr/rational/iir_stage.h`](include/tap/sr/rational/iir_stage.h) is
  IIR-b's draft of the shipping header as it was measured: `basic_iir_stage` with the four-path stage
  by 4 (`up4` / `down4`, nonlinear, DspTap's committed `k_fourpath` designs at divisor 2, laid out
  [2, 2, 1, 1] over the paths) beside the two half-band variants. Its by-2 machine is the one that
  ships; the by-4 one was removed when it failed the bar.
- **The workloads** ([`fourpath_main.cpp`](fourpath_main.cpp)): `icount_main.cpp`'s shape, 2 s of
  stereo at 48 kHz in 32-frame blocks through a `tap::dsp::chain`, float and Q15, at
  `super_economy`, `economy` and `balanced`:
  - the replaced part: ↓2 at divisor 4 then ↓2 at divisor 2 (the pair), against ↓4 at divisor 2;
    and the mirror going up;
  - the placements in their cascades: by 8 (half-bands at 4, 2, 1 against ↓4 at 2 then ↓2 at 1) and
    by 16 (half-bands at 8, 4, 2, 1 against ↓2 at 8, ↓4 at 2, ↓2 at 1), both directions. Both
    placements replace the same pair at the same divisors.
- **The counts**: `scripts/icount.py` and the QEMU plugin, as the ratchet counts (M33 and M55 under
  `qemu-system-arm` 8.2.2 with arm-none-eabi-gcc 13.2.1, Hexagon under the plugin-enabled
  `qemu-hexagon` with the CodeLinaro clang 19.1.5), the exceptions build, Release. The streaming
  count is each workload's total minus the `construct` workload's (construction and the fixture,
  whose sine is soft double on the M33). Every count is in [`outputs/`](outputs/).

## The numbers (`economy`; `super_economy` and `balanced` within 0.01 % of them)

Streaming instructions, and the four-path stage's change against the pair (negative: fewer):

| target | format | pair ↓ | four-path ↓ | change | pair ↑ | four-path ↑ | change | in ×8 ↓ | in ×16 ↓ | in ×8 ↑ | in ×16 ↑ |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| M33 | float | 8,882,175 | 9,833,646 | +10.7 % | 30,789,208 | 31,443,201 | +2.1 % | +8.9 % | +4.2 % | +1.9 % | +1.1 % |
| M33 | Q15 | 15,715,584 | 18,008,392 | +14.6 % | 65,805,549 | 56,112,381 | −14.7 % | +12.1 % | +6.0 % | −11.9 % | −6.9 % |
| M55 | float | 9,141,470 | 9,979,219 | +9.2 % | 33,418,222 | 33,025,597 | −1.2 % | +7.5 % | +3.6 % | −0.8 % | −0.5 % |
| M55 | Q15 | 17,004,366 | 15,293,048 | −10.1 % | 70,620,676 | 67,112,311 | −5.0 % | −8.3 % | −4.9 % | −3.9 % | −2.3 % |
| Hexagon | float | 6,393,195 | 5,148,438 | −19.5 % | 17,855,818 | 16,472,394 | −7.7 % | −16.2 % | −9.4 % | −6.4 % | −3.8 % |
| Hexagon | Q15 | 8,516,006 | 6,466,444 | −24.1 % | 27,984,577 | 24,983,509 | −10.7 % | −19.7 % | −11.6 % | −8.5 % | −4.9 % |

(Hexagon counts packets, as its ratchet key does.) IIR-0's spike kernels, for comparison: −23 / −21 /
−32 % in float and −13 / −17 / −22 % in Q15 on the M33 / M55 / Hexagon, going down at `economy`.

## Why the margin moved

The six sections cost the same multiplies either way (a tie, IIR-0's 2.4). What decides the count is
the overhead around them, and DspTap's kernels are generic over the section counts: each path is a
loop over a run-time size, and `fourpath_decimate` builds its four path outputs in an array, so on
the in-order Arm cores the four-path stage pays four path set-ups and the array's traffic per
output for one or two sections each, where IIR-0's kernels had the counts as compile-time facts and
unrolled them. Hexagon's packets absorb that overhead, so its margin survives (−19.5 % and −24.1 %
going down). A four-path stage that passed on the Arm targets would need a kernel specialized to its
[2, 2, 1, 1] layout in DspTap; the plan's bar is met by neither the replaced part nor the cascades
on all three targets with today's.

## Reproducing

From the repository root, with the plugin built as `ci.yml`'s ratchet job builds it, for each of
`m33`, `m55` and `hexagon` (Hexagon also needs the toolchain and `qemu-hexagon` on `PATH`):

```sh
cmake -S rational/tools/spikes/iir/iirb_fourpath -B build-fp-m33 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake
cmake --build build-fp-m33 -j 4
python3 scripts/icount.py --engine rational --target m33 --build-dir build-fp-m33 \
        --plugin /tmp/libinsncount.so --baselines m33.json --update
```

then subtract each `construct_<format>_<profile>` count from its siblings.
