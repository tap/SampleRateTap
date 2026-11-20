## 1. Zero-input limit cycles, exhaustive per section

Sections searched: every one-multiplier coefficient (nonlinear, every profile and divisor) and every first-order coefficient of the near-linear paths, in Q31; every second-order section (the near-linear paths and the Chebyshev II guard band biquads, orders 3 to 11, every profile), in Q2.30.

| rounding | 1st-order sections | with a limit cycle | largest amplitude (LSB) | 2nd-order sections | with a limit cycle | largest amplitude (LSB) |
|---|---:|---:|---:|---:|---:|---:|
| round half up | 74 | 44 | 6 | 88 | 65 | 14 |
| round half even | 74 | 44 | 6 | 88 | 65 | 14 |
| round half away | 74 | 44 | 6 | 88 | 65 | 14 |
| truncate (floor) | 74 | 0 | 0 | 88 | 28 | 4 |
| magnitude truncation | 74 | 0 | 0 | 88 | 0 | 0 |

## 2. The decay to an exactly zero state (magnitude truncation), low-rate frames

Per path: 1 + the sum over its sections of each one's longest decay from the full headroom; the stage: the longer path, plus the decimator's one-sample odd-path latch (near-linear: the delay path's K frames run alongside). Q15 in Q2.29 states, Q31 in Q2.61.

| profile | divisor | nonlinear Q15 | nonlinear Q31 | near-linear Q15 | near-linear Q31 |
|---|---:|---:|---:|---:|---:|
| `super_economy` | 1 | 101 | 207 | 103 | 197 |
| `super_economy` | 2 | 38 | 75 | 37 | 68 |
| `super_economy` | 4 | 36 | 72 | 36 | 65 |
| `super_economy` | 8 | 22 | 42 | 22 | 42 |
| `economy` | 1 | 117 | 240 | 157 | 304 |
| `economy` | 2 | 39 | 77 | 53 | 102 |
| `economy` | 4 | 36 | 72 | 36 | 65 |
| `economy` | 8 | 22 | 42 | 22 | 42 |
| `balanced` | 1 | 128 | 265 | 194 | 366 |
| `balanced` | 2 | 39 | 77 | 53 | 103 |
| `balanced` | 4 | 37 | 72 | 36 | 65 |
| `balanced` | 8 | 22 | 42 | 22 | 42 |
| `transparent` | 1 | 308 | 647 | 518 | 1003 |
| `transparent` | 2 | 81 | 164 | 82 | 158 |
| `transparent` | 4 | 58 | 116 | 50 | 96 |
| `transparent` | 8 | 36 | 71 | 35 | 64 |

## 3. Bit-true Q15 stages (Q2.29 states, magnitude truncation): full-scale input, then silence

Decimators driven by full-scale square waves (periods 2 to 400 high-rate samples) and by sines at +12 dB clipped to full scale (eight frequencies), 6000 outputs each, then zeros. Largest |state| and |difference node| in units of full scale (two guard bits: < 4), and the most frames from the first zero input to an exactly zero state. Every divisor.

| profile | variant | largest state | largest difference node | frames to zero (observed) |
|---|---|---:|---:|---:|
| `super_economy` | nonlinear | 1.959 | 2.959 | 85 |
| `super_economy` | near-linear | 2.056 | 3.071 | 65 |
| `economy` | nonlinear | 1.985 | 2.974 | 99 |
| `economy` | near-linear | 2.094 | 3.503 | 86 |
| `balanced` | nonlinear | 2.018 | 3.004 | 109 |
| `balanced` | near-linear | 2.106 | 3.394 | 101 |
| `transparent` | nonlinear | 2.060 | 3.244 | 232 |
| `transparent` | near-linear | 2.298 | 3.698 | 171 |
