# rational — synchronous small-factor L/M within a rate family

[![CI](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/SampleRateTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](../LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**Synchronous sample rate conversion by a small rational factor, inside one
rate family, as fast as possible.**

Ratios L/M with L, M ∈ {2^a · 3^b} and gcd(L, M) = 1 — ↑2, ↓3, 2/3, ↑8,
3/8 … — realised as chains of Nyquist (L-th-band) stages whose structural
zeros cost nothing, and, beside them, recursive (IIR) half-band stages by 2
for the conversions by powers of 2 and a 1:1 guard band filter for the band
every half-band leaves (below). Header-only C++20, built on the Tap family's shared
substrate ([DspTap](https://github.com/tap/DspTap): the L-th-band designer
and the stage composition landed there first, with the float/Q15/Q31
sample-format traits, the dot kernels and the row-sum quantization).

> **Status: M6 of [PLAN.md](PLAN.md) section 6, the plan complete.** The single stages ship
> for every ratio of the vocabulary — ↑2, ↓2, ↑3, ↓3, ↑4, ↓4, ↑6, ↓6, ↑8, ↓8
> and the mixed 3/2, 2/3, 4/3, 3/4, 8/3, 3/8 — as `converter<Ch, ratio<L, M>>` (float,
> the golden model pinned sample-for-sample against committed scipy
> `upfirdn` vectors), `converter_q15` and `converter_q31`, with the family's
> call shapes: `process`, `pull`, exact `outputs_for` / `frames_needed`,
> `flush`, `reset`, the latency as an exact rational. Each stage is one
> L-th-band filter at the pinned length of the M2 design spike
> ([`notebooks/design_spike.ipynb`](notebooks/design_spike.ipynb)), re-pinned
> since with the designer's Kaiser β for A + 4 dB (X17); its
> structural zeros are never multiplied by an interpolator or a decimator,
> so the half-band decimator costs 21 MACs per output of its 39 taps and an
> interpolator's centre phase is a copy. The chains ship too (`chain.h`):
> `basic_chain<S, Ch, R...>` runs stages in sequence with each stage designed at
> its own rate, and the 25 named multi-stage chains of the 14 × 14 coverage
> matrix (PLAN.md section 3), which selects a chain per attenuation tier (the
> 70 dB tier's at `economy`, the 120 dB tier's at `transparent`), pinned row by
> row by `tests/test_matrix.cpp`.
> The fixed-point profiles are measured per stage (`tests/test_fixed_point.cpp`,
> PLAN.md section 6): exact-unity rows and full-scale DC in Q15 and Q31,
> bit-pinned tables, saturation without wrap, Q31 within 2.5e−9 of double on the
> reference noise (480 frames at peak 0.9; 1.5e−8 under full-scale drive);
> Q15 is format-limited, and its numbers are stated per stage — at Q15 use
> `economy`, where every stage of the vocabulary attains 70 dB of stopband at its own rate (a Q15 decimator
> quantizes each branch at unity and divides by M in its one rounding, so ↓6
> and ↓8 attain −71.8 / −72.9 dB, as ↑6 and ↑8 do; a mixed ratio going down
> holds its rows at a power-of-two gain, shifted out the same way; the relaxed designs a chain runs are
> measured too, and since the designer's β was re-tuned (X17) every one of them attains 70 dB as well, PLAN.md section 6). The C ABI, the executed coverage-matrix notebook and
> the instruction-count ratchet landed at M6 (below); since 0.9.0 they cover the
> recursive stages and the guard band filter too (IIR-d). The
> plan is authoritative: charter, the decisions R1–R29, the generated
> matrix, layout, test strategy, non-goals and risks.

## Quick start

```cpp
#include <tap/sr/rational/rational.h>

using namespace tap::sr::rational;

converter<2, down2> down;                  // 96 -> 48 kHz (any by-2), stereo, economy
// profiles: economy() (default, 3/8 of the lower rate flat) | balanced() |
// transparent() (120 dB) | super_economy()
std::vector<float> out(down.outputs_for(n_in) * 2);
std::size_t made = down.process(in, n_in, out.data());   // noexcept, alloc-free
std::vector<float> tail(down.flush_output_frames() * 2);
down.flush(tail.data());                                 // end of stream

converter<1, ratio2over3> to_32k;          // 48 -> 32 kHz: one mixed stage, 29.5 MACs per output
auto lat = to_32k.latency_output_frames(); // exact: {29, 3} output frames = 29 / 3

down4_down4_down3<float, 1> to_8k;         // 384 -> 8 kHz: the matrix's 1/48 chain, 344 MACs per output
// each stage designed at its own rate: divisors 12, 3, 1 of the chain's lowest rate (PLAN.md 3.1)
static_assert(to_8k.k_divisors[0] == tap::dsp::exact_ratio{12, 1});
chain<2, up2, ratio4over3> to_32k_from_12k; // 12 -> 32 kHz: 8/3 as the matrix factors it, stereo

down_by8<float, 2> to_6k;                  // 48 -> 6 kHz by intent: the matrix's 1/8 chain, down4_down2, 71 MACs
up_by8<float, 2>   from_6k;                // and back: up2_up4 (down8 / up8, one stage, is lower latency at more MACs)
down_by16_transparent<float, 1> to_3k(profile::transparent()); // 1/16 at 120 dB: down8_down2, its tier's own chain
```

The ratio is a compile-time type (`ratio<L, M>`, L and M of the form
2^a · 3^b in lowest terms: anything else fails to compile with the charter's
message). The channel count is the type's too, the first template argument
of the aliases (`converter<2, down2>`, `chain<2, up2, ratio4over3>`) and the
second of `basic_stage<S, Ch, R>` / `basic_chain<S, Ch, R...>`: a
compile-time constant of at least 1, never a constructor argument, and a
count of zero fails to compile (A13, `docs/CHANNELS_PLAN.md`). Construction
rejects a profile the designer refuses and a spec no length up to the
search bound meets (`std::invalid_argument`,
`std::runtime_error`); `converter<Ch, R>::validate(profile)` and a
chain's `validate` are the same verdict as a `noexcept` `const char*` reason
(nullptr when it constructs), the family's error model (root README,
"Errors") — what a build without exceptions calls before constructing. `pull(out, n, pop_fn)` is the callback-driven shape and
`frames_needed(n)` is exact arithmetic from the current position.

- `ratio.h` — `ratio<L, M>` (R1) and `ratio_traits`: the band index
  max(L, M) of the stage's one Nyquist filter, the composite factor, the
  lower rate, the direction flags and the 2 / 3 exponents the chain
  factoring reads.
- `design.h` — `profile` (`bridge`'s four names as (A, f_pass / r_min),
  each carrying the pinned taps per branch for the bands 2, 3, 4, 6, 8),
  `design_stage<R>(profile)`: the band's L-th-band design at the pinned
  length (a custom profile without pins is searched on the same
  16384-point grid), centre exactly 1, zeros exactly 0, every branch at DC
  gain 1; `stage_taps_per_phase<R>` for the mixed ratios.
- `stage.h` — `basic_stage<S, Ch, R>`: the L-phase schedule machine (bridge's)
  for interpolators and mixed ratios, each phase row trimmed to its nonzero
  span; the M-branch commutator for decimators, the nonzero branches summed
  under one finalize. Zero-primed and causal (scipy's `upfirdn` streaming
  prefix), bit-identical for any chunking, DC gain exactly 1 in every
  format. Its count-independent state and cold code are `stage_base<S, R>`'s,
  built once per format and ratio whatever counts an image runs (A13's
  K12); only `process`, `pull` and `flush` are compiled per count, each out
  of line.
- `converter.h` — `basic_converter<S, Ch, R>` and the `converter<Ch, R>`,
  `converter_q15<Ch, R>`, `converter_q31<Ch, R>` aliases.
- `chain.h` — `basic_chain<S, Ch, R...>`: DspTap's `chain<>` over
  `basic_stage`, constructing each stage at the profile relaxed by the
  stage's design divisor (its lower rate over the chain's lowest, the
  largest 2^a · 3^b at or below it; `profile::relaxed`, the pinned
  relaxation tables of `design.h`); `chain<Ch, R...>` / `chain_q15` /
  `chain_q31`; `macs_per_output_exact()` the MACs per output as a reduced rational and
  `macs_per_output()` its double; and the 25 named multi-stage
  chains of the coverage matrix (`up2_ratio3over2<S, Ch>` …
  `down4_down6_down2<S, Ch>`): eighteen the 70 dB tier's, selected at
  `economy` and run at `super_economy`, `economy` and `balanced`, and seven
  the 120 dB tier's alone, selected at `transparent` (`up2_ratio3over2`,
  `up2_up6`, `up2_up8`, `up2_up6_up4`, `down6_down2`, `down8_down2`,
  `down4_down6_down2`). Every chain runs at every profile; the tier says
  where the matrix selects it. The chains by 4, 8 and 16 have a second name,
  by intent: `down_by4<S, Ch>`, `down_by8`, `down_by16` and their `up_by`
  twins are the 70 dB tier's chains (`down2_down2`, `down4_down2`,
  `down2_down4_down2`), and `down_by16_transparent` / `up_by16_transparent`
  the 120 dB tier's (`down8_down2`, `up2_up8`), the one ratio whose tiers
  differ. Each is a plain alias written out in the header, the same chain in
  both families, never a lookup; the suffix names the tier a chain is
  cheapest at, not a profile, and every alias runs at every profile. They are
  the cheapest chains (by 16 at `economy`, where its tier is selected;
  `rational.Chain.ByAliasesAreTheCheapestWithinR3` pins the search behind
  them); `down8` beside `down_by8` is one stage, lower latency at twice the
  MACs.
- `rational.h` — the umbrella and `TAP_SR_VERSION_*` (0.8.0 since the ×4 / ×8 / ×16 plan, `../CHANGELOG.md`; 0.7.0 from A13,
  0.6.0 from the audit response, 0.5.0 from M6);
  the named ratios of the vocabulary (`up2` … `ratio3over8`) are `ratio.h`'s.

Against the general-purpose resampler libraries at four ratios of the
vocabulary — ↑2, ↓2, 3/2 and 2/3, against libsamplerate, soxr,
r8brain-free-src and SpeexDSP at the settings each needs to reach `economy`
and `transparent`, and at their best — quality, latency, host throughput and
embedded instruction counts are measured in
[docs/COMPARISON.md](docs/COMPARISON.md) (the notebook behind it:
[notebooks/rational_comparison.ipynb](notebooks/rational_comparison.ipynb);
the family index: [docs/COMPARISON.md](../docs/COMPARISON.md) at the root).

## The low-band round trip

[`examples/low_band_fft.cpp`](examples/low_band_fft.cpp) is what the chains by 16
were named for: a 4096-point FFT with 0.73 Hz bins in the low band of a 48 kHz
signal. It decimates with `down_by16` to 3 kHz, makes a spectral change there with a
4096-point STFT (periodic Hann, overlap 4), and interpolates *the change alone* back
with `up_by16`, adding it to the input delayed by the round trip's exact latency:

```text
out[n] = x[n - T] + up_by16(change(down_by16(x)))[n],   T = 16 tau_down + tau_up
```

The bins the change leaves alone, and everything above the low band, never pass
through the chains, so no aliasing is re-injected and no crossover filter is designed.
T is an integer for every alias. Measured at `economy` (GCC 13, x86-64, double, 12 s of
200 Hz and 205 Hz at −12 dBFS and 5 kHz at −20 dBFS):

| | |
|---|---|
| 4096 points at 48 kHz (11.7 Hz bins) | one peak: 200 and 205 Hz are 0.43 bins apart |
| 4096 points at 3 kHz after `down_by16` (0.73 Hz bins) | two peaks, 46.7 dB of dip between them |
| T | 16 × 189/16 + 189 = 378 samples (7.875 ms) |
| the chains' cost | 17.875 MACs per full-rate sample (143 / 16 each way) |
| an identity change | the input delayed by T, bit for bit |
| removing 205 Hz (the 9 bins around it) | 205 Hz −70.27 dB; 200 Hz and 5 kHz unchanged (0.00 dB); with T ± 1 only −31.43 dB |

`tests/test_low_band.cpp` pins the claims at ×4, ×8 and ×16, every profile and both
float and double, streamed in blocks within about 1 MB of state
(`rational.LowBand.IdentityChangeIsTheInputDelayedBitForBit`,
`RemovalNeedsTheExactDelay`, `ResolvesTonesFiveHertzApartAfterDown16`,
`RemovingOneToneLeavesTheOthers`): a removal at least 60 dB deep at the 70 dB profiles
and 100 dB at `transparent` (by 16 through `down_by16_transparent` /
`up_by16_transparent`), failing one sample early or late (−31.4 dB, which is
2 sin(π f / fs) at 205 Hz), at least 40 dB between the two peaks, and the other tones
within 0.01 dB. The removal there is of a tone on a bin of the low-rate FFT, so the
change takes all of it and what is left is the round trip's ripple: −66.9 dB at worst
at the 70 dB profiles (by 16 at `super_economy`), −119.2 dB at `transparent`. A tone
between bins also leaves the window's tails outside the removed bins: the example's
205 Hz measures −94.4 dB at `transparent`, the change's floor rather than the round
trip's. Resolution is bought with time: a 4096-point frame at 3 kHz is 1.37 s, and a
change spreads over it.

## The recursive stages (IIR)

Beside the FIR stages, `iir_stage.h` and `iir_chain.h` add recursive half-band stages
by 2 (the IIR plan, [`docs/IIR_PLAN.md`](../docs/IIR_PLAN.md); [PLAN.md](PLAN.md)
R21–R25): a stage type of their own, never a profile of the FIR stage (the family's D20).
Each is the two-path allpass half-band H(z) = ½ [A0(z²) + z⁻¹ A1(z²)] (Valenzuela and
Constantinides), its phase variant in the type:

- `iir_phase::nonlinear`: both paths cascades of first-order allpass sections, the
  elliptic (power-complementary) half-band. The fewest multiplies and the lowest delay;
  its group delay rises toward the passband edge.
- `iir_phase::near_linear`: one path a pure delay, the other an allpass that approximates
  it, so the passband is delayed by a whole number of samples within a stated deviation,
  at more multiplies.

```cpp
#include <tap/sr/rational/rational.h>
namespace rat = tap::sr::rational;

// 48 -> 24 kHz, stereo: the nonlinear and the near-linear variant, float and Q15.
rat::iir_converter<2, rat::down2>                 down;            // economy by default
rat::iir_converter_near_linear_q15<2, rat::up2>   up(rat::profile::transparent());
// 48 -> 3 kHz and back: cascades by 16, each half-band relaxed to its divisor.
rat::iir_down_by<float, 1, rat::iir_phase::near_linear, 16> to_low;
rat::iir_up_by<float, 1, rat::iir_phase::near_linear, 16>   from_low;
```

They keep the family's call shapes (`process`, `pull`, exact `outputs_for` /
`frames_needed`, `flush`, `reset`, `validate`), the four profiles at the FIR stages'
numbers, and every profile in both variants. At divisor 1, going down (the tables
`tap/dsp/iir_design.h` and `iir_tables.h` hold, pinned by
`rational.iir_stage_test.MatchesTheDirectFormGoldenLeg` and
`rational.iir_double.EveryStageMeetsItsSpecAtEveryProfileAndDivisor`):

| profile | nonlinear: order, multiplies per output | its delay, output frames (DC … passband edge) | near-linear: order N = multiplies | its delay path, output frames (deviation) |
|---|---:|---:|---:|---:|
| `super_economy` | 9, 4 | 1.27 … 2.11 | 5 | 5 (0.0078) |
| `economy` | 9, 4 | 1.20 … 2.48 | 7 | 7 (0.0087) |
| `balanced` | 9, 4 | 1.16 … 2.77 | 8 | 8 (0.0134) |
| `transparent` | 17, 8 | 2.08 … 5.96 | 19 | 19 (0.0002) |

Against the FIR half-band's 21 MACs per output and 19/2 frames of delay at `economy`
(↓2), and going up the same multiplies over the factor and twice the frames. The
cascades `iir_down_by` / `iir_up_by<S, Ch, P, D>` (D = 2, 4, 8, 16) build each stage at
its design divisor, so a stage further up is shorter: at `economy` 4 / 8 / 16 / 24
multiplies per output going down by 2 / 4 / 8 / 16 (nonlinear) and 7 / 13 / 21 / 29
(near-linear), against 43 / 71 / 143 for `down_by4` … `down_by16`
(`rational.IirChain.CascadesCostTheRelaxedRow`).

- **Latency is nominal** (PLAN.md R23): a recursive stage has no exact
  `latency_output_frames()`; it reports `nominal_latency_output_frames()`,
  `latency_deviation_output_frames()` and `group_delay_output_frames(f)`, which
  `tap::dsp::chain` composes. The near-linear variant also reports its
  `delay_path_output_frames()`, exact, so its cascades enter DspTap's round-trip scaffold
  `tap::dsp::subband_delta` as the FIR chains do: removing a tone of the usable band by
  2 to 16 leaves it −64.5 / −70.0 / −68.6 dB down at `super_economy` / `economy` /
  `balanced` and −114.8 dB at `transparent`
  (`rational.iir_double.RoundTripCancelsToTheProfilesThreshold`, thresholds −60 and
  −100). The nonlinear variant has no delay path, and the scaffold refuses it at compile
  time (`rational.IirStage.RefusedShapesFailToCompileWithTheMessage`).
- **Formats** (R25): float states and products in float, double the golden model, Q15 in
  Q2.29 and Q31 in Q2.60 states (two integer guard bits), saturation at the output only.
  Against the stages' transfer functions in one direct form through
  `scipy.signal.lfilter` (`tools/reference/make_iir_reference_vectors.py`, 64 stages):
  9.6e-14 of full scale in double, 3.8e-7 in float, 1.5e-5 in Q15, 5.2e-9 in Q31, the Q15
  and Q31 outputs bit-pinned. The Q15 floor is the output's own rounding, −100.8 dB RMS.
- **Flush** (R24) feeds `window_frames()` zeros, the count to an exactly zero state, so
  it equals zero padding bit for bit and every output after it is silence.
- **By 4 and by 3.** Every cascade is half-bands. A four-path stage by 4 was measured on
  this machine and does not beat the two half-bands it would replace by 10 % on all three of the M33,
  the M55 and Hexagon ([`tools/spikes/iir/iirb_fourpath/`](tools/spikes/iir/iirb_fourpath/README.md));
  by 3 is planned and not implemented. Both are `static_assert`s that say so.

**Flushing subnormals is the host's job** (R24, the IIR plan's I9). After the input goes
silent a floating recursive state decays into the subnormal range, where it is slow on x86
and some A-class Arm cores and, under round to nearest, never reaches zero. No stage reads
and writes the floating-point environment; each checks it as a debug-build precondition
(`TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED`: an assert in a Debug build, nothing in Release,
nothing under `TAP_DSP_ALLOW_SUBNORMALS`, which must be one setting per image), and
`tap::dsp::subnormals_flushed<S>()` answers the same question for a host. What a host sets
on each audio thread before it runs:

