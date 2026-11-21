# Interleaved against planar (informational, never a gate)

Every engine's float converter at 1, 2, 6 (5.1) and 12 (7.1.4) channels, two
ways: one `converter<Ch>` over interleaved frames, the engines' API (`_i`), and
`Ch` mono converters over contiguous channel buffers, what a planar host would
otherwise run (`_p`). Workloads in `layout_main.cpp`, one binary per scenario
(`tap_sr_layout_<engine>_ch<C>_<i|p>`, each including and linking one engine
only), built with `TAP_SR_BUILD_ICOUNT_BENCH`. The configurations are the host
ratchet's: async push/pull 1:1 at a two-block setpoint, bridge 44.1 -> 48 kHz,
rational up2, float, 128-frame blocks, 1 s of audio.

`scripts/layout.py --target {host,m33,m55,hexagon}` counts only
`tap_sr_layout_block()`, one block through every channel, summed over the run
(callgrind's toggle on the host in `icount.py`'s pinned environment; the call
markers' `total=` under QEMU), so construction, which the planar layout pays Ch
times, is outside the count. It prints each count and, per pair, the planar /
interleaved ratio and whether the outputs are bit-identical. CI runs it after
each ratchet (`ci.yml`, the `Layout counts` steps; artifacts `layout-<target>`),
`continue-on-error`, and it gates nothing: the maintainer's decision
(2026-11-21), the same as DspTap's `bench/layout`.

Async planar is measured, not endorsed: each mono converter runs its own clock
servo, which a deployment must not do (async's C ABI refuses that fan-out);
driven identically here, they stay in step and the outputs match bit for bit.

Host, x86-64 (GCC 13.3 `-O3`, valgrind 3.22, the pinned glibc dispatch;
2026-11-21, before the first CI run):

| pair | 2 ch i | 2 ch p | p / i | 6 ch i | 6 ch p | p / i | 12 ch i | 12 ch p | p / i |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| async | 52,953,572 | 67,248,781 | 1.270 | 107,986,550 | 201,195,762 | 1.863 | 176,074,748 | 402,382,524 | 2.285 |
| bridge | 43,246,713 | 44,969,562 | 1.040 | 127,737,465 | 134,913,561 | 1.056 | 252,581,211 | 269,819,997 | 1.068 |
| rational | 24,421,446 | 27,477,588 | 1.125 | 66,488,151 | 82,437,639 | 1.240 | 128,191,290 | 164,868,153 | 1.286 |

Per target, planar / interleaved (CI run 413, workflow_dispatch on this
branch, 2026-11-21; every pair's outputs bit-identical; `hexagon` counts
packets, as its ratchet does; the M55 row follows when that job finishes):

| target | async 2 | async 6 | async 12 | bridge 2 | bridge 6 | bridge 12 | rational 2 | rational 6 | rational 12 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| host (x86-64) | 1.270 | 1.863 | 2.285 | 1.040 | 1.056 | 1.068 | 1.125 | 1.240 | 1.286 |
| m33 | 1.002 | 1.024 | 1.030 | 1.002 | 1.003 | 1.005 | 1.007 | 1.014 | 1.015 |
| hexagon | 1.078 | 1.115 | 1.126 | 1.011 | 1.018 | 1.020 | 1.032 | 1.061 | 1.067 |

Interleaved is never the more expensive layout, on any target. On the host
async gains the most: its per-frame blend is shared by every channel, and
from four channels it puts the channels in SIMD lanes (the book's Part III,
C6), which no mono converter can; rational shares its per-frame polyphase
bookkeeping; bridge, almost all filter dot products, differs by 4 to 7 %.
On the M33, where the dots dominate and C6's path is compiled out, the two
layouts are within 3 % of each other; Hexagon sits between.
