# IIR-0: the design spike for recursive stages in `rational`

The first pull request of the IIR plan ([`docs/IIR_PLAN.md`](../../../../docs/IIR_PLAN.md), the v8
draft, section 5 item 1; its audit is [`docs/PLANS_AUDIT_2026-11.md`](../../../../docs/PLANS_AUDIT_2026-11.md),
whose findings and gaps this spike answers): it **measures**, commits its programs and outputs as a durable record,
and returns go/no-go verdicts. Nothing here ships, and nothing here is in a build CI runs: the
library, its tests, baselines, ceilings and workflows are untouched. The notebook
[`../../../notebooks/iir_spike.ipynb`](../../../notebooks/iir_spike.ipynb) redraws every result and is
the demonstration's first version (I11); **it runs Python designs (scipy, numpy and `python/`), not
the shipping C++**.

Every number below names the file that produced it and the output that holds it. Measured on
Linux x86-64 (an Intel Xeon VM at 2.1 GHz, 4 cores) with GCC 13.3, Clang 18.1, arm-none-eabi-gcc
13.2.1, QEMU 8.2.2 (`qemu-system-arm`), the CodeLinaro Hexagon clang 19.1.5 and a plugin-enabled
`qemu-hexagon` 8.2.2 (built as `ci.yml`'s ratchet job builds it), Python 3.11 with the root
`requirements.lock` (numpy 2.4.6, scipy 1.17.1, matplotlib 3.11.2) plus mpmath 1.4.1.

## The go/no-go verdicts

| # | Question (plan section 5) | Verdict | The numbers | Decision |
|---|---|---|---|---|
| 1 | A near-linear design per 70 dB profile, by a named method | **Go**, and `transparent` too | `super_economy` N5, `economy` N7, `balanced` N8, `transparent` N19 (allpass order = MACs per decimated output), each meeting A + 1 dB | Confirms section 1's "both variants together" and I3 at all four profiles |
| 2 | The round trip's phase | **Confirmed, and better**: the bound is an identity, not a weighting effect | Per stage the phase error is exactly asin of the stopband at the mirror frequency: 0.0181 deg at 70 dB. Round trips with the relaxed designs: -61.0 to -71.5 dB at the 70 dB profiles (x2 to x16), -107 to -115 dB at `transparent` | Question 5's -40 dB can be tightened to the FIR's -60 dB at no extra sections (the maintainer's call) |
| 3 | Float at 120 dB (I14) | **Go**: `transparent` is offered in float | Float states and products: error power -137.1 dB re full scale at worst (`transparent`, near-linear, interpolating, white noise), -141.3 dB for a sine | Confirms I14 |
| 4 | Zero-input limit cycles (I8) | **Reopens I8's rounding**: round-to-nearest fails, magnitude truncation passes | Exhaustive search, complete by Jackson's bound: round to nearest (any tie rule) cycles in 44 of 74 first-order and 65 of 88 second-order sections (up to 6 and 14 LSB); floor truncation in 0 of 74 and 28 of 88; magnitude truncation in none | I8 names magnitude truncation at each section's output (floor suffices for the one-multiplier sections) |
| 5 | Hexagon's floating-point control (I9) | **Reopens I9 for Hexagon** | `qemu-hexagon` models no flush-to-zero field in USR, and none of its writable bits flushes; the per-section flush costs +104 % of a block (float, `economy`) and +108 % (`transparent`), far above the 10 % bar. A per-call state flush costs +2.0 %, a (v + c) - c offset +65 % | I9 needs a Hexagon decision (below) |
| 6 | The constexpr designs (I4) | **Go for the half-band; the Chebyshev II's L-inf gain sweep moves out of constexpr** | GCC 13 and Clang 18 bit-identical on all 36 tables. Clang steps: N17 half-band 30,487 (2.9 % of Clang's default), order-11 Chebyshev II 21,451 without the gain sweep, 414,830 with it (four times MSVC's documented default of 100,000). The prototype's nome step cancels at wide transitions (up to 94,049 ulps); rewritten, every table is within 15 ulps of mpmath | I4 holds with two amendments (below); MSVC and AppleClang need CI |
| 7 | The four-path stage (I12) | **Passes on instructions, not on MACs**, at design divisor 2 (inside x8 and x16) at the 70 dB profiles; fails at x4 and at `transparent` | MACs: a 6-section tie with the pair (12 = 12) at every 70 dB profile (a 4-section tie at `super_economy`'s x16 top); x4's best 8-section designs miss the spec by 2 to 13 dB. Instructions, the pair it replaces against it (`economy`): -23 / -21 / -32 % in float and -13 / -17 / -22 % in Q15 on the M33 / M55 / Hexagon | By section 5 it joins IIR-b's scope, at D = 2 and the 70 dB profiles only; the margin is the spike's kernels' (section 9), so IIR-b's ratchet should confirm it on the shipping code |

## 1. The designs (I3, I13): sections per profile, variant and divisor

`python/gen_designs.py` → `outputs/designs.json`, `cpp/designs.h`; `python/tables.py` →
`outputs/tables.md` section 1. Every design meets -(A + 1 dB) on the 16384-point grid, rational's
margin and grid (`design.h`: `k_design_margin_db`, `k_design_grid_points`).

| profile | divisor | nonlinear | its coefficients | stopband dB | near-linear N | sections (2nd + 1st order) | K | stopband dB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `super_economy` | 1 | N9 | 4 | -90.0 | 5 | 2 + 1 | 5 | -75.5 |
| `super_economy` | 2 | N5 | 2 | -81.2 | 2 | 1 + 0 | 2 | -73.9 |
| `super_economy` | 4 | N5 | 2 | -112.1 | 2 | 1 + 0 | 2 | -104.8 |
| `super_economy` | 8 | N3 | 1 | -83.0 | 1 | 0 + 1 | 1 | -83.0 |
| `economy` | 1 | N9 | 4 | -77.6 | 7 | 3 + 1 | 7 | -77.5 |
| `economy` | 2 | N5 | 2 | -75.8 | 3 | 1 + 1 | 3 | -92.2 |
| `economy` | 4 | N5 | 2 | -107.0 | 2 | 1 + 0 | 2 | -99.6 |
| `economy` | 8 | N3 | 1 | -80.0 | 1 | 0 + 1 | 1 | -80.0 |
| `balanced` | 1 | N9 | 4 | -71.3 | 8 | 4 + 0 | 8 | -74.6 |
| `balanced` | 2 | N5 | 2 | -73.3 | 3 | 1 + 1 | 3 | -88.7 |
| `balanced` | 4 | N5 | 2 | -104.6 | 2 | 1 + 0 | 2 | -97.2 |
| `balanced` | 8 | N3 | 1 | -78.6 | 1 | 0 + 1 | 1 | -78.6 |
| `transparent` | 1 | N17 | 8 | -127.9 | 19 | 9 + 1 | 19 | -121.2 |
| `transparent` | 2 | N9 | 4 | -132.5 | 5 | 2 + 1 | 5 | -126.9 |
| `transparent` | 4 | N7 | 3 | -145.6 | 3 | 1 + 1 | 3 | -129.2 |
| `transparent` | 8 | N5 | 2 | -132.7 | 2 | 1 + 0 | 2 | -125.3 |

- The nonlinear orders reproduce plan 2.2 and 2.4's `ellipord` orders exactly, the margin included.
- The near-linear variant costs 1.25 (`super_economy`), 1.75 (`economy`), 2.0 (`balanced`) and 2.4
  (`transparent`) times the nonlinear's multiplies, against the literature's "two to three times"
  (2.2). Its delay path is always K = N, and its passband group delay (2K high-rate samples: 14 at
  `economy`, 38 at `transparent`) is below the FIR half-band's 21 and 61.
- At divisor 8 the two variants are the same filter up to one sample of delay: the N3 elliptic
  half-band's second path holds no section, so it is a bare delay and the design already has the
  near-linear form.
- Quantizing the coefficients to I8's formats (Q31 one-multiplier, Q2.30 second-order) moves no
  stopband by more than 0.02 dB (`outputs/tables.md` section 8).

**MACs per output frame per channel, decimating, relaxed / unrelaxed** (`outputs/tables.md` section
2; the plan's 2.4 table, which these confirm for the nonlinear variant; the FIR rows are today's
half-band chains at the same divisors, `design.h`'s pins, 2m + 1 per decimated output):

| profile | variant | x2 | x4 | x8 | x16 |
|---|---|---:|---:|---:|---:|
| `super_economy` | nonlinear | 4 | 8 / 12 | 16 / 28 | 24 / 60 |
| `super_economy` | near-linear | 5 | 9 / 15 | 17 / 35 | 25 / 75 |
| `super_economy` | FIR half-bands | 19 | 49 / 57 | 109 / 133 | 181 / 285 |
| `economy` | nonlinear | 4 | 8 / 12 | 16 / 28 | 24 / 60 |
| `economy` | near-linear | 7 | 13 / 21 | 21 / 49 | 29 / 105 |
| `economy` | FIR half-bands | 23 | 53 / 69 | 113 / 161 | 185 / 345 |
| `balanced` | nonlinear | 4 | 8 / 12 | 16 / 28 | 24 / 60 |
| `balanced` | near-linear | 8 | 14 / 24 | 22 / 56 | 30 / 120 |
| `balanced` | FIR half-bands | 27 | 57 / 81 | 117 / 189 | 189 / 405 |
| `transparent` | nonlinear | 8 | 16 / 24 | 28 / 56 | 44 / 120 |
| `transparent` | near-linear | 19 | 29 / 57 | 41 / 133 | 57 / 285 |
| `transparent` | FIR half-bands | 63 | 105 / 189 | 181 / 441 | 333 / 945 |

An interpolating stage spends half its decimating figure per output frame (both paths run once per
input frame and give two outputs).

## 2. The near-linear method, and the round trip's phase (go/no-go 1 and 2)

**The structure** is the plan's: H(z) = 1/2 [z^-2K + z^-1 A1(z^2)], one path a pure delay and the
other a real allpass of order N in w = z^2 (Renfors and Saramäki 1987; Krukowski and Kale 2003, the
plan's section 7). **The identity** (derived here from the structure, in `python/iir_design.py`): with
eps(theta) = arg A1(e^{j theta}) + (K - 1/2) theta the allpass path's phase error against the delay,
H = 1/2 e^{-j 2K omega} (1 + e^{j eps}) in the passband, so the passband's phase error is eps / 2 and its
magnitude cos(eps / 2); at the mirror frequency pi - omega the stopband is exactly |sin(eps / 2)|. The
passband phase error at any frequency is therefore asin of the stopband at its mirror, for every
design of this structure: a 70 dB stopband bounds it to 0.0181 deg, with no weighting involved (the
audit's gap said "if its design weights the bands equally"; the weighting cannot separate them).
`outputs/tables.md` section 3 checks it per design (the phase error equals the bound to 1e-13 deg).

**The method** follows from the identity: minimise the largest |sin(eps / 2)| over the passband,
which is the stopband. Writing the allpass as A1 = w^-N conj(D) / D, D = sum d_k w^-k, gives
sum_k d_k sin(k theta + beta) = |D| sin(eps / 2) with beta = (K - 1/2 - N) theta / 2, which is linear
in the d_k. So with the weight 1 / |D| frozen from the previous iterate, the minimax is a linear
program (`scipy.optimize.linprog`, HiGHS), iterated until it stops improving, then polished on the
true objective by SLSQP with its analytic Jacobian (`nearlinear_lp`, `nearlinear_polish`); the polish
moves the designs by up to 0.7 dB, and only `transparent`'s N19 needed it to meet its margin. The
lowest N meeting -(A + 1 dB) is searched with K from N - 1 to N + 2; K = N won every time. **Which
constraint is active:** the stopband, which by the identity is the passband phase error; the
passband magnitude (cos(eps / 2), within 8e-8 dB at `economy`) and the phase target never bind.
*Attribution to confirm:* the structure is the cited literature's; the linear-programming form above
is this spike's derivation from the structure and a general-purpose optimizer, not a method copied
from Krukowski and Kale's book, which was not available here. The maintainer may want IIR-a's
`NOTICE.md` record to say so, or to check the book for its own method.

**Per stage** (`outputs/tables.md` section 3; high-rate samples of the stage, DC to its passband edge):

| profile | divisor | nonlinear group delay, DC .. edge | near-linear 2K | deviation | phase error deg |
|---|---:|---:|---:|---:|---:|
| `super_economy` | 1 | 2.53 .. 4.21 | 10 | 0.016 | 0.0097 |
| `economy` | 1 | 2.40 .. 4.96 | 14 | 0.018 | 0.0077 |
| `balanced` | 1 | 2.31 .. 5.54 | 16 | 0.027 | 0.0106 |
| `transparent` | 1 | 4.16 .. 11.92 | 38 | 0.0005 | 0.00005 |
| `economy` | 2 / 4 / 8 | 1.57 .. 1.78 / 1.61 .. 1.66 / 1.00 .. 1.01 | 6 / 4 / 2 | 0.002 / 0.001 / 0.006 | 0.0014 / 0.0006 / 0.0058 |

The nonlinear figures confirm plan 2.2's corrected 2.4 .. 5.0 (`economy`) and 4.2 .. 11.9
(`transparent`).

**Round trips** (`outputs/tables.md` section 4): down then up through the same relaxed stages, both
directions sharing one H(z). `residual` is max |1 - G| over [0, p r_min] with G the round trip's
in-band response, its delay removed and its magnitude included.

| profile | x2 | x4 | x8 | x16 | every stage exactly at -A, x16 |
|---|---:|---:|---:|---:|---:|
| `super_economy` | 0.019 deg, -69.5 dB | 0.042 deg, -62.6 dB | 0.043 deg, -62.5 dB | 0.051 deg, -61.0 dB | 0.145 deg, -51.9 dB |
| `economy` | 0.015, -71.5 | 0.018, -70.0 | 0.019, -69.4 | 0.031, -65.4 | 0.145, -51.9 |
| `balanced` | 0.021, -68.6 | 0.025, -67.1 | 0.027, -66.5 | 0.041, -63.0 | 0.145, -51.9 |
| `transparent` | 0.0001, -115.2 | 0.0002, -111.5 | 0.0002, -109.5 | 0.0003, -107.1 | 0.0005, -101.9 |

So the plan's "about -52 dB" is the bound with every stage exactly at 70 dB; the designs as found
(each stage meets its own margin, and the relaxed ones far exceed it) reach -61 dB or better through
x16 at every 70 dB profile, so question 5's target can be tightened to the FIR's -60 dB at no extra
sections. The per-stage maxima add (the bound column equals the measured one): the errors share a
sign at the band edge.

## 3. Float at 120 dB (go/no-go 3, I14)

`cpp/float_floor.cpp` → `outputs/float_floor.md`. Each profile, both variants, decimating and
interpolating at divisor 1, float states and products against the same structure in long double
(x86's 64-bit significand), 2^18 outputs; error power against a full-scale sine's.

- **Float states, worst case:** -137.1 dB (`transparent`, near-linear, up, white noise); -141.3 dB for
  a sine; the 70 dB profiles -141.7 dB or lower. The largest bin of the error spectrum (65536-point
  Hann) is -151.2 dB at worst. So float clears 120 dB by 17 dB and `transparent` stays offered in float.
- **Double states with float I/O** equal the float output's own rounding (-153.7 dB for a sine,
  -156.8 dB for noise): the price of float states is 5 to 20 dB of a floor far below every profile.
- **What float states save** (`icount/`, `economy`'s decimator, 32 output frames per call, mono):
  1,038 instructions with float states against 29,401 with double on the M33 and M4F (soft-float),
  28 times; on the M55 (hardware double) 1,008 against 1,010.

## 4. Fixed point (go/no-go 4, I8 and I6)

`fixed_point/fixed_point.cpp` → `outputs/fixed_point.md`. Formats as I8 names them (Q31 one-multiplier
coefficients, Q2.30 second-order ones, one rounding per section at its output).

**The search.** A zero-input limit cycle is the section's response to its rounding errors, so it lies
within |y| <= e_max ||h||_1 (Jackson 1969: e_max = 1/2 LSB rounding, 1 LSB truncating). With zero input
a cascade's first section is autonomous after one or two samples, and each later one once its
upstream is zero, so the cascade is free of limit cycles exactly when each section is. The program
runs every state of each section's box exhaustively: every one-multiplier coefficient of every
design (nonlinear, every profile and divisor), the near-linear paths' first- and second-order
sections, and the Chebyshev II guard band filter's sections at orders 3 to 11, every profile:

| rounding at the section's output | first-order sections with a limit cycle | largest (LSB) | second-order sections with a limit cycle | largest (LSB) |
|---|---:|---:|---:|---:|
| round half up / half even / half away | 44 of 74 | 6 | 65 of 88 | 14 |
| truncate (floor) | 0 of 74 | 0 | 28 of 88 | 4 |
| **magnitude truncation (toward zero)** | **0 of 74** | 0 | **0 of 88** | 0 |

In integer units the recursion is the same for Q15 and Q31 samples (Q2.29 or Q2.61 states), so one
search covers both. Floor truncation is provably enough for the one-multiplier section with a > 0:
its zero-input recursion u <- floor(-a u) alternates sign and shrinks every two samples. It saves
about a quarter of the Q15 decimator's instructions on the M33 (1,634 against 2,279 per block).

**The decay to an exactly zero state** (I6's fixed-point window, upper bounds: 1 + the sum along each
path of each section's longest decay from the full headroom, exact for first-order sections, the
linear decay into the search box plus the box's longest time for second-order ones), low-rate frames,
magnitude truncation:

| profile | nonlinear Q15 | nonlinear Q31 | near-linear Q15 | near-linear Q31 |
|---|---:|---:|---:|---:|
| `super_economy` | 101 | 207 | 103 | 197 |
| `economy` | 117 | 240 | 157 | 304 |
| `balanced` | 128 | 265 | 194 | 366 |
| `transparent` | 308 | 647 | 518 | 1003 |

(divisor 1; the relaxed designs 22 to 164, `outputs/fixed_point.md`). Bit-true Q15 stages driven at full
scale reach zero within 65 to 232 frames, under the bounds.

**Headroom** (`outputs/tables.md` section 6, `outputs/fixed_point.md` section 3). The stored states
(the section outputs) have L1 norms up to 2.69 at `economy` and 3.71 at `transparent`, so the plan's two
integer guard bits hold them for any input. **But the difference node x - y1 that feeds each multiply
reaches L1 3.64 and 3.77 at `economy` (nonlinear, near-linear), 4.04 at `balanced` near-linear, 5.06 at
`transparent` near-linear and 5.22 at `transparent` nonlinear**, past |node| < 4. Bit-true Q15 stages driven by full-scale square waves (every
period from 2 to 400) and clipped sines reach 2.30 in a state and 3.70 in a difference node: no
saturation, but no margin either at `transparent`. (The audit's 1.93 measured section outputs only, at
`economy`, periods from 4.) Recommended: three integer bits, Q15 samples in **Q3.28** 32-bit states
(the difference then fits 32 bits and one 32 x 32 multiply serves) and Q31 samples in **Q3.60** 64-bit
states; or two bits with the difference formed in the 64-bit accumulator, as `fixed_point.cpp` does.
The guard band filter's partial cascades (L-inf scaled, increasing pole radius) have L1 norms up to
3.05 at the orders below, inside two bits.

**The Q15 floor** (`outputs/fixed_point.md` section 4): a -1 dBFS sine through the bit-true Q15
decimator (Q2.29 states, magnitude truncation, the output rounded half up) against the same structure
in double: -98.1 dB (`economy`, nonlinear) to -101.1 dB (`transparent`, near-linear), in every case
equal to the double reference merely rounded to Q15. The recursion adds nothing measurable above the
output's own rounding.

**Cost.** Fixed point is dearer than float here: per 32-frame mono block of `economy`'s decimator,
Q15 (Q3.28, magnitude truncation) 2,279 on the M33, 2,377 on the M55 and 704 packets on Hexagon,
against float's 1,038, 1,008 and 197; Q31 (Q3.60, a 64 x 32 multiply) 5,178, 5,597 and 1,467 (section
8 has every row). Both are well below today's FIR stage in the same harness (section 8).

## 5. Flush to zero (go/no-go 5, I9) and window_frames() (I6)

`ftz/flush_guard.h` is the guard as v8 specifies it: chosen per target and type at compile time
(the guard where the control bit governs the type, the per-section flush elsewhere), conditional,
restoring only the bits it set, through `__builtin_arm_get_fpscr` / `__builtin_arm_set_fpscr` on
32-bit Arm, `_mm_getcsr` / `_mm_setcsr` on x86-64, `mrs` / `msr` FPCR on AArch64, and empty
elsewhere. `ftz/ftz_check.cpp` runs on every target (`icount/run_targets.py` →
`outputs/targets.json`, `ftz_check`):

| target | float | double | silence to an exact zero (`economy` / `transparent`, float; double) | the environment after a call, both host states |
|---|---|---|---|---|
| x86-64 | guard | guard | 405 / 977; 3412 / 8313 | control bits equal, flags raised by the call kept in both |
| M4 (no FPU) | per-section flush | per-section flush | the same | no guard (it compiles to nothing) |
| M4F, M33 | guard | per-section flush | the same | control bits equal in both |
| M55 | guard | guard | the same | control bits equal in both |
| Hexagon | per-section flush | per-section flush | the same | no guard |

- **Sticking without a mechanism** (dsp-14, every target): float and double stick at 2 times the
  smallest subnormal for a = 0.8134 and 6 times for a = 0.9188, as k <= 1 / (2 (1 - a)) predicts; with
  FPSCR.FZ set, double still sticks on the M4F and M33 (soft-float), as dsp-4 found.
- **The guard's cost** (instructions per call around an empty body, against the same call without
  it): +10 with the host's bit off, +5 with it on, on the M4F, M33 and M55 (1.0 % and 0.5 % of a 32-frame
  `economy` decimator block); 0 on the M4 and Hexagon. Listings: `outputs/guard_cortex_m.txt`
  (`vmrs`, `lsls`, `bpl`: three instructions when the bit is set; `orr`, `vmsr`, then `vmrs`, `bic`,
  `vmsr` to restore) and `outputs/guard_aarch64.txt` (Clang 18 for AArch64, compiled, not run:
  `mrs`, `tbnz` when the bit is set). On x86-64 the moved programs reproduce I9's table
  (`outputs/ftz_host.txt`): the conditional guard +0.01 ns per call with the host's bits on, +5.5 ns
  off; subnormal states make a 32-frame stereo block 9.8 times slower without it.
- **The per-section flush's cost** per 32-frame block: +93 % of the float decimator on the M33 and
  M4F, +99 % on the M55, +104 % on Hexagon; +32 % of the soft double decimator on the M33.

**Hexagon.** QEMU 8.2.2's USR has the fields OVF, the five FP sticky flags (bits 1 to 5), LPCFG,
FPRND (22 to 23) and the five FP exception enables (25 to 29) and nothing else
(`target/hexagon/reg_fields_def.h.inc`); its softfloat status never enables flush-to-zero; a write
keeps bits 0 to 5, 8 to 19 and 22 to 23 (`reg_immut_masks` 0xc13000c0); and with every keepable
non-flag bit set, a subnormal product stays subnormal in float and double (`ftz_check` on Hexagon).
Whether silicon has a control the emulator lacks is for the Hexagon Programmer's Reference Manual,
which this spike did not consult. Also, under the family's toolchain file (no `-mcpu`, so clang 19's
default `hexagonv60`) **double is software** (`__qdsp_muldf3`, 5,642 packets per block against
float's 197), so double needs the flush there too. The bar was 10 % of a block; the per-section flush
costs 104 %. Measured alternatives, float `economy` decimator, 32 frames per call:

| mechanism on Hexagon | packets per block | cost | what it changes |
|---|---:|---:|---|
| none | 197 | | states stick at k x 2^-149: never exactly zero |
| per-section flush (compare and select per state per sample) | 401 | +104 % | nothing; the plan's mechanism |
| states flushed once per call, after the block | 201 | +2.0 % | outputs then depend on the block size, at subnormal levels only |
| every stored state through (v + c) - c, c = 2^-60 | 326 | +65 % | rounds every state to multiples of 2^-83 (about -500 dB) |

The decision for the maintainer: accept the per-call flush on Hexagon (and say that bit-exactness
across block sizes holds there only above the smallest normal: `FlushEqualsZeroPaddingBitForBit` would
then have to cut its zero padding into the same blocks as the flush, since the two runs' subnormal
outputs differ otherwise), pay the per-section flush there, or read I6 on Hexagon as "below the
smallest normal" rather than "exactly zero". The same per-call flush costs +4.2 % on the M33 and +4.4 %
on the M55. Double on Hexagon (software) pays +14 % for the per-section flush (6,410 against 5,642).

**window_frames() in float and double** (I6 restated; `outputs/tables.md` section 7 and
`cpp/window_float.cpp` → `outputs/window_float.md`): the formula ceil(ln(max / min_normal) / ln(1 / r))
over the stage's largest pole radius gives 853 / 6867 frames at `economy` and 2080 / 16752 at
`transparent` (nonlinear; near-linear 698 / 5617 and 1402 / 11293), confirming the plan's estimates. A
whole stage started with every state at max / 8 in 64 sign patterns reaches an exactly zero state
within the formula in every case but one, where it needs one frame more (`super_economy` near-linear,
514 against 513): IIR-a should add a margin of the section count.

## 6. The constexpr designs (go/no-go 6, I4)

`constexpr/cx_math.h` (sqrt, sin, cos, tan, exp, log by series and iteration, no libm) and
`constexpr/cx_designs.h` (the elliptic half-band from the prototype; the odd-order Chebyshev II from
its analogue prototype, the bilinear transform, the pairing, the order by pole radius and the gain
rule), driven by `constexpr/designs_main.cpp`.

- **Bits.** GCC 13.3 and Clang 18.1 produce the same bits for all 16 half-band tables (every profile,
  divisors 1 to 8) and all 20 Chebyshev II tables (every profile, orders 3 to 11, with the L-inf gain
  over 256 frequencies): `constexpr/expected_bits.txt`.
- **Steps** (`constexpr/bisect_local.sh` → `outputs/constexpr_steps.md`; `probe_steps.cmake` reproduces
  them through CMake):

  | design | Clang 18 steps | GCC 13 operations |
  |---|---:|---:|
  | half-band N17 (`transparent`, the largest named order) | 30,487 | 143,931 |
  | half-band N9 (`economy`) | 15,385 | 84,509 |
  | Chebyshev II order 11, L-inf gain over 256 frequencies | 414,830 | 1,506,277 |
  | Chebyshev II order 5, L-inf gain over 256 frequencies | 155,612 | 815,738 |
  | Chebyshev II order 11, unity DC gain per section | 21,451 | 160,250 |

  Against the defaults (Clang 1,048,576 steps, GCC 33,554,432 operations, MSVC /constexpr:steps
  100,000 by its documentation; MSVC counts in its own units), the half-band and the Chebyshev II
  without its gain sweep have a wide margin; the L-inf sweep does not fit MSVC's default if its steps
  compare. **Amendment:** design the Chebyshev II in constexpr with unity DC gain per section and take
  the L-inf factors from a committed table per named setting, or from a cheaper rule.
- **Accuracy** (`python/constexpr_accuracy.py` → `outputs/constexpr_accuracy.md`, against the same
  formulas at 60 digits in mpmath). The prototype's e = (1 - k'^(1/2)) / (2 (1 + k'^(1/2))) cancels when
  the transition is wide, which is exactly the relaxed designs I13 adds: up to 94,049 ulps
  (`super_economy` at divisor 8) in constexpr and libm alike. Since 1 - k'^(1/2) = k^2 / ((1 + k'^(1/2))
  (1 + k')), the same step has no cancellation; with it every table is within 14.8 ulps of mpmath
  (`cx_designs.h` carries it). **Amendment:** IIR-a's designer uses that form, and its accuracy test pins
  ulps against an mpmath leg as I4 plans.
- **MSVC and AppleClang** could not run here. `constexpr/` is a self-contained CMake project (CMake
  3.20 and a C++20 compiler, nothing else): `cmake -S . -B build && cmake --build build --config
  Release && ctest --test-dir build -C Release` runs `cx.BitsMatchGccAndClang` (the tables against
  `expected_bits.txt`, built with `/W4 /WX /permissive-` under MSVC), and `cmake -DWHICH=1 -P
  probe_steps.cmake` (then 3 and 5) bisects that compiler's step limit. A Windows and a macOS runner
  of the existing CI can run it as one extra step each.

## 7. The guard band filter (I7, 2.3)

`python/tables.py` → `outputs/tables.md` section 5. Both forms at one passband spec: flat within 0.01
dB peak to peak to the profile's edge (the Chebyshev II monotone, rp = 0.01 dB; the type II FIR
equiripple, `scipy.signal.remez`), A dB from the Chebyshev's stopband edge. Edges as fractions of the
low Nyquist; `worst alias` is the nonlinear half-band's alias after the filter at its largest in the
guard band.

| profile | order | stop edge | Chebyshev MACs | type II taps | FIR MACs (folded) | FIR delay | worst alias dB |
|---|---:|---:|---:|---:|---:|---:|---:|
| `super_economy` | 3 / 5 / 7 / 9 | 0.986 / 0.931 / 0.870 / 0.820 | 6 / 10 / 14 / 18 | 20 / 30 / 38 / 52 | 10 / 15 / 19 / 26 | 9.5 / 14.5 / 18.5 / 25.5 | -26 / -40 / -50 / -58 |
| `economy` | 3 / 5 / 7 / 9 | 0.990 / 0.951 / 0.906 / 0.869 | 6 / 10 / 14 / 18 | 26 / 40 / 50 / 64 | 13 / 20 / 25 / 32 | 12.5 / 19.5 / 24.5 / 31.5 | -23 / -35 / -44 / -50 |
| `balanced` | 3 / 5 / 7 / 9 | 0.992 / 0.960 / 0.923 / 0.892 | 6 / 10 / 14 / 18 | 32 / 48 / 58 / 76 | 16 / 24 / 29 / 38 | 15.5 / 23.5 / 28.5 / 37.5 | -22 / -32 / -40 / -46 |
| `transparent` | 3 / 5 / 7 / 9 | 0.999 / 0.990 / 0.972 / 0.953 | 6 / 10 / 14 / 18 | - / 58 / 72 / 82 | - / 29 / 36 / 41 | - / 28.5 / 35.5 / 40.5 | -32 / -49 / -62 / -71 |

(Order 11 in the outputs; the type II FIR at `transparent`'s order-3 edge, 0.999, did not converge.)
The 70 dB rows reproduce plan 2.3's reach table and the audit's 40 / 48-50 / 64 taps (dsp-13).

**An order per profile.** I7 leaves "an order per profile, each set by the spike" without a rule. A
rule that sets one: the lowest odd order whose worst combined alias in the guard band is at or below
-A / 2 (the half-band's aliases at Nyquist sit at -3 dB). It gives **5 at `super_economy` and 7 at
`economy`, `balanced` and `transparent`** (`outputs/tables.md`, "An order per profile"); the plan's
own examples use 5 at `economy`, which leaves -35 dB. The rule is the spike's proposal, for the
maintainer.

**Before the decimator against after it** (`economy`, 16 kHz out, 0.01 dB to 6 kHz, 70 dB): order 5
after the decimator reaches 70 dB from 7.605 kHz for 10 MACs per output. The same order and passband
at 32 kHz reaches 70 dB only at 13.21 kHz and is -0.27 dB at 7.6 kHz, -0.59 at 8, -1.24 at 8.4 and
-10.71 at 10 kHz; matching the after-filter there needs order 14 (15 odd), 60 MACs per output, 6.0
times. This confirms 2.3 as v8 corrected it and the audit's dsp-5. The notebook shows it on the
sweep: the order-7 filter before the decimator leaves the aliases near the low Nyquist at -11.9 dB,
after the decimator at -77.3 dB.

## 8. Instructions on the targets

`icount/` (a scratch CMake project over the family's toolchain files, DspTap's for the M4 / M4F;
Release, as the ratchet builds) and `icount/run_targets.py`, which counts each workload under QEMU
with the repository's plugin (`tools/qemu_insn_plugin`, built against the header `ci.yml` pins),
bracketing the counted calls with `tools/probe/call_marker.h`'s markers. Per 32-output-frame mono
block, `economy` unless named; Hexagon counts packets. FIR rows are today's `basic_stage` in the
same harness. `outputs/targets.json` holds every row.

| workload | M4 (soft) | M4F | M33 | M55 | Hexagon |
|---|---:|---:|---:|---:|---:|
| IIR down 2, float states | 21,632 | 1,038 | 1,038 | 1,008 | 197 |
| IIR down 2, double states | 29,402 | 29,401 | 29,401 | 1,010 | 5,642 |
| IIR down 2, Q15 (Q3.28, magnitude truncation) | 2,146 | 2,146 | 2,279 | 2,377 | 704 |
| IIR down 2, Q15 (Q3.28, floor) | 1,601 | 1,602 | 1,634 | 1,898 | 452 |
| IIR down 2, Q31 (Q3.60, magnitude truncation) | 5,146 | 5,178 | 5,178 | 5,597 | 1,467 |
| IIR down 2, Q31 (Q3.60, floor) | 4,410 | 4,445 | 4,442 | 4,539 | 892 |
| IIR near-linear down 2, float | 36,734 | 2,127 | 2,161 | 2,263 | 620 |
| IIR near-linear down 2, Q15 | 3,365 | 3,495 | 3,367 | 3,567 | 1,537 |
| IIR up 2, float | 9,565 | 507 | 507 | 493 | 88 |
| IIR up 2, Q15 (magnitude truncation) | 1,139 | 1,172 | 1,186 | 1,237 | 341 |
| **FIR down 2, float** (today's `basic_stage`) | 116,686 | 117,548 | 117,549 | 7,979 | 21,311 |
| **FIR down 2, Q15** | 6,943 | 6,945 | 6,944 | 5,569 | 3,282 |
| **FIR up 2, float** | 57,128 | 57,482 | 57,482 | 3,588 | 10,876 |
| **FIR up 2, Q15** | 3,214 | 3,214 | 3,213 | 2,476 | 1,799 |
| `transparent` IIR down 2, float / double | 40,523 / 54,953 | 1,678 / 54,956 | 1,680 / 54,956 | 1,781 / 1,650 | 369 / 10,582 |

The flush rows are in section 5; the cascades in section 9; every row in `outputs/targets.md`.

- **IIR against today's FIR stage** (the plan's first risk, "instructions, not MACs"): the nonlinear
  `economy` decimator counts 4.2 times fewer instructions than the FIR stage in Q15 on the M33 (floor
  rounding: 1,634 against 6,944; 3.0 times with magnitude truncation), 2.3 to 2.9 times on the M55 and
  4.7 to 7.3 times on Hexagon; the near-linear one 2.1, 1.6 and 2.1 times in Q15. In float the FIR stage
  accumulates in double (`sample_traits<float>`), soft-float on the M33 and M4F and Hexagon v60, so the
  float IIR stage there is 113 (M33) and 108 (Hexagon) times cheaper, and 7.9 times on the M55.
- **Fixed point against float in the IIR stage**: Q15 costs 1.6 to 2.4 times float's instructions on
  the M33 and M55 and 2.3 to 3.6 times on Hexagon; Q31 4.3 to 5.6 times on the M33 and M55 and 4.5 to 7.4
  times on Hexagon (floor and magnitude truncation the two ends). With float states the stage never
  needs the soft double.
- **Float against double states** (I14's count of one section both ways): 1,038 against 29,401 on the
  M33 and M4F, 28 times; on the M55 they cost the same.

## 9. The four-path stage (go/no-go 7, I12)

`python/fourpath.py` → `outputs/fourpath.md` and `.json`. A four-path stage H(z) = 1/4 sum_k z^-k
A_k(z^4) whose output is at design divisor D replaces the nonlinear half-bands at divisors 2D and D, on
plan 2.4's spec: only the bands that fold onto the passband ([1/4 - f, 1/4 + f] and [1/2 - f, 1/2] of
its input rate, f = p / (4D)), at -(A + 1 dB) like every design here; its passband is tied to them by
the M-path power complementarity. Designs by SLSQP minimax from random starts over every layout with
the paths at most two sections apart up to 6 sections, and the balanced layouts above, stopping at
the first section count that meets the spec or at the tie. D = 1 is x4 (and the bottom of x8 and
x16), D = 2 the top of x8 and the middle of x16, D = 4 the top of x16.

**MACs** (per chain output frame per channel; the bar is fewer than 0.9 times the pair's):

| profile | D | pair it replaces | pair MACs | sections at a tie | fewest meeting the spec | best at the tie | group delay, pair / four-path (output frames) |
|---|---:|---|---:|---:|---:|---:|---|
| `super_economy` | 1 | N5 . N9 | 8 | 8 | none | -68.7 dB | 1.66 .. 2.54 / - |
| `super_economy` | 2 | N5 . N5 | 12 | 6 | **6, a tie** | -89.6 dB | 0.60 .. 0.64 / 0.59 .. 0.64 |
| `super_economy` | 4 | N3 . N5 | 16 | 4 | **4, a tie** | -73.4 dB | 0.26 .. 0.27 / 0.22 .. 0.23 |
| `economy` | 1 | N5 . N9 | 8 | 8 | none | -61.7 dB | 1.59 .. 2.93 / - |
| `economy` | 2 | N5 . N5 | 12 | 6 | **6, a tie** | -84.2 dB | 0.59 .. 0.65 / 0.58 .. 0.65 |
| `economy` | 4 | N3 . N5 | 16 | 4 | none | -70.3 dB | 0.26 .. 0.27 / - |
| `balanced` | 1 | N5 . N9 | 8 | 8 | none | -58.1 dB | 1.55 .. 3.22 / - |
| `balanced` | 2 | N5 . N5 | 12 | 6 | **6, a tie** | -81.7 dB | 0.59 .. 0.66 / 0.58 .. 0.66 |
| `balanced` | 4 | N3 . N5 | 16 | 4 | none | -68.9 dB | 0.26 .. 0.27 / - |
| `transparent` | 1 | N9 . N17 | 16 | 16 | none | -93.4 dB (at 15) | 2.77 .. 6.77 / - |
| `transparent` | 2 | N7 . N9 | 20 | 10 | none | -104.4 dB (at 9) | 0.97 .. 1.10 / - |
| `transparent` | 4 | N5 . N7 | 28 | 7 | none | -113.2 dB | 0.38 .. 0.39 / - |

So in MACs the four-path stage never beats the pair: at best it ties (D = 2 at every 70 dB profile,
layout [2, 2, 1, 1]; D = 4 at `super_economy`), as plan 2.4 expected; x4 fails everywhere, and
`transparent` fails at every placement (its search is not conclusive at large counts: the best found
is not monotone in the count, -88.4 dB at 16 against -93.4 at 15, but nothing near the -121 dB the
tie needs). Its group delay equals the pair's within 0.01 output frames at D = 2 (0.04 less at
`super_economy`'s D = 4): no case for a latency-first caller.

**Instructions** (`outputs/targets.md`; `economy`, the cascades of `python/gen_cascades.py`, per
32-output-frame mono block; the D = 2 design meets the spec, the D = 1 and D = 4 ones miss it and are
counted for their instructions only):

| chain | float M33 | float M55 | float Hexagon | Q15 M33 | Q15 M55 | Q15 Hexagon |
|---|---:|---:|---:|---:|---:|---:|
| x8: three half-bands | 5,286 | 5,068 | 2,140 | 7,649 | 8,741 | 3,243 |
| x8: four-path (D = 2) + half-band | 4,322 | 4,230 | 1,511 | 6,876 | 7,587 | 2,617 |
| the pair alone against the four-path alone | **-23 %** | **-21 %** | **-32 %** | **-13 %** | **-17 %** | **-22 %** |
| x16: four half-bands | 9,391 | 8,919 | 4,002 | 14,064 | 15,163 | 6,448 |
| x16: four-path in the middle | 8,428 | 8,082 | 3,371 | 13,295 | 13,754 | 5,756 |
| x4: two half-bands / one four-path (misses the spec) | 2,460 / 2,005 | 2,368 / 1,975 | 946 / 449 | 3,653 / 3,393 | 4,106 / 3,719 | 1,524 / 1,190 |

(The pair alone is the chain less its common last half-band, the `dec_float` and `dec_q15_floor` rows;
the Q15 chains use floor rounding, which section 4 shows is enough for these first-order sections.
The C++ four-path kernel reproduces the Python design's response, tone by tone:
`outputs/fourpath_check.txt`.)
At the same MACs the four-path stage counts 13 to 32 % fewer instructions than the pair it replaces,
on every target and both formats: it runs one stage pass with four independent recursions where the
pair runs two passes through an intermediate buffer. **By I12's bar (more than 10 % in MACs or in
instructions, at some profile) it passes, on instructions, at D = 2 inside x8 and x16 at the three 70
dB profiles** (measured at `economy`; `super_economy` and `balanced` have the same layout, so the same
code). The plan expected the cascade to win; it does in MACs, not in instructions. Two cautions for
the maintainer: these are the spike's kernels, both written the same way, not the shipping stage or
chain (a fused half-band chain could narrow the gap), and the Q15 margin on the M33 (13 % for the
pair, 10 % for the whole x8 chain) is close to the bar. IIR-b's ratchet would settle it on the shipping
code.

## What this spike could not measure, and what needs CI

- **MSVC and AppleClang** (I4): the constexpr project in `constexpr/` is ready for a Windows and a macOS
  runner (section 6).
- **The guard's cost on Apple silicon** (I9 asks for it): `flush_guard.h`'s AArch64 path compiles
  (`outputs/guard_aarch64.txt`) but no AArch64 hardware or emulator is here, and the x86 benchmarks
  (`ftz_bench.cpp`, `guard_branch.cpp`) use `<xmmintrin.h>`; an AArch64 port of them on the macOS
  runner would answer it.
- **Hexagon silicon**: whether USR or another register has a flush control the emulator lacks, and
  whether subnormal arithmetic is slow there (QEMU counts packets, not cycles).
- **Cycles** on any Cortex-M: QEMU counts instructions only.
- **The near-linear four-path stage**: not designed. I12 asks for both variants; with the nonlinear
  one passing on instructions (section 9), the near-linear one is the open half of I12.
- **The four-path stage in the shipping stage and chain**: the instruction margin of section 9 is
  between the spike's kernels; IIR-b's ratchet measures the real ones.
- **f. harris's design scripts**: not opened, per the plan's clean room; the maintainer runs them
  against `outputs/designs.json` for the independent check (section 7 of the plan).
- **Krukowski and Kale's book** was not available: the near-linear method is attributed in section 2
  as what it is.

## What contradicts or changes the plan

- I8: two guard bits do not hold the difference node at `transparent` (L1 5.22) or `balanced`
  near-linear (4.04): three bits (Q3.28 / Q3.60), or the difference in the accumulator.
- I8: round-to-nearest leaves zero-input limit cycles; the rounding is magnitude truncation (floor for
  the one-multiplier sections), which DspTap's FIR traits (round half away) do not use.
- I8's cost: fixed point costs more instructions than float on the M33, M55 and Hexagon.
- I9: Hexagon has no flush control to use; the per-section flush there costs 104 %, ten times the bar.
  And double is software on the family's Hexagon build.
- I4: the prototype's nome step loses up to 1e5 ulps on the relaxed designs (fixed by algebra); the
  Chebyshev II's L-inf gain sweep is too long for MSVC's default step limit.
- Question 5 and the go/no-go's "about -52 dB": a bound with every stage exactly at 70 dB; the designs
  reach -61 dB or better through x16, and the phase bound needs no assumption on the weighting.
- 2.2's "two to three times the nonlinear's coefficients": 1.25 to 2.4 times measured.
- I6's float formula needs a margin of about one frame per section.
- I7's order per profile has no rule in the plan; the spike proposes one (5, 7, 7, 7).
- I12 and 2.4: "the cascade stays cheaper" holds in MACs (the four-path stage at best ties), not in
  instructions: at design divisor 2 the four-path stage counts 13 to 32 % fewer than the pair on the
  M33, M55 and Hexagon, which passes I12's bar and, by section 5, brings it into IIR-b's scope.

## Sources, per formula and method

- The two-path structure, the commutator and the half-sum: Valenzuela and Constantinides 1983.
- The elliptic half-band's coefficients (`iir_design.halfband_coefficients`, `cx_designs.h`): the
  selectivity of the bireciprocal case and the half-band's a = (1 - x) / (1 + x) from Gazsi 1985 and
  Valenzuela and Constantinides 1983; the modulus, the nome series and the critical frequencies' series
  as Antoniou 2006 gives the elliptic approximation (also in Gazsi 1985). The cancellation-free step
  is algebra on those formulas.
- The near-linear structure: Renfors and Saramäki 1987; Krukowski and Kale 2003. The identity, the
  linear-programming form and its iteration: derived here from the structure (section 2); the
  optimizers are scipy's (HiGHS, SLSQP).
- The four-path structure: harris 2004 (the M-path recursive polyphase filters); Renfors and Saramäki
  1987. Its design: SLSQP minimax from random starts, as the audit's scratch programs did.
- The Chebyshev II and the bilinear transform with prewarping: Antoniou 2006. Section ordering and the
  L-inf gain rule: Jackson 1970. The type II FIR: Parks-McClellan through `scipy.signal.remez`.
- Zero-input limit cycles and their amplitude bound: Jackson 1969. Magnitude truncation's freedom from
  them is checked here exhaustively, not assumed.
- f. harris's `tony_des2.m` / `tonycxx.m`: not read.

## Layout and how to run

From the repository root, in a venv made from the root `requirements.lock` plus `mpmath` (the
`build-*/` names below are ignored by git):

```sh
python3 -m venv build-iir-venv && build-iir-venv/bin/pip install --require-hashes -r requirements.lock
build-iir-venv/bin/pip install mpmath
cd rational/tools/spikes/iir/python
../../../../../build-iir-venv/bin/python gen_designs.py         # designs.json, cpp/designs.h (about 20 min)
../../../../../build-iir-venv/bin/python tables.py              # tables.md, tables.json
../../../../../build-iir-venv/bin/python fourpath.py            # fourpath.json, fourpath.md (hours)
../../../../../build-iir-venv/bin/python gen_cascades.py        # cpp/fourpath_designs.h, icount/cascades.inc
../../../../../build-iir-venv/bin/python constexpr_accuracy.py  # constexpr_accuracy.md
cd .. && B=../../../../build-iir && mkdir -p $B
g++ -std=c++20 -O2 -I../../../../submodules/dsptap/include cpp/float_floor.cpp -o $B/float_floor && $B/float_floor
g++ -std=c++20 -O2 cpp/window_float.cpp -o $B/window_float && $B/window_float
g++ -std=c++20 -O2 fixed_point/fixed_point.cpp -o $B/fixed_point && $B/fixed_point
constexpr/bisect_local.sh g++-13 clang++-18
python3 icount/run_targets.py --plugin /path/to/libinsncount.so   # every target; see its docstring
```

| path | what it is |
|---|---|
| `python/iir_design.py` | the designs and their measurements (the module everything imports) |
| `python/gen_designs.py`, `tables.py`, `fourpath.py`, `gen_cascades.py`, `constexpr_accuracy.py` | the generators of `outputs/` |
| `cpp/kernels.h`, `cpp/designs.h` | floating-point kernels over any arithmetic type; the designs as C++ |
| `cpp/float_floor.cpp`, `cpp/window_float.cpp` | I14's floor; I6's float window, whole stage |
| `fixed_point/fixed_point.cpp` | I8's search, the decay bounds, the bit-true Q15 headroom check |
| `ftz/flush_guard.h`, `ftz/ftz_check.cpp` | I9's guard as v8 specifies it, and its checks on every target |
| `ftz/ftz_bench.cpp`, `guard_branch.cpp`, `guard_arm.cpp` | the plan's scratch programs behind I9's numbers, moved here |
| `ftz/ftz_m33.cpp`, `mxcsr.cpp` | the audit's scratch programs (dsp-4, dsp-14, dsp-15), moved here |
| `constexpr/` | I4: the constexpr designs, the prototype (moved here), the bits, the CMake project for MSVC / AppleClang |
| `icount/` | the counted workloads, their CMake project and runner |
| `outputs/` | every result this README cites |