| target | what the host sets | governs |
|---|---|---|
| x86-64 | MXCSR FTZ (bit 15) and DAZ (bit 6): `_mm_setcsr(_mm_getcsr() \| 0x8040)` (`<xmmintrin.h>`) | float and double |
| AArch64 (Apple silicon, Arm Linux, Windows on Arm) | FPCR.FZ (bit 24): `msr fpcr` (GCC, Clang), `_WriteStatusReg` (MSVC) | float and double |
| Cortex-M with an FPU (M4F, M33, M55), AArch32 | FPSCR.FZ (bit 24), and FPDSCR (0xE000EF3C) so exception handlers start with it: `__builtin_arm_set_fpscr`, at reset | float on the M4F and M33; float and double on the M55 |
| Cortex-M without an FPU, double on the M4F and M33, Hexagon | nothing: no bit exists | an honest limit |

Honest limits, each pinned:
- without flush-to-zero, or where no bit governs the type, the state sticks at a few
  multiples of the smallest subnormal (at most 7 measured in float and double, bound 16:
  `rational.iir_subnormals.WithoutFlushToZeroTheResidueIsBounded`, an image of its own),
  so the output after a flush is that residue, not silence;
- the floating near-linear stage at `transparent` and divisor 1 holds a zero-input limit
  cycle at about four times the smallest normal even under flush-to-zero (a second-order
  section's product flushes and the section lifts the state again), so its flush ends in
  that residue (within eight times the smallest normal), and a chain whose later stages
  follow it flushes within the residue of zero padding: the maintainer kept it as an honest
  limit (the IIR plan's question 19, PLAN.md R28), as for the guard band filter's
  Chebyshev II below;
- a non-finite input poisons the recursive state until `reset()` (an FIR stage clears it
  after one window): `rational.iir_float_test.NonFiniteInputPoisonsTheStateUntilReset`;
- the contracts hold without `-ffast-math` (or `/fp:fast`): reassociation moves the float
  outputs past their tolerances, and `-ffinite-math-only` deletes non-finite checks.

## The guard band filter

Every half-band crosses over at a quarter of its higher rate, the low rate's Nyquist, where the
recursive half-band is −3 dB and the FIR one −6 dB: decimating, its transition band folds onto
the *guard band* between the passband edge and Nyquist; interpolating, low-rate content there
images into it. The passband stays alias-free to the profile either way. `guard_band.h` adds a
filter that removes the guard band as a whole (the IIR plan's I7; [PLAN.md](PLAN.md) R26–R28): a
1:1 stage of its own at the low rate, which a caller puts in a `tap::dsp::chain` after a
decimator or before an interpolator, FIR or recursive. It changes no rate, so `ratio<L, M>`'s
charter is unchanged. Its form is in the type:

- `guard_phase::nonlinear`: the odd-order Chebyshev II, flat within 0.01 dB to the passband edge,
  A dB down from the stop edge its order sets, with a zero exactly at Nyquist (its first-order
  section). The cheaper form; its phase is nonlinear, so its latency is nominal, as a recursive
  stage's.
- `guard_phase::linear`: the even-length symmetric (type II) FIR at the same stop edge, flat
  within 0.01 dB peak to peak, its zero at Nyquist from its symmetry. Linear phase: its latency is
  exact, (N − 1) / 2 frames, so an all-FIR chain that holds it keeps its exact latency.

```cpp
#include <tap/sr/rational/rational.h>
namespace rat = tap::sr::rational;

// 48 -> 24 kHz, stereo: the recursive half-band, then the Chebyshev II at 24 kHz.
using filter_t = rat::guard_band_filter<float, 2, rat::guard_phase::nonlinear>;
using down_t   = rat::basic_iir_stage<float, 2, rat::down2, rat::iir_phase::nonlinear>;
tap::dsp::chain<down_t, filter_t> down(down_t{rat::profile::economy()},
                                       filter_t{rat::guard_band_settings::economy(rat::half_band_kind::nonlinear)});
// 16 -> 48 kHz: the linear form, then an FIR stage; the latency stays exact: 49/2 x 3 + 29 = 205/2.
using linear_t = rat::guard_band_filter<float, 1, rat::guard_phase::linear>;
using up3_t    = rat::basic_stage<float, 1, rat::up3>;
tap::dsp::chain<linear_t, up3_t> up(linear_t{}, up3_t{});
```

The settings are the profiles' numbers (`guard_band_settings::super_economy(kind)` …
`transparent(kind)`, A and the passband edge as a fraction of the filter's own rate, the chain's
lowest) at a Chebyshev II order each: the lowest odd order whose worst alias in the guard band,
behind the half-band the filter serves, is at or below −A/2 (the maintainer's rule, the IIR
plan's question 14). That half-band is the `kind` every named setting takes,
`half_band_kind::fir`, `nonlinear` or `near_linear`, since the rule picks 5 / 7 / 7 / 7 behind
the nonlinear half-band and 7 / 7 / 7 / 11 behind the other two (question 20: "The settings can
be different for the fir and near linear chains than the nonlinear chains"); `economy` and
`balanced` are the same design behind every kind, and `guard_band_settings{}` is `economy`'s.
In Q15 the linear form at the four 70 dB settings runs DspTap's Q15 rows
(`k_guard_band_fir_q15`, since DspTap 0.4.0: designs with the margin Q1.14 needs, at the same
stop edge), so its taps, multiplies, latency and window differ there from float, double and Q31's
(the table's last column; the IIR plan's question 21). A custom form is designed at construction
(the Chebyshev II at run time; the linear form by DspTap's Kaiser designer, refused by
`validate()` where no length up to 512 taps meets it, its Q15 stopband not stated). Pinned by `rational.guard_band.EachFormMeetsItsSpec`,
`rational.guard_band_test.MatchesTheGoldenLeg` (scipy's own Chebyshev II design through `sosfilt`,
the FIR's taps through `lfilter`; `tools/reference/make_guard_band_reference_vectors.py`) and
`rational.guard_band.OrderIsTheLowestOddOrderWhoseGuardBandAliasIsAtHalfA`:

| settings | behind the half-band | order | 70 dB (120) from, of the rate (at 48 kHz) | Chebyshev II: multiplies, delay at DC … edge (frames) | linear form: taps = MACs, latency (frames); in Q15 where it differs |
|---|---|---:|---:|---:|---:|
| `super_economy` | nonlinear | 5 | 0.4657 (22.4 kHz) | 15, 0.49 … 2.33 | 30, 29/2; Q15 32, 31/2 |
| `super_economy` | FIR, near-linear | 7 | 0.4350 (20.9 kHz) | 20, 0.75 … 4.12 | 38, 37/2; Q15 40, 39/2 |
| `economy` | every kind | 7 | 0.4530 (21.7 kHz) | 20, 0.54 … 5.04 | 50, 49/2 |
| `balanced` | every kind | 7 | 0.4614 (22.1 kHz) | 20, 0.44 … 5.86 | 58, 57/2; Q15 60, 59/2 |
| `transparent` | nonlinear | 7 | 0.4862 (23.3 kHz) | 20, 0.38 … 7.14 | 72, 71/2 |
| `transparent` | FIR, near-linear | 11 | 0.4669 (22.4 kHz) | 30, 0.64 … 14.71 | 102, 101/2 |

**The offering** (the IIR plan's I10; `rational.iir_composition.TheOfferingTableIsMeasured`): by 2
in each recursive variant, with no guard band filter or either form, as multiplies per output
frame per channel and the chain's nominal latency in output frames (its passband deviation in
parentheses). Going down the filter runs at the output rate; going up at the input rate, half its
multiplies per output. Each variant's rows run the settings behind its own half-band, so the
near-linear rows with a filter run order 7 at `super_economy` and 11 at `transparent`. In Q15
the linear form's rows differ at `super_economy` and `balanced`, where its Q15 rows are two taps
longer; they are the rows marked Q15:

| ↓2 | `super_economy` | `economy` | `balanced` | `transparent` |
|---|---:|---:|---:|---:|
| nonlinear | 4, 1.27 (0.84) | 4, 1.20 (1.28) | 4, 1.16 (1.61) | 8, 2.08 (3.88) |
| nonlinear + Chebyshev II | 19, 1.76 (2.68) | 24, 1.73 (5.79) | 24, 1.60 (7.03) | 28, 2.46 (10.63) |
| nonlinear + linear form | 34, 15.77 (0.84) | 54, 25.70 (1.28) | 62, 29.66 (1.61) | 80, 37.58 (3.88) |
| near-linear | 5, 5 (0.0078) | 7, 7 (0.0087) | 8, 8 (0.0134) | 19, 19 (0.0002) |
| near-linear + Chebyshev II | 25, 5.75 (3.38) | 27, 7.54 (4.52) | 28, 8.44 (5.43) | 49, 19.64 (14.07) |
| near-linear + linear form | 43, 23.5 (0.0078) | 57, 31.5 (0.0087) | 66, 36.5 (0.0134) | 121, 69.5 (0.0002) |
| nonlinear + linear form, Q15 | 36, 16.77 (0.84) | 54, 25.70 (1.28) | 64, 30.66 (1.61) | 80, 37.58 (3.88) |
| near-linear + linear form, Q15 | 45, 24.5 (0.0078) | 57, 31.5 (0.0087) | 68, 37.5 (0.0134) | 121, 69.5 (0.0002) |

| ↑2 | `super_economy` | `economy` | `balanced` | `transparent` |
|---|---:|---:|---:|---:|
| nonlinear | 2, 2.53 (1.68) | 2, 2.40 (2.56) | 2, 2.31 (3.23) | 4, 4.16 (7.75) |
| Chebyshev II + nonlinear | 9.5, 3.51 (5.37) | 12, 3.47 (11.58) | 12, 3.19 (14.06) | 14, 4.93 (21.26) |
| linear form + nonlinear | 17, 31.53 (1.68) | 27, 51.40 (2.56) | 31, 59.31 (3.23) | 40, 75.16 (7.75) |
| near-linear | 2.5, 10 (0.0157) | 3.5, 14 (0.0175) | 4, 16 (0.0268) | 9.5, 38 (0.0005) |
| Chebyshev II + near-linear | 12.5, 11.49 (6.76) | 13.5, 15.07 (9.03) | 14, 16.88 (10.87) | 24.5, 39.27 (28.14) |
| linear form + near-linear | 21.5, 47 (0.0157) | 28.5, 63 (0.0175) | 33, 73 (0.0268) | 60.5, 139 (0.0005) |
| linear form + nonlinear, Q15 | 18, 33.53 (1.68) | 27, 51.40 (2.56) | 32, 61.31 (3.23) | 40, 75.16 (7.75) |
| linear form + near-linear, Q15 | 22.5, 49 (0.0157) | 28.5, 63 (0.0175) | 34, 75 (0.0268) | 60.5, 139 (0.0005) |

The near-linear variant with the linear form keeps an exact delay (its delay path plus the FIR's),
so that chain enters DspTap's round-trip scaffold; the Chebyshev II makes any chain's phase
nonlinear. A mixed chain is the caller's `tap::dsp::chain` (R27): no named IIR chain and no row of
the coverage matrix. A chain that holds a `tap::dsp::chain` (`down_by8`, say) composes and
processes, but cannot `flush()`, since a chain reports no `window_frames()`: write its stages out
flat.

- **The aliases** (`rational.guard_band.AliasesLandOnlyAboveThePassbandEdge`, decimating by 2):
  in the passband −71.4 dB or below at the 70 dB settings and −123.9 dB at `transparent`, with
  or without the filter; past its stop edge −75.1 and −127.2 dB or below with either form,
  against −3 dB (recursive) and −6 dB (FIR) without one. In the guard band between the passband
  and the stop edge the worst alias is at or below −A/2 behind every half-band at its own
  settings: at most −40.2 / −43.8 / −40.6 / −61.6 dB behind the nonlinear half-band, −39.6 /
  −39.5 / −38.2 / −63.5 behind the near-linear one and −41.2 / −37.4 / −37.3 / −60.7 behind the
  FIR. The nonlinear half-band's orders behind the others would leave −31.4 / −31.6 dB at
  `super_economy` and −48.8 / −45.6 dB at `transparent`, above −A/2: why the settings differ.
- **Formats**: the Chebyshev II as DspTap's `biquad_cascade` runs it (float states, Q15 and Q31 in
  Q2.29 / Q2.60 with Q2.30 coefficients, which hold every valid setting); against the golden leg
  8.2e-15 / 1.1e-6 / 1.5e-5 / 5.1e-9 of full scale in double / float / Q15 / Q31, the Q15 floor
  −101.1 dB RMS. The FIR in `sample_traits`' formats, its taps symmetric bit for bit in every one
  (DspTap's `quantize_symmetric_row_preserving_sum`), in Q15 at 70 dB from DspTap's Q15 rows;
  5.6e-16 / 6.8e-8 / 2.5e-4 / 4.0e-9, the Q15 leg through the Q15 rows' own design. Q15 and Q31
  outputs bit-pinned.
- **Flush** writes `window_frames()` frames, bit for bit the zero-padded stream's: the FIR's
  history (N − 1), or the Chebyshev's count to an exactly zero state under flush-to-zero (1,032 to
  3,564 frames in float, 8,286 to 28,660 in double, 420 to 1,750 in Q15, 828 to 3,480 in Q31).

Honest limits, each pinned:
- the floating Chebyshev II of order 7 or more (every named setting but `super_economy` behind
  the nonlinear half-band) holds a zero-input limit cycle under flush-to-zero from some states,
  settling at most 8.7 (float) and 10.4 (double) times the smallest normal at order 7 and 8.4 /
  7.7 at `transparent`'s order 11, bound 16
  (`rational.guard_band_float_test.TheChebyshevLimitCycleIsBounded`): its own flush is still bit
  for bit, the output after it is that residue, and a chain whose later stages follow it
  flushes within the residue of zero padding (the IIR plan's question 19, kept as an honest limit:
  R28). Orders 3 and 5 drain to exactly zero, and fixed point always does;
- without flush-to-zero the Chebyshev's state sticks at most 11 times the smallest subnormal
  (bound 32: `rational.iir_subnormals.WithoutFlushToZeroTheGuardBandResidueIsBounded`); the
  linear form, an FIR, needs no flush-to-zero and no precondition;
- the linear form in Q15 at `transparent` is format-limited, as the FIR stages are in Q15 there:
  DspTap has no Q15 row at 120 dB, and the double rows' Q1.14 taps attain −70.9 / −69.2 dB from
  the stop edge, the output −77.8 dB RMS from its design in double; at Q15 the Chebyshev II holds
  its A. At the 70 dB settings the Q15 rows hold the tier: −89.2 / −70.4 / −72.3 / −70.7 dB from
  the stop edge, flat to 0.0090 / 0.0098 / 0.0097 / 0.0096 dB, −82.4 dB RMS from their design at
  worst (`rational.guard_band_fixed_test.QuantizedLinearTablesAttainTheirStatedStopbands`; the IIR
  plan's question 21, lifted at DspTap 0.4.0);
- a non-finite input poisons the Chebyshev until `reset()` and clears from the linear form after
  its window.

## The boundaries are identity, not policy

- **Within a family only.** The 48 kHz family (8, 12, 16, 24, 32, 48, 96,
  192, 384 kHz) and the 44.1 kHz family (11.025, 22.05, 44.1, 88.2,
  176.4 kHz). Crossing them is the family's [`bridge`](../bridge/README.md)
  engine (147/160), reached by a chain the caller writes — never this
  engine. Absorbing a clock is [`async`](../async/README.md), by
  composition.
- **Never routed to by rate.** `ratio<L, M>` names the number and a chain
  names the stages; there is no `(in_hz, out_hz)` lookup anywhere. The
  plan's coverage matrix documents chains and dispatches nothing.
- **Speed-first.** The ratio is a compile-time type; the stage factoring
  is chosen by MACs (↑4 is two half-bands, and since 0.8.0 a 4th-band stage
  sits at the high-rate end of every chain by 8 and above, where it is the
  cheapest); the default `economy` profile
  takes the speed side of every inaudible trade, with a 120 dB
  `transparent` profile behind the same design path.

## Position in the Tap family

`rational` is one of the three engines of the `tap::sr` family, all built on
the same shared substrate and living in one tree:

```
                    ┌────────────────────────────┐
                    │           DspTap           │  shared substrate (submodules/dsptap)
                    │  kaiser · nyquist design · │
                    │  sample traits (float/Q15/ │
                    │  Q31) · FIR dot kernels ·  │
                    │  row-sum quantization ·    │
                    │  chain<> · analysis        │
                    └──────┬────────┬────────┬───┘
                           │        │        │
            ┌──────────────┴─┐ ┌────┴──────────┐ ┌┴────────────────┐
            │ tap::sr::async │ │ tap::sr::bridge│ │ tap::sr::rational│
            │ absorbs the    │ │ 44.1 ↔ 48 on  │ │ L/M inside one   │
            │ clock (servo)  │ │ one clock      │ │ rate family      │
            └────────────┬───┘ └──┬─────────────┘ └──────────────────┘
                         │        │
                         └── test-only ──  bridge's golden cross-validation
                             (bridge/tests/, bridge/examples/bluetooth_bridge)
```

[DspTap](https://github.com/tap/DspTap) (vendored at `submodules/dsptap`)
provides the L-th-band designer and the stage composition this engine is
built on, with the Kaiser design path, the float/Q15/Q31 sample-format
traits, the FIR dot kernels, the row-sum quantization and the analysis
instruments the three engines share. [`bridge`](../bridge/README.md) is the
sibling for the one cross-family pair, 44.1 ↔ 48 kHz, which a chain through
this engine's stages reaches as a type the caller writes (the coverage
matrix's cross rows, PLAN.md 3.5 / 3.6); [`async`](../async/README.md)
absorbs a clock, by composition. Which engine applies is a property of the
clock topology and the rate pair, never inferred from a float ratio
([the boundaries](#the-boundaries-are-identity-not-policy)). Each engine's
quality tiers are its own ladder, stated with its numbers in the root
README's profile table; this engine's four names are `bridge`'s, at the same
numbers.

## Build

```sh
git clone --recurse-submodules https://github.com/tap/SampleRateTap
cmake -S SampleRateTap -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure -L '^rational$'
```

This engine lives in `rational/` of the SampleRateTap family repository; the
root build configures every engine and the `rational` label selects this
one's tests, including the family's dependency-rule checks for it and the
compile-fail charter test. `cmake -S SampleRateTap/rational` configures it on
its own. The notebooks are committed executed; re-run them with
`jupyter nbconvert --to notebook --execute --inplace notebooks/<name>.ipynb`
in the root `requirements.lock` environment when what they measure changes:
`design_spike.ipynb` (the M2 pins, an independent numpy leg),
`matrix.ipynb` (the coverage matrix through the C ABIs, below), and since
0.9.0 `iir_comparison.ipynb` (the FIR half-band against both recursive
variants, alone and with each guard band form, at every profile: MACs,
latency, the response, the aliases and the images) and
`guard_band_demo.ipynb` (the guard band filter on a logarithmic sweep:
spectrograms on one fixed 0 to −140 dB scale, a waterfall, the worst
aliases and images, the group delays, listening clips written outside the
tree). Both run the shipping C++ through the C ABI, with the measurements
they share in `notebooks/iir_measure.py`, which also draws the book's
Part VII figure (`scripts/book_figures.py guard-band`). One configuration
of the demonstration is a scipy design, labelled as a reference: the
filter before the decimator, at the high rate, is no named setting.

### C ABI

`capi/tap_sr_rational_capi.h` (`-DTAP_SR_BUILD_CAPI=ON`, or
`cmake -S rational/capi -B build_capi` on its own) exposes the chains to FFI
consumers in float and, through `tap_sr_rational_create_format(chain,
profile, format, channels)` with `TAP_SR_RATIONAL_FORMAT_Q15` / `_Q31`, in the
fixed-point profiles (`tap_sr_rational_process_q15` / `_q31` and the matching
flush; a call in another format than the converter's returns 0 and touches
nothing — the shape the siblings' ABIs share). `tap_sr_rational_create(chain, profile, channels)` takes
one of the 33 named within-family chains of the coverage matrix as a
constant (`TAP_SR_RATIONAL_UP2` … `TAP_SR_RATIONAL_DOWN4_DOWN6_DOWN2`),
never a rate. The values are stable: a retired chain's value is never
reused and `create` refuses it (ten were retired in 0.8.0, listed in the
header), so the live values run from 0 to 42 with gaps and
`TAP_SR_RATIONAL_CHAIN_END` (43), one past the largest value ever assigned,
bounds them. The chains by 4, 8 and 16 have their names by intent here too,
`TAP_SR_RATIONAL_DOWN_BY4` … `TAP_SR_RATIONAL_UP_BY16_TRANSPARENT`, each
`#define`d as its chain's constant: a second spelling, never a value of its
own (`CApi.ByConstantsAreTheirChainsConstants`). `tap_sr_rational_create_stage(L, M, profile, divisor_num,
divisor_den, channels)` is one stage of the vocabulary, any of its sixteen
ratios, at a stated design divisor, what a chain through `bridge` composes. Each converter reports its
exact accounting, its latency and its MACs per output as exact rationals,
per-stage design lengths, and the bit-packed family version
(`tap_sr_rational_version()`). The C ABI takes the channel count at run time,
where the C++ types carry it: one and two channels run `basic_chain<S, 1, …>`
and `basic_chain<S, 2, …>`, and any other count that many one-channel
converters behind the handle, the frames split to them and interleaved back,
bit for bit the native type of that count
(`CApi.FanOutIsTheNativeConverterBitForBit`, 3, 6 and 12 channels in every
format; A13's K8, option (b)). `notebooks/tap_sr_rational_py.py` is the
ctypes binding the notebooks measure the shipping C++ through
(`Chain(name, fmt="q15")` for the fixed-point formats).

Since 0.9.0 the recursive stages and the guard band filter are in the C ABI
too, beside the entry points that were there, so no existing symbol changed
(the set grows from 23 to 27, `rational.Family.ExportedSymbolsArePinned`):

- `tap_sr_rational_create_iir_format(direction, factor, phase, profile,
  format, channels)`: a recursive half-band by 2 (`basic_iir_stage`) or its
  cascade by 4, 8 or 16 (`iir_up_by` / `iir_down_by`), with
  `TAP_SR_RATIONAL_DIRECTION_UP` / `_DOWN` and `TAP_SR_RATIONAL_IIR_NONLINEAR`
  / `_NEAR_LINEAR`;
- `tap_sr_rational_create_guard_band_format(form, half_band_kind, profile,
  format, channels)`: the filter at a named setting, with
  `TAP_SR_RATIONAL_GUARD_NONLINEAR` / `_LINEAR` and
  `TAP_SR_RATIONAL_HALF_BAND_FIR` / `_NONLINEAR` / `_NEAR_LINEAR`. Named
  settings only, as the chains are named: no custom design in C;
- `tap_sr_rational_nominal_latency_output_frames(c)` and
  `tap_sr_rational_group_delay_output_frames(c, f)`, doubles that answer for
  every handle: a recursive stage's nominal and group delay (I5), and an
  exact latency's value for the rest.

Every other entry point works on the new handles, in every format and at any
channel count (one and two native, any other a fan-out). The exact
`tap_sr_rational_latency_output_frames` refuses a handle whose C++ type has
no `latency_output_frames()`: a recursive stage or cascade, the near-linear
one included (its delay path is exact, but its passband's delay strays from
it, so the path is its nominal), and the Chebyshev II write 0 / 0, and
`tap_sr_rational_latency_seconds` returns NaN; the linear form reports its
(N − 1) / 2. `tap_sr_rational_stage_taps` reports 0 for a recursive stage
and the Chebyshev II and N for the linear form. The float handles take the
host's flush-to-zero as their C++ types do (I9). `CApi.*` and
`capi_recursive.*` pin every handle bit for bit against its C++ type in
every format. The binding names them `IirStage(direction, factor, phase)`
and `GuardBand(form, half_band)`.

### Embedded targets and the instruction-count ratchet

Every push runs this engine's emulation-sized battery on **Cortex-M33**
(QEMU mps2-an505), **Cortex-M55** (mps3-an547) and **Hexagon**
(qemu-hexagon, static musl), and gates forty fixed workloads — the by-2
and by-3 stages both ways in float and Q15 at `economy`, the Q15 by-4
chain and the Q15 by-8 chain (↓4 · ↓2, the 4th-band stage's, since 0.8.0),
the by-2 pair in float at `transparent` and the 1/16 chain the matrix runs
there (↓8 · ↓2, the 120 dB tier's, since 0.8.0), the 2/3 stage in float and
Q15, the Q15 by-4 chain's construction with its input fixture (its
stream removed, so the chain's row minus it is the streaming loop alone),
and since 0.9.0 twenty-four recursive ones (the `iir_` rows: the recursive
half-band by 2 up and down, in float and Q15, nonlinear and near-linear
(`iir_near_`), at `economy`, alone and with the guard band filter in each
form, `_cheby` and `_linear`, before the interpolator or after the
decimator) — and the worst single call of the ↑2, ↓2 and ↓4 stages in float and Q15,
against committed per-target instruction counts (`bench/baselines.json`,
two-sided ±3 %), measured by the family's shared harness from the
repository root:

```sh
cmake -B build-m55 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
      -DTAP_SR_BUILD_TESTS=OFF -DTAP_SR_BUILD_EXAMPLES=OFF \
      -DTAP_SR_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55 -j
python3 scripts/icount.py --engine rational --target m55 --build-dir build-m55 \
      --plugin libinsncount.so
```

The channel count moved into the type at A13 (`docs/CHANNELS_PLAN.md`), and its gain is
measured in instructions only: no cycle counts on hardware yet (K11, deferred to 0.7.0).
Against the baselines before it, three rows were accepted above gate 0's rule, and shipped
at the M55's `up2_q15_eco` −2.62 % and `up3_q15_eco` −3.70 % (+1.28 and +0.63 % at gate 0)
and the M33's `down3_float_eco` +1.29 % (+1.37 %). One row is gated at a midpoint: the M55's
`ratio23_q15_eco` counts 15,341,916 instructions with exceptions and 14,577,966 without (GCC
allocates the block's registers differently in the two builds, their GIMPLE identical:
`PLAN.md` section 6, the A13 record), so its baseline is the midpoint, 14,959,941, and both
builds sat within ±2.55 % of it. Re-record that row at the midpoint of the two builds' counts,
never with `--update` from one of them. Since X17's re-tune of the designer's β (every row
re-recorded, `PLAN.md` section 6, "The designer's β, X17") it counts 15,140,167 and 14,312,206,
the baseline is their midpoint, 14,726,186, and both builds sit within ±2.81 % of it.
The recursive rows joined at 0.9.0 recorded at the midpoint of the two builds too; theirs
split by 0.07 % at most (Hexagon's float decimators), so both builds sit within that of
them. On the host the recursive ↑2 and ↓2 (`iir_converter`, the nonlinear variant) join the
float converter's rows at 2 and 12 channels (`iir_up2_*`, `iir_down2_*`), the two builds
counting them identically.

<!-- ICOUNT:BEGIN -->
Executed instructions per fixed workload (`rational/bench/icount/`), measured under QEMU with a counting plugin — deterministic, and gated in CI at ±3% against `rational/bench/baselines.json`. A `callmax_` row is the worst single call instead of a total: the most instructions one call executes over that stream (`bench/icount/callmax_main.cpp`):

| Workload | Cortex-M33 | Cortex-M55 | Hexagon |
|---|---:|---:|---:|
| `callmax_down2_float` | 107,977 | 7,605 | 21,543 |
| `callmax_down2_q15` | 6,685 | 5,624 | 3,763 |
| `callmax_down4_float` | 155,453 | 9,708 | 30,609 |
| `callmax_down4_q15` | 8,101 | 6,241 | 5,603 |
| `callmax_up2_float` | 210,879 | 12,524 | 40,873 |
| `callmax_up2_q15` | 11,488 | 9,212 | 5,880 |
| `construct_q15_eco` | 26,681,136 | 1,560,152 | 8,303,115 |
| `down2_down2_q15_eco` | 52,537,300 | 24,216,725 | 19,567,759 |
| `down2_float_eco` | 343,562,765 | 23,353,785 | 66,524,317 |
| `down2_float_tr` | 790,228,053 | 40,912,993 | 142,826,681 |
| `down2_q15_eco` | 45,602,994 | 16,879,879 | 16,905,026 |
| `down3_float_eco` | 434,872,408 | 27,445,097 | 82,591,396 |
| `down3_q15_eco` | 48,745,572 | 17,640,706 | 18,300,152 |
| `down4_down2_q15_eco` | 47,659,519 | 18,466,975 | 16,790,347 |
| `down8_down2_float_tr` | 538,963,615 | 31,889,072 | 104,910,201 |
| `iir_down2_float_eco` | 31,241,063 | 9,094,037 | 12,755,722 |
| `iir_down2_float_eco_cheby` | 40,657,772 | 18,139,896 | 16,194,183 |
| `iir_down2_float_eco_linear` | 782,589,285 | 40,658,318 | 141,036,333 |
| `iir_down2_q15_eco` | 39,868,097 | 15,915,202 | 15,569,550 |
| `iir_down2_q15_eco_cheby` | 59,754,398 | 36,217,373 | 19,719,711 |
| `iir_down2_q15_eco_linear` | 66,963,102 | 25,703,811 | 24,167,321 |
| `iir_near_down2_float_eco` | 35,299,770 | 13,096,186 | 12,721,596 |
| `iir_near_down2_float_eco_cheby` | 44,998,706 | 22,432,988 | 16,159,355 |
| `iir_near_down2_float_eco_linear` | 786,949,395 | 44,951,801 | 141,002,283 |
| `iir_near_down2_q15_eco` | 47,687,367 | 23,277,977 | 15,802,978 |
| `iir_near_down2_q15_eco_cheby` | 67,536,094 | 43,589,076 | 19,953,140 |
| `iir_near_down2_q15_eco_linear` | 74,771,246 | 33,075,626 | 24,398,543 |
| `iir_near_up2_float_eco` | 44,333,322 | 21,513,985 | 15,707,022 |
| `iir_near_up2_float_eco_cheby` | 62,638,973 | 39,779,763 | 22,501,399 |
| `iir_near_up2_float_eco_linear` | 1,547,377,281 | 84,804,474 | 272,135,604 |
| `iir_near_up2_q15_eco` | 79,876,059 | 51,687,956 | 21,142,347 |
| `iir_near_up2_q15_eco_cheby` | 119,162,210 | 92,180,180 | 29,321,383 |
| `iir_near_up2_q15_eco_linear` | 133,595,184 | 71,164,627 | 38,209,769 |
| `iir_up2_float_eco` | 36,764,246 | 15,478,214 | 15,307,906 |
| `iir_up2_float_eco_cheby` | 56,315,074 | 33,842,205 | 22,104,339 |
| `iir_up2_float_eco_linear` | 1,541,051,944 | 78,866,871 | 271,737,330 |
| `iir_up2_q15_eco` | 57,543,822 | 35,895,295 | 21,055,640 |
| `iir_up2_q15_eco_cheby` | 96,931,072 | 75,985,542 | 29,237,953 |
| `iir_up2_q15_eco_linear` | 111,354,568 | 54,969,921 | 38,126,314 |
| `ratio23_float_eco` | 538,739,305 | 30,545,005 | 115,766,432 |
| `ratio23_q15_eco` | 48,128,267 | 14,726,186 | 19,232,880 |
| `up2_float_eco` | 650,842,262 | 38,707,334 | 123,990,515 |
| `up2_float_tr` | 1,542,330,040 | 73,546,460 | 275,750,044 |
| `up2_q15_eco` | 61,032,738 | 28,687,571 | 25,064,644 |
| `up3_float_eco` | 1,243,186,839 | 66,371,040 | 229,335,777 |
| `up3_q15_eco` | 82,905,394 | 42,624,043 | 36,815,849 |

On the host, the float converter in 128-frame blocks at 2 and 12 channels (`rational/bench/icount/host_main.cpp`): instructions executed by the streaming loop alone, counted under callgrind on x86-64 with GCC 13 (`scripts/icount.py --target host`), gated the same way:

| Workload | x86-64 |
|---|---:|
| `down2_float_12ch` | 146,817,769 |
| `down2_float_2ch` | 26,912,629 |
| `down4_float_12ch` | 194,807,923 |
| `down4_float_2ch` | 34,142,581 |
| `iir_down2_float_12ch` | 36,771,773 |
| `iir_down2_float_2ch` | 6,718,523 |
| `iir_up2_float_12ch` | 81,460,526 |
| `iir_up2_float_2ch` | 14,160,025 |
| `up2_float_12ch` | 256,387,855 |
| `up2_float_2ch` | 48,551,143 |
<!-- ICOUNT:END -->

## Footprint

One stage's heap, after construction, is a formula in its geometry (N = 2mB − 1 taps
from `taps()`, T the row length from `row_length()`, R the stored rows `k_rows`, P the
history lines per channel, which is M for a decimator and 1 otherwise; `c` the
coefficient size, `s` the sample size, `ch` channels):

```text
held = R T c                      rows
     + (T + 64) P ch s            history: one planar allocation, P x ch lines, 64 frames of append slack
     + 16 ch s                    pull()'s pop scratch
peak = max(design, 8 N + held)    the double design lives to the end of the constructor
```

A chain holds its stages, a scratch between each pair (`cap_i ch s`, where `cap_i` is
the most a 64-frame chunk can reach stage `i` with, `tap::dsp::chain`) and 64 `ch s`
of zeros for its flush. Its peak depends on the order its stages are constructed in,
which C++ leaves unspecified, so the test reports it and does not pin it. Stages at
`economy`, stereo:

| stage | N | f32 held | f32 peak | Q15 held | Q15 peak | Q31 held | Q31 peak |
|---|---:|---:|---:|---:|---:|---:|---:|
| `up2` | 39 | 960 | 1,272 | 480 | 792 | 960 | 1,272 |
| `down2` | 39 | 1,632 | 1,944 | 816 | 1,128 | 1,632 | 1,944 |
| `up3` | 59 | 1,040 | 1,512 | 520 | 992 | 1,040 | 1,512 |
| `down3` | 59 | 2,384 | 2,856 | 1,192 | 1,664 | 2,384 | 2,856 |
| `up4` | 79 | 1,120 | 1,752 | 560 | 1,192 | 1,120 | 1,752 |
| `down4` | 79 | 3,136 | 3,768 | 1,568 | 2,200 | 3,136 | 3,768 |
| `up6` | 119 | 1,280 | 2,232 | 640 | 1,592 | 1,280 | 2,232 |
| `down6` | 119 | 4,640 | 5,592 | 2,320 | 3,272 | 4,640 | 5,592 |
| `up8` | 159 | 1,440 | 2,712 | 720 | 1,992 | 1,440 | 2,712 |
| `down8` | 159 | 6,144 | 7,416 | 3,072 | 4,344 | 6,144 | 7,416 |
| `ratio3over2` | 59 | 1,040 | 1,512 | 520 | 992 | 1,040 | 1,512 |
| `ratio2over3` | 59 | 1,120 | 1,592 | 560 | 1,132 | 1,120 | 1,592 |
| `ratio4over3` | 79 | 1,120 | 1,752 | 560 | 1,192 | 1,120 | 1,752 |
| `ratio3over4` | 79 | 1,180 | 1,812 | 590 | 1,280 | 1,180 | 1,812 |
| `ratio8over3` | 159 | 1,440 | 2,712 | 720 | 1,992 | 1,440 | 2,712 |
| `ratio3over8` | 159 | 1,700 | 2,972 | 850 | 2,544 | 1,700 | 2,972 |

Across the profiles a stage grows with its pins: `up2` f32 stereo holds 896 / 960 /
1,024 / 1,440 bytes at `super_economy` / `economy` / `balanced` / `transparent`, and
`down2` 1,536 / 1,632 / 1,728 / 2,352. The named chains hold 3.3 KB (`up2_up2`,
`up2_up3`, `up2_ratio3over2`) to 10.9 KB (`up3_up4_up4`) at `economy`, f32 stereo, and up
to 11.8 KB at `transparent`, where the 120 dB tier's own chains hold 4.0 KB
(`up2_ratio3over2`) to 11.3 KB (`up2_up6_up4`) (the pins of the designer's β for A + 4 dB,
X17; the chains since the 4th-band stage and the chain per tier, 0.8.0).

The figures are bytes, and the formula has no pointer-sized term (the history is one
allocation since the roadmap's A12; before it, each channel's each line was a vector of
its own, `P ch sizeof(std::vector)` more), so a 32-bit target holds the same.
`tests/test_footprint.cpp` pins the stage formulas, held and peak, for all 16 ratios
of the vocabulary, every profile and format, and every named chain's held bytes,
against DspTap's heap meter (`tap/dsp/analysis/heap_meter.h`) exactly, on the hosted legs
(not under ASan or TSan, whose runtime owns the allocator) and on Hexagon (32-bit), and checks that processing allocates nothing. On Windows x64 the allocator asks for more than the formula: MSVC's `std::allocator` pads every
request of 4096 bytes or more by 39 bytes to align it to 32 bytes, and the test counts each
allocation through DspTap's model of that (`heap_meter::allocator_request`).

### Code size and stack

The code a deployment links and the stack each call needs are gated beside the
instruction counts, by `scripts/ceilings.py` against `bench/ceilings.json`, on one
probe per format (`bench/icount/probe_main.cpp`): the by-2 pair at the default
profile, stereo, an `up2` stage and a `down2` stage each streaming 32-frame blocks
through `process()` (the `up` and `down` calls), built Release in the ratchet's tree;
and, since 0.8.0, a second probe per format the same way for the 4th-band pair, an
`up4` and a `down4` stage (the rows marked "by 4"); and since 0.9.0 three more, the
recursive half-band pair `up2` / `down2` in the nonlinear variant ("IIR"), in the
near-linear one ("near-linear IIR"), and the nonlinear pair with the guard band
filter's Chebyshev II before the interpolator and after the decimator ("IIR + guard
band"), each a `tap::dsp::chain`.

- **Engine `.text` and `.rodata`** are the probe's own translation unit, from its
  link map: the two stages as instantiated.
- **Image** is the whole linked probe's `.text` + `.rodata`. That includes the runtime
  it pulls in (newlib or musl, the C++ library, libgcc's soft double, libm) and the
  startup.
- **Static worst path** (M33) is the deepest chain of frames from the call. It takes
  GCC's frames (`-fstack-usage -fcallgraph-info=su`) for the compiled code and the
  disassembly's for the precompiled runtime routines the call reaches, so it bounds
  every path.
- **Measured high-water** (M33) is what the call used, painted and read under QEMU
  over a second of blocks. It is never above the bound; the script fails if it is.

Each ceiling is the recorded figure plus 3 %, rounded up to 64 bytes for sizes or 8
for stack. A change that moves a figure past its ceiling re-records the file in the
same PR, with the reason.

<!-- ceilings:begin (scripts/ceilings.py docs; do not edit by hand) -->
| target | format | engine `.text` (bytes) | ceiling | engine `.rodata` (bytes) | ceiling | image (bytes) | ceiling |
|---|---|--:|--:|--:|--:|--:|--:|
| m33 | f32 | 9,288 | 9,600 | 1,050 | 1,088 | 43,849 | 45,184 |
| m33 | q15 | 10,776 | 11,136 | 1,050 | 1,088 | 45,545 | 46,912 |
| m33 | q31 | 10,492 | 10,816 | 1,050 | 1,088 | 45,249 | 46,656 |
| m33 | f32 (by 4) | 9,308 | 9,600 | 978 | 1,024 | 43,785 | 45,120 |
| m33 | q15 (by 4) | 10,836 | 11,200 | 978 | 1,024 | 45,529 | 46,912 |
| m33 | q31 (by 4) | 10,512 | 10,880 | 978 | 1,024 | 45,209 | 46,592 |
| m33 | f32 (IIR) | 40,196 | 41,408 | 3,296 | 3,456 | 67,037 | 69,056 |
| m33 | q15 (IIR) | 41,164 | 42,432 | 3,312 | 3,456 | 66,509 | 68,544 |
| m33 | q31 (IIR) | 41,656 | 42,944 | 3,312 | 3,456 | 66,997 | 69,056 |
| m33 | f32 (near-linear IIR) | 5,260 | 5,440 | 2,384 | 2,496 | 30,673 | 31,616 |
| m33 | q15 (near-linear IIR) | 6,864 | 7,104 | 2,384 | 2,496 | 32,793 | 33,792 |
| m33 | q31 (near-linear IIR) | 7,392 | 7,616 | 2,384 | 2,496 | 33,321 | 34,368 |
| m33 | f32 (IIR + guard band) | 75,448 | 77,760 | 6,171 | 6,400 | 114,537 | 118,016 |
| m33 | q15 (IIR + guard band) | 76,784 | 79,104 | 6,187 | 6,400 | 115,721 | 119,232 |
| m33 | q31 (IIR + guard band) | 77,732 | 80,064 | 6,187 | 6,400 | 116,673 | 120,192 |
| m55 | f32 | 8,084 | 8,384 | 1,050 | 1,088 | 43,857 | 45,184 |
| m55 | q15 | 9,480 | 9,792 | 1,050 | 1,088 | 45,561 | 46,976 |
| m55 | q31 | 9,260 | 9,600 | 1,050 | 1,088 | 45,345 | 46,720 |
| m55 | f32 (by 4) | 8,036 | 8,320 | 978 | 1,024 | 43,737 | 45,056 |
| m55 | q15 (by 4) | 9,524 | 9,856 | 978 | 1,024 | 45,537 | 46,912 |
| m55 | q31 (by 4) | 9,276 | 9,600 | 978 | 1,024 | 45,289 | 46,656 |
| m55 | f32 (IIR) | 28,656 | 29,568 | 3,296 | 3,456 | 55,409 | 57,088 |
| m55 | q15 (IIR) | 29,372 | 30,272 | 3,312 | 3,456 | 53,189 | 54,848 |
| m55 | q31 (IIR) | 29,964 | 30,912 | 3,312 | 3,456 | 53,781 | 55,424 |
| m55 | f32 (near-linear IIR) | 5,148 | 5,312 | 2,384 | 2,496 | 29,673 | 30,592 |
| m55 | q15 (near-linear IIR) | 6,468 | 6,720 | 2,384 | 2,496 | 31,073 | 32,064 |
| m55 | q31 (near-linear IIR) | 7,220 | 7,488 | 2,384 | 2,496 | 31,825 | 32,832 |
| m55 | f32 (IIR + guard band) | 52,484 | 54,080 | 6,171 | 6,400 | 87,489 | 90,176 |
| m55 | q15 (IIR + guard band) | 53,288 | 54,912 | 6,187 | 6,400 | 88,313 | 91,008 |
| m55 | q31 (IIR + guard band) | 54,384 | 56,064 | 6,187 | 6,400 | 89,409 | 92,096 |
| hexagon | f32 | 19,700 | 20,352 | 428 | 448 | 297,048 | 305,984 |
| hexagon | q15 | 22,008 | 22,720 | 428 | 448 | 299,992 | 308,992 |
| hexagon | q31 | 20,888 | 21,568 | 428 | 448 | 298,904 | 307,904 |
| hexagon | f32 (by 4) | 22,356 | 23,040 | 356 | 384 | 299,664 | 308,672 |
| hexagon | q15 (by 4) | 26,068 | 26,880 | 356 | 384 | 303,984 | 313,152 |
| hexagon | q31 (by 4) | 23,952 | 24,704 | 356 | 384 | 301,904 | 310,976 |
| hexagon | f32 (IIR) | 23,192 | 23,936 | 2,772 | 2,880 | 284,752 | 293,312 |
| hexagon | q15 (IIR) | 24,728 | 25,472 | 2,772 | 2,880 | 279,488 | 287,936 |
| hexagon | q31 (IIR) | 22,640 | 23,360 | 2,772 | 2,880 | 277,376 | 285,760 |
| hexagon | f32 (near-linear IIR) | 12,564 | 12,992 | 916 | 960 | 272,176 | 280,384 |
| hexagon | q15 (near-linear IIR) | 12,956 | 13,376 | 916 | 960 | 274,168 | 282,432 |
| hexagon | q31 (near-linear IIR) | 15,724 | 16,256 | 916 | 960 | 276,984 | 285,312 |
| hexagon | f32 (IIR + guard band) | 34,080 | 35,136 | 5,172 | 5,376 | 313,904 | 323,328 |
| hexagon | q15 (IIR + guard band) | 37,316 | 38,464 | 5,172 | 5,376 | 317,168 | 326,720 |
| hexagon | q31 (IIR + guard band) | 34,944 | 36,032 | 5,172 | 5,376 | 314,736 | 324,224 |

| M33 call | format | static worst path (bytes) | ceiling | measured high-water (bytes) | ceiling |
|---|---|--:|--:|--:|--:|
| `up` | f32 | 160 | 168 | 160 | 168 |
| `up` | q15 | 144 | 152 | 128 | 136 |
| `up` | q31 | 144 | 152 | 144 | 152 |
| `up` | f32 (by 4) | 160 | 168 | 160 | 168 |
| `up` | q15 (by 4) | 144 | 152 | 128 | 136 |
| `up` | q31 (by 4) | 144 | 152 | 144 | 152 |
| `up` | f32 (IIR) | 72 | 80 | 72 | 80 |
| `up` | q15 (IIR) | 96 | 104 | 96 | 104 |
| `up` | q31 (IIR) | 120 | 128 | 116 | 120 |
| `up` | f32 (near-linear IIR) | 112 | 120 | 108 | 112 |
| `up` | q15 (near-linear IIR) | 144 | 152 | 140 | 152 |
| `up` | q31 (near-linear IIR) | 200 | 208 | 196 | 208 |
| `up` | f32 (IIR + guard band) | 96 | 104 | 96 | 104 |
| `up` | q15 (IIR + guard band) | 128 | 136 | 124 | 128 |
| `up` | q31 (IIR + guard band) | 208 | 216 | 208 | 216 |
| `down` | f32 | 184 | 192 | 184 | 192 |
| `down` | q15 | 184 | 192 | 164 | 176 |
| `down` | q31 | 160 | 168 | 160 | 168 |
| `down` | f32 (by 4) | 200 | 208 | 200 | 208 |
| `down` | q15 (by 4) | 208 | 216 | 188 | 200 |
| `down` | q31 (by 4) | 184 | 192 | 184 | 192 |
| `down` | f32 (IIR) | 72 | 80 | 72 | 80 |
| `down` | q15 (IIR) | 96 | 104 | 92 | 96 |
| `down` | q31 (IIR) | 120 | 128 | 116 | 120 |
| `down` | f32 (near-linear IIR) | 112 | 120 | 112 | 120 |
| `down` | q15 (near-linear IIR) | 152 | 160 | 152 | 160 |
| `down` | q31 (near-linear IIR) | 208 | 216 | 204 | 216 |
| `down` | f32 (IIR + guard band) | 96 | 104 | 96 | 104 |
| `down` | q15 (IIR + guard band) | 120 | 128 | 116 | 120 |
| `down` | q31 (IIR + guard band) | 208 | 216 | 208 | 216 |
<!-- ceilings:end -->

## Non-finite input and denormals

A NaN or ±Inf input sample corrupts only the outputs whose windows, stage by stage, cover it, on
its own channel, and every output after them is bit-identical to a chain's that never saw it,
because each stage is a pure FIR with nothing computed from a sample's value
(`non_finite_test.*`, `tests/test_non_finite.cpp`, in float and double: a 24-frame burst reaches
110 outputs through ↑3, 31 through ↓2 and 343 through the 8/1 chain, ↑2 · ↑4). The float datapath accumulates in double, where every float denormal is a normal number, so no
denormal arises inside a dot product; the output, rounded back to float, may carry denormals onward
(a decaying tail, a denormal input passed through), and the converter does not flush them. Q15 and Q31
have neither. The recursive stages are different on both counts, and
[the recursive stages](#the-recursive-stages-iir) say how: a non-finite input poisons
their state until `reset()`, and their subnormals are the host's to flush.
