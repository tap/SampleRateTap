/// @file guard_band.h
/// @brief guard_band_filter<S, Ch, P>: the 1:1 guard band filter at a chain's low rate, as an odd-order Chebyshev II
/// or a linear-phase type II FIR.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Every half-band crosses over at a quarter of its higher rate, which is the
// low rate's Nyquist: the recursive half-band is -3 dB there (power
// complementary), the FIR Nyquist filter -6 dB. Decimating to a rate r, the
// half-band's transition band (f_pass, r - f_pass) folds onto the guard band
// (f_pass, r / 2), its aliases strongest near Nyquist; interpolating, low-rate
// content in the guard band images into the transition band
// (docs/IIR_PLAN.md 2.3). The passband is alias-free to the profile with or
// without this filter. The guard band filter removes the guard band as a
// whole: a 1:1 stage of its own at the low rate, which a caller places in a
// tap::dsp::chain after a decimator or before an interpolator, FIR or
// recursive (I7, I10; R26, R27). It is not a ratio: ratio<L, M>'s charter
// (L != M) is unchanged, and a 1:1 guard band stage is admitted beside the
// ratios (PLAN.md section 1).
//
// Two forms, the type's P (guard_phase), each with a zero exactly at the low
// rate's Nyquist, where every half-band is weakest:
//   - nonlinear: the odd-order Chebyshev II (DspTap's design_cheby2_lowpass,
//     Antoniou 2006), (n - 1) / 2 biquads and one first-order section whose
//     numerator is b0 (1 + z^-1), run by tap::dsp::biquad_cascade: flat
//     (monotone) within 0.01 dB to its passband edge, A dB down from the stop
//     edge its order sets, the cheaper form. Its phase is nonlinear, so a
//     chain that holds it has a nominal latency and never an exact one;
//   - linear: the even-length symmetric (type II) FIR, linear phase, its
//     zero at Nyquist from its symmetry, run by tap::dsp::dot_row: flat
//     within 0.01 dB peak to peak to the passband edge and A dB down from the
//     same Chebyshev II's stop edge. Its latency is exact, (N - 1) / 2
//     frames, so an all-FIR chain that holds it keeps R7's exact latency.
//
// Settings (guard_band_settings): the passband edge p as a fraction of the
// filter's own rate (the chain's lowest, where it sits: the profile's p), A,
// and the Chebyshev II order, which sets the stop edge of both forms. The
// named settings take the profiles' numbers and the order of I7's rule behind
// the half-band the filter serves, which each names (half_band_kind): the
// lowest odd order whose worst alias in the guard band, after that half-band
// at its own design, is at or below -A/2 (the maintainer's decisions,
// docs/IIR_PLAN.md questions 14 and 20: "The settings can be different for
// the fir and near linear chains than the nonlinear chains"). The rule picks
// 5 / 7 / 7 / 7 behind the nonlinear half-band and 7 / 7 / 7 / 11 behind the
// FIR and the near-linear ones, so economy and balanced are one design behind
// every kind and super_economy and transparent two
// (rational.guard_band.OrderIsTheLowestOddOrderWhoseGuardBandAliasIsAtHalfA
// recomputes every kind's pick from the designs: -40.1 / -43.5 / -40.2 /
// -61.6 dB at the named orders behind the nonlinear half-band, -39.6 / -39.2
// / -37.8 / -64.6 behind the near-linear and -40.9 / -37.0 / -37.0 / -62.1
// behind the FIR):
//
//   | settings      | half-band        | A      | p     | order | stop edge | Chebyshev II   | linear (type II FIR)   |
//   |---------------|------------------|--------|-------|-------|-----------|----------------|------------------------|
//   | super_economy | nonlinear        |  70 dB | 1/3   | 5     | 0.4657    | 3 sections, 15 | 30 taps, 29/2 frames   |
//   | super_economy | FIR, near-linear |  70 dB | 1/3   | 7     | 0.4350    | 4 sections, 20 | 38 taps, 37/2 frames   |
//   | economy       | every kind       |  70 dB | 3/8   | 7     | 0.4530    | 4 sections, 20 | 50 taps, 49/2 frames   |
//   | balanced      | every kind       |  70 dB | 19/48 | 7     | 0.4614    | 4 sections, 20 | 58 taps, 57/2 frames   |
//   | transparent   | nonlinear        | 120 dB | 5/12  | 7     | 0.4862    | 4 sections, 20 | 72 taps, 71/2 frames   |
//   | transparent   | FIR, near-linear | 120 dB | 5/12  | 11    | 0.4669    | 6 sections, 30 | 102 taps, 101/2 frames |
//
// (the stop edge as a fraction of the filter's rate: 21.7 kHz at 48 kHz for
// economy, the zero at 24; the Chebyshev II's multiplies per output frame per
// channel, five per section as the kernel runs them; the FIR's N taps, its
// multiplies, since its dot does not fold its symmetric pairs, and its exact
// latency, in float, double and Q31; the rows in this order are the "named
// settings" every list of six numbers below runs over). In Q15 the linear form
// runs DspTap's Q15 rows (since DspTap 0.4.0) at the four 70 dB settings,
// whose Q1.14 taps keep the tier the double rows lose there (docs/IIR_PLAN.md
// question 21), two taps longer at super_economy and balanced; transparent,
// with no Q15 row, runs the double rows in every format:
//
//   | linear form in Q15 | half-band        | taps (other formats) | latency, frames | Q1.14 stopband | ripple    |
//   |--------------------|------------------|----------------------|-----------------|----------------|-----------|
//   | super_economy      | nonlinear        | 32 (30)              | 31/2 (29/2)     | -89.2 dB       | 0.0090 dB |
//   | super_economy      | FIR, near-linear | 40 (38)              | 39/2 (37/2)     | -70.4 dB       | 0.0098 dB |
//   | economy            | every kind       | 50 (50)              | 49/2            | -72.3 dB       | 0.0097 dB |
//   | balanced           | every kind       | 60 (58)              | 59/2 (57/2)     | -70.7 dB       | 0.0096 dB |
//   | transparent        | nonlinear        | 72 (72)              | 71/2            | -70.9 dB       | 0.0125 dB |
//   | transparent        | FIR, near-linear | 102 (102)            | 101/2           | -69.2 dB       | 0.0148 dB |
//
// so in Q15 taps(), macs_per_output(), latency_output_frames() and
// window_frames() (N - 1) follow these N (the stopband and ripple from the
// stop edge and over the passband on 16385 points each;
// guard_band_fixed_test.QuantizedLinearTablesAttainTheirStatedStopbands,
// guard_band_test.TheLinearFormsLengthLatencyAndWindowArePerFormat). Behind
// the kinds it names, a named setting leaves every alias in the guard band at
// or below -A/2, and behind every half-band the passband and the band past
// the stop edge keep their contracts
// (rational.guard_band.AliasesLandOnlyAboveThePassbandEdge); the nonlinear
// half-band's orders 5 and 7 at super_economy and transparent would leave
// -31.4 and -48.8 dB behind the near-linear half-band and -31.6 and -45.6
// behind the FIR, which is why the settings differ by kind. Where the
// coefficients come from (I4):
//   - nonlinear: the named settings are compile-time constants
//     (tap::dsp::k_guard_band_cheby2: the poles, zeros and pairing constexpr,
//     the L-inf section gains committed, every one exactly 1.0); any other
//     setting designs at construction, at run time
//     (tap::dsp::design_cheby2_lowpass, then cheby2_linf_gains and
//     apply_section_gains, Jackson 1970), within 1e-13 of the compile-time
//     design at a committed spec point;
//   - linear: the named settings run DspTap's committed Parks-McClellan rows
//     (tap::dsp::k_guard_band_fir: attained stopbands -71.4 / -70.9 / -73.3
//     / -71.2 / -123.0 / -120.1 dB, ripple 0.0086 / 0.0091 / 0.0068 / 0.0088
//     / 0.0062 / 0.0093 dB peak to peak), and in Q15 at 70 dB its Q15 rows
//     (tap::dsp::k_guard_band_fir_q15: remez designs with the margin Q1.14
//     needs, at the double row's stop edge); any other setting designs at
//     construction through DspTap's Kaiser designer (design_guard_band_linear
//     at the shortest even length search_guard_band_linear_length finds: A +
//     1 dB on 4097 points, up to 512 taps), which is not equiripple and so
//     runs longer than a committed row at the same spec (82 taps against 64
//     at economy's numbers and order 9), held to its spec by tolerance in
//     double; in Q15 it runs that same design, whose Q1.14 stopband is not
//     stated (Q1.14 rounding costs the double rows the tier, iir_tables.h);
//     validate() states a setting no length up to 512 taps meets.
//
// Contract, as numbers:
//   - 1:1 (k_up = k_down = 1): outputs_for(n) = n and frames_needed(k) = k
//     from any state; zero-primed and causal; pull() is bit-identical to
//     process() on the same stream, for any chunking;
//   - latency: the nonlinear form reports a nominal latency (I5), as a
//     recursive stage does: nominal_latency_output_frames() (the group delay
//     at DC), latency_deviation_output_frames() (the largest stray from it
//     over the passband) and group_delay_output_frames(f) (f in cycles per
//     frame, from the coefficients), never latency_output_frames(); at
//     economy 0.54 frames at DC and 5.04 at the passband edge (deviation
//     4.51; 0.38 to 0.75 at DC and 1.84 to 14.07 of deviation across the
//     named settings, the most at transparent's order 11). The linear form
//     reports latency_output_frames() = (N - 1) / 2 frames, an exact_ratio
//     (49/2 at economy in every format; per format at super_economy and
//     balanced, the Q15 table above), which tap::dsp::chain sums exactly;
//   - flush() feeds window_frames() zeros and writes as many outputs, bit
//     for bit the zero-padded stream's. The linear form's window is its
//     history, N - 1 frames, after which every output is exactly zero. The
//     nonlinear form's is the count to an exactly zero state (I6): floating
//     point, tap::dsp::floating_flush_frames over the largest pole radius
//     (from the format's largest finite state to its smallest normal, plus
//     one frame per section) under the host's flush-to-zero, 1,032 to 3,564
//     frames in float and 8,286 to 28,660 in double at the named settings;
//     fixed point, twice the sum over the sections of each one's
//     magnitude-truncated decay through its pole radius from the full state
//     headroom to one LSB, plus one (the near-linear half-band's rule,
//     iir_stage.h): 420 to 1,750 frames in Q15 and 828 to 3,480 in Q31, where
//     every state is zero within 365 and 802 after full-scale drive. One
//     exception, an honest limit (the maintainer's decision, docs/IIR_PLAN.md
//     question 19, R28): the floating Chebyshev II of order 7 or more holds a
//     zero-input limit cycle under flush-to-zero, from some states and not
//     others: once |a2 y| falls below the smallest normal its product
//     flushes and the section, run for that sample as a first-order
//     recursion with |a1| > 1, lifts the state again (iir_kernels.h). At the
//     named settings' orders 7 and 11 its state then settles below 16 times
//     the smallest normal (measured up to 8.7 times in float and 10.4 in
//     double at order 7, and 8.4 and 7.7 at transparent's order 11, over 20
//     starts per design;
//     rational.guard_band_float_test.TheChebyshevLimitCycleIsBounded; a
//     custom setting of order 7 or more is not measured here, and DspTap's
//     survey found up to 13.1 times, iir_kernels.h), so after flush() the
//     output is that residue, not silence; the stage's
//     flush() still equals its zero padding bit for bit, and a chain whose
//     later stages follow it flushes within the residue of zero padding.
//     Orders 3 and 5 drain to an exactly zero state at every named spec point
//     (so super_economy's behind the nonlinear half-band does), and fixed
//     point always does (magnitude
//     truncation leaves no limit cycle, I8);
//   - flush-to-zero is the host's job (I9), for the nonlinear form only:
//     process(), pull() and flush() check TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED
//     as a debug-build precondition where a control bit governs S
//     (tap/dsp/subnormals.h, which states what a host sets per target); no
//     filter reads-and-writes the environment. The linear form is an FIR and
//     needs nothing: its history empties after one window. Without the bits
//     (or where none governs S: double on the M33, Hexagon) the Chebyshev's
//     state sticks at a few multiples of the smallest subnormal (at most 11
//     measured, bound 32:
//     rational.iir_subnormals.WithoutFlushToZeroTheGuardBandResidueIsBounded);
//   - formats: the nonlinear form as tap/dsp/iir_kernels.h states it (float
//     states and products in transposed direct form II, double the golden
//     model; Q15 and Q31 in direct form I with Q2.30 coefficients, Q2.29 and
//     Q2.60 states, magnitude truncation at each section's output,
//     saturation at the output only). Every valid setting's coefficients lie
//     in Q2.30, so validate() has no format rejection: the denominators by
//     stability (|a1| < 2 r < 2, a2 = r^2 < 1), the numerators below 2 at
//     every point of a grid over p, A and the order (at most 1.99996). Under
//     full-scale drive the largest state is 1.71 of full scale (limit 4), the
//     Q15 output within half an LSB of the double filter's (bound 2) and the
//     Q31 within 1.3e-8, and the Q15 floor is -101.1 dB RMS against double.
//     The linear form as tap/dsp/sample_traits.h states it (float taps
//     accumulated in double; Q15 in Q1.14 taps and Q31 in Q1.30, one
//     rounding, saturating), its table symmetric bit for bit in every format
//     (tap::dsp::quantize_symmetric_row_preserving_sum: the first half
//     quantized with its sum preserved, mirrored), so its zero at Nyquist
//     holds in the quantized taps too.
//     Against the golden leg (scipy): 8.2e-15 / 5.6e-16 of full scale in
//     double, 1.1e-6 / 6.8e-8 in float, 1.5e-5 / 2.5e-4 in Q15 and 5.1e-9 /
//     4.0e-9 in Q31 (Chebyshev II / FIR); the Q15 and Q31 outputs of both
//     forms bit-pinned, the float and double ones tolerances (this tree does
//     not pass -ffp-contract=off);
//   - the constructor designs and allocates; process(), pull(), flush() and
//     reset() are noexcept and allocation-free; one stream per instance, of
//     Ch channels, a compile-time count of at least 1 (D18); the count-
//     independent state and cold code are guard_band_filter_base<S, P>'s,
//     built once per format and form (K12), and process(), pull() and
//     flush() are compiled per count, out of line (A12), each against a
//     cursor;
//   - honest limits: the linear form in Q15 at transparent is format-limited,
//     as rational's FIR stages are in Q15 there: DspTap has no Q15 row at 120
//     dB (Q1.14 cannot carry it), and the double rows' Q1.14 taps attain
//     -70.9 / -69.2 dB from the stop edge (the 70 dB settings' Q15 rows hold
//     the tier, above; Q31 keeps every design: -71.4 / -70.9 / -73.3 / -71.2 /
//     -123.0 / -120.1 dB; at Q15 the Chebyshev II, whose Q2.30 coefficients
//     keep its design, holds each setting's A:
//     guard_band_fixed_test.QuantizedLinearTablesAttainTheirStatedStopbands).
//     Against its design in double, the Q15 linear form's output is -77.8 dB
//     RMS at transparent and -82.4 dB at worst at 70 dB
//     (guard_band.Q15FloorsAgainstDouble); a non-finite input poisons the
//     nonlinear form's state until reset(), and clears from the linear form
//     after its window, as from an FIR stage (I9);
//     a custom setting's fixed-point headroom is not checked (the named
//     settings' is measured: guard_band_fixed_test.
//     FullScaleDriveNeverWrapsInside); the contracts hold without -ffast-math
//     (or /fp:fast).
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "tap/dsp/chain.h"
#include "tap/dsp/detail/expects.h"
#include "tap/dsp/fir_kernels.h"
#include "tap/dsp/iir_design.h"
#include "tap/dsp/iir_kernels.h"
#include "tap/dsp/quantize.h"
#include "tap/dsp/sample_traits.h"
#include "tap/dsp/subnormals.h"

// The same definitions as stage.h's (an identical redefinition is allowed):
// the per-block entry points stay out of line (A12), and a helper is forced
// into its block under clang only.
#if !defined(TAP_SR_RATIONAL_NOINLINE)
#if defined(_MSC_VER)
#define TAP_SR_RATIONAL_NOINLINE __declspec(noinline)
#else
#define TAP_SR_RATIONAL_NOINLINE __attribute__((noinline))
#endif
#endif
#if !defined(TAP_SR_RATIONAL_ALWAYS_INLINE)
#if defined(__clang__)
#define TAP_SR_RATIONAL_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define TAP_SR_RATIONAL_ALWAYS_INLINE inline
#endif
#endif

namespace tap::sr::rational {

    // ANCHOR: rational_guard_phase
    /// The form of a guard band filter (docs/IIR_PLAN.md I7), the type's: see
    /// the file header.
    enum class guard_phase : std::uint8_t {
        nonlinear, ///< the odd-order Chebyshev II: the cheaper form, its phase nonlinear
        linear,    ///< the even-length symmetric (type II) FIR: linear phase, an exact latency
    };
    // ANCHOR_END: rational_guard_phase

    // ANCHOR: rational_half_band_kind
    /// The half-band a guard band filter serves, the one beside it in the
    /// chain (docs/IIR_PLAN.md question 20): the guard band's aliases are that
    /// half-band's, so a named setting's order depends on it (the file header).
    enum class half_band_kind : std::uint8_t {
        fir,         ///< the FIR Nyquist half-band, basic_stage by 2
        nonlinear,   ///< the recursive half-band, iir_phase::nonlinear
        near_linear, ///< the recursive half-band, iir_phase::near_linear
    };
    // ANCHOR_END: rational_half_band_kind

    // ANCHOR: rational_guard_band_settings
    /// What a guard band filter is designed from (the file header): the
    /// passband edge as a fraction of the filter's own rate (a chain's lowest,
    /// where it sits), the stopband A, and the Chebyshev II order, which sets
    /// the stop edge of both forms. The named settings are the profiles'
    /// numbers at I7's order behind the half-band the filter serves, so each
    /// names its half_band_kind; any other is a custom setting, designed at
    /// construction. A default-constructed guard_band_settings is economy's,
    /// the same behind every kind.
    struct guard_band_settings {
        double passband_frac     = 3.0 / 8.0; ///< f_pass over the filter's rate, in (0, 1/2)
        double stopband_atten_db = 70.0;      ///< A, dB, from the stop edge the order sets
        int    order             = 7;         ///< the Chebyshev II order: odd, 3 to 15

        /// 70 dB, flat to 1/3 (16 kHz at 48): order 5 behind the nonlinear
        /// half-band (70 dB from 22.4 kHz), 7 behind the FIR and near-linear
        /// ones (from 20.9).
        static constexpr guard_band_settings super_economy(half_band_kind kind) noexcept {
            return {1.0 / 3.0, 70.0, kind == half_band_kind::nonlinear ? 5 : 7};
        }
        /// 70 dB, flat to 3/8 (18 kHz at 48), order 7 behind every kind (70 dB
        /// from 21.7 kHz); the kind is named all the same.
        static constexpr guard_band_settings economy(half_band_kind /*kind*/) noexcept { return {}; }
        /// 70 dB, flat to 19/48 (19 kHz at 48), order 7 behind every kind (70
        /// dB from 22.1 kHz).
        static constexpr guard_band_settings balanced(half_band_kind /*kind*/) noexcept {
            return {19.0 / 48.0, 70.0, 7};
        }
        /// 120 dB, flat to 5/12 (20 kHz at 48): order 7 behind the nonlinear
        /// half-band (120 dB from 23.3 kHz), 11 behind the FIR and near-linear
        /// ones (from 22.4).
        static constexpr guard_band_settings transparent(half_band_kind kind) noexcept {
            return {5.0 / 12.0, 120.0, kind == half_band_kind::nonlinear ? 7 : 11};
        }

        /// Where both forms reach A, a fraction of the filter's rate: the
        /// order-`order` Chebyshev II's stop edge (tap::dsp::
        /// design_cheby2_lowpass). @pre validate(*this) == nullptr.
        constexpr double stop_edge() const noexcept {
            return tap::dsp::design_cheby2_lowpass(order, passband_frac, stopband_atten_db).stop_edge;
        }
    };
    // ANCHOR_END: rational_guard_band_settings

    /// The texts validate() returns and the constructor raises.
    inline constexpr const char* k_guard_band_settings_reason =
        "tap::sr::rational::guard_band_filter: bad settings (passband_frac in (0, 1/2) of the filter's rate, "
        "stopband_atten_db above the 0.01 dB passband ripple, order odd from 3 to 15, and a finite design)";
    inline constexpr const char* k_guard_band_linear_unmeetable_reason =
        "tap::sr::rational::guard_band_filter: the linear form designs a custom setting at run time (Kaiser, "
        "tap::dsp::design_guard_band_linear), and no even length up to 512 taps meets its stopband from the "
        "Chebyshev II's stop edge at its order";

    /// The reason settings are rejected by either form, or nullptr: finite,
    /// 0 < p < 1/2, A above the 0.01 dB passband ripple, the order odd from 3
    /// to 15 (DspTap's eight sections), and a design whose every coefficient
    /// is finite. noexcept and allocation-free; the form's validate() adds
    /// its own.
    inline const char* validate(const guard_band_settings& s) noexcept {
        if (!(std::isfinite(s.passband_frac) && std::isfinite(s.stopband_atten_db)) || !(s.passband_frac > 0.0)
            || !(s.passband_frac < 0.5) || !(s.stopband_atten_db > 0.01) || s.order < 3
            || s.order > 2 * static_cast<int>(tap::dsp::k_max_cheby2_sections) - 1 || s.order % 2 == 0) {
            return k_guard_band_settings_reason;
        }
        const tap::dsp::cheby2_design d =
            tap::dsp::design_cheby2_lowpass(s.order, s.passband_frac, s.stopband_atten_db);
        for (std::size_t i = 0; i < d.count; ++i) {
            const tap::dsp::iir_section& c = d.sections[i];
            if (!(std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2) && std::isfinite(c.a1)
                  && std::isfinite(c.a2))) {
                return k_guard_band_settings_reason;
            }
        }
        return d.order == 0 || !std::isfinite(d.stop_edge) ? k_guard_band_settings_reason : nullptr;
    }

    namespace detail {

        /// The six distinct named settings, in profile order: super_economy
        /// behind the nonlinear half-band and behind the others, economy,
        /// balanced, transparent behind the nonlinear half-band and behind
        /// the others.
        inline constexpr std::array<guard_band_settings, 6> k_guard_band_named = {
            guard_band_settings::super_economy(half_band_kind::nonlinear),
            guard_band_settings::super_economy(half_band_kind::fir),
            guard_band_settings::economy(half_band_kind::fir),
            guard_band_settings::balanced(half_band_kind::fir),
            guard_band_settings::transparent(half_band_kind::nonlinear),
            guard_band_settings::transparent(half_band_kind::fir)};

        /// Which named setting s is, by number (an index into
        /// k_guard_band_named), or -1 for a custom one.
        inline int find_guard_band_key(const guard_band_settings& s) noexcept {
            for (std::size_t i = 0; i < k_guard_band_named.size(); ++i) {
                const guard_band_settings& n = k_guard_band_named[i];
                if (s.passband_frac == n.passband_frac && s.stopband_atten_db == n.stopband_atten_db
                    && s.order == n.order) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        /// The named settings' Chebyshev II designs, compile-time constants
        /// (I4: constexpr, no libm at construction).
        inline constexpr std::array<tap::dsp::cheby2_design, 6> k_guard_band_cheby2_designs = {
            tap::dsp::k_guard_band_cheby2<70, 1, 3, 5>,   tap::dsp::k_guard_band_cheby2<70, 1, 3, 7>,
            tap::dsp::k_guard_band_cheby2<70, 3, 8, 7>,   tap::dsp::k_guard_band_cheby2<70, 19, 48, 7>,
            tap::dsp::k_guard_band_cheby2<120, 5, 12, 7>, tap::dsp::k_guard_band_cheby2<120, 5, 12, 11>};

        /// The taps of a committed row copied at compile time, so an image
        /// holds the six named rows and not DspTap's whole table.
        template <std::size_t N>
        constexpr std::array<double, N> guard_band_taps(std::span<const double> taps) noexcept {
            std::array<double, N> out{};
            for (std::size_t i = 0; i < N; ++i) {
                out[i] = taps[i];
            }
            return out;
        }
        /// A committed row's taps, the double row's or (Q15) the Q15 row's.
        template <int StopDb, int PassNum, int PassDen, int Order>
        constexpr auto guard_band_fir_copy() noexcept {
            return guard_band_taps<tap::dsp::k_guard_band_fir<StopDb, PassNum, PassDen, Order>.taps.size()>(
                tap::dsp::k_guard_band_fir<StopDb, PassNum, PassDen, Order>.taps);
        }
        template <int StopDb, int PassNum, int PassDen, int Order>
        constexpr auto guard_band_fir_q15_copy() noexcept {
            return guard_band_taps<tap::dsp::k_guard_band_fir_q15<StopDb, PassNum, PassDen, Order>.taps.size()>(
                tap::dsp::k_guard_band_fir_q15<StopDb, PassNum, PassDen, Order>.taps);
        }

        // The copies, one plain inline variable per row rather than a
        // variable template: GCC gives a variable template's instantiations
        // STB_GNU_UNIQUE binding, which a shared library built with hidden
        // visibility still exports (the C ABI's pinned symbol set,
        // rational.Family.ExportedSymbolsArePinned), and a plain inline
        // variable keeps its hidden visibility.
        inline constexpr auto k_row_70_1_3_5       = guard_band_fir_copy<70, 1, 3, 5>();
        inline constexpr auto k_row_70_1_3_7       = guard_band_fir_copy<70, 1, 3, 7>();
        inline constexpr auto k_row_70_3_8_7       = guard_band_fir_copy<70, 3, 8, 7>();
        inline constexpr auto k_row_70_19_48_7     = guard_band_fir_copy<70, 19, 48, 7>();
        inline constexpr auto k_row_120_5_12_7     = guard_band_fir_copy<120, 5, 12, 7>();
        inline constexpr auto k_row_120_5_12_11    = guard_band_fir_copy<120, 5, 12, 11>();
        inline constexpr auto k_row_q15_70_1_3_5   = guard_band_fir_q15_copy<70, 1, 3, 5>();
        inline constexpr auto k_row_q15_70_1_3_7   = guard_band_fir_q15_copy<70, 1, 3, 7>();
        inline constexpr auto k_row_q15_70_3_8_7   = guard_band_fir_q15_copy<70, 3, 8, 7>();
        inline constexpr auto k_row_q15_70_19_48_7 = guard_band_fir_q15_copy<70, 19, 48, 7>();

        /// A named setting's linear row: the committed taps and the stop edge
        /// they match.
        struct guard_band_linear_row {
            std::span<const double> taps;
            double                  stop_edge = 0.0;
        };
        inline constexpr std::array<guard_band_linear_row, 6> k_guard_band_linear_rows = {{
            {k_row_70_1_3_5, tap::dsp::k_guard_band_fir<70, 1, 3, 5>.stop_edge},
            {k_row_70_1_3_7, tap::dsp::k_guard_band_fir<70, 1, 3, 7>.stop_edge},
            {k_row_70_3_8_7, tap::dsp::k_guard_band_fir<70, 3, 8, 7>.stop_edge},
            {k_row_70_19_48_7, tap::dsp::k_guard_band_fir<70, 19, 48, 7>.stop_edge},
            {k_row_120_5_12_7, tap::dsp::k_guard_band_fir<120, 5, 12, 7>.stop_edge},
            {k_row_120_5_12_11, tap::dsp::k_guard_band_fir<120, 5, 12, 11>.stop_edge},
        }};

        /// The named settings' Q15 rows (DspTap's k_guard_band_fir_q15), in
        /// k_guard_band_named's order: the four at 70 dB; none at 120 dB
        /// (empty), where Q15 runs the double row, format-limited (the file
        /// header).
        inline constexpr std::array<std::span<const double>, 6> k_guard_band_linear_q15_taps = {
            std::span<const double>{k_row_q15_70_1_3_5},
            std::span<const double>{k_row_q15_70_1_3_7},
            std::span<const double>{k_row_q15_70_3_8_7},
            std::span<const double>{k_row_q15_70_19_48_7},
            std::span<const double>{},
            std::span<const double>{}};

        /// The design a named linear setting (by number) runs in format S:
        /// its Q15 row in Q15 where there is one, else the double row.
        template <tap::dsp::sample_type S>
        constexpr std::span<const double> guard_band_named_taps(std::size_t key) noexcept {
            if constexpr (std::is_same_v<S, std::int16_t>) {
                if (!k_guard_band_linear_q15_taps[key].empty()) {
                    return k_guard_band_linear_q15_taps[key];
                }
            }
            return k_guard_band_linear_rows[key].taps;
        }

        /// A custom setting's Chebyshev II: the run-time design at unity DC
        /// gain per section, then the L-inf section gains (Jackson 1970).
        inline tap::dsp::cheby2_design guard_band_cheby2(const guard_band_settings& s) noexcept {
            const tap::dsp::cheby2_design unity =
                tap::dsp::design_cheby2_lowpass(s.order, s.passband_frac, s.stopband_atten_db);
            std::array<double, tap::dsp::k_max_cheby2_sections - 1> gains{};
            tap::dsp::cheby2_linf_gains(unity, gains);
            return tap::dsp::apply_section_gains(unity, std::span<const double>(gains.data(), unity.count - 1));
        }

    } // namespace detail

    // ANCHOR: rational_guard_band_base
    /// The count-independent part of a guard band filter over sample format
    /// S in form P (K12): everything but the hot paths, built once per format
    /// and form and shared by every channel count's guard_band_filter<S, Ch,
    /// P>. See the file header.
    template <tap::dsp::sample_type S, guard_phase P>
    class guard_band_filter_base {
      public:
        using sample = S;
        /// The Chebyshev II's recursive state (the nonlinear form).
        using state = tap::dsp::iir_state<S>;
        /// The FIR's tap in the format (the linear form).
        using coeff      = typename tap::dsp::sample_traits<S>::coeff;
        using table_type = tap::dsp::biquad_cascade_table<S>;

        /// 1:1: a guard band filter changes no rate (tap::dsp::sync_stage).
        static constexpr std::size_t k_up        = 1;
        static constexpr std::size_t k_down      = 1;
        static constexpr guard_phase k_phase     = P;
        static constexpr bool        k_is_linear = P == guard_phase::linear;

        // ANCHOR: rational_guard_band_validate
        /// The reason a filter cannot be built from s, or nullptr when it can
        /// (D17): bad settings (validate(s)'s text), and for the linear form a
        /// custom setting no Kaiser design up to 512 taps meets. The named
        /// settings answer at once; a custom linear one runs the length search
        /// construction runs (it allocates, as an FIR stage's search does). The
        /// nonlinear form takes every valid setting in every format: its
        /// coefficients lie in Q2.30 (the file header). The constructor
        /// rejects with the same text through tap::dsp::raise
        /// (std::invalid_argument, or std::runtime_error for the unmeetable
        /// linear design), so a build without exceptions calls this first.
        static const char* validate(const guard_band_settings& s = guard_band_settings{}) noexcept {
            if (const char* why = tap::sr::rational::validate(s)) {
                return why;
            }
            if (detail::find_guard_band_key(s) >= 0) {
                return nullptr;
            }
            if constexpr (k_is_linear) {
                if (tap::dsp::search_guard_band_linear_length(s.passband_frac, s.stop_edge(), s.stopband_atten_db)
                    == 0) {
                    return k_guard_band_linear_unmeetable_reason;
                }
            }
            return nullptr;
        }
        // ANCHOR_END: rational_guard_band_validate

        /// Outputs the next in_frames inputs yield: in_frames.
        std::size_t outputs_for(std::size_t in_frames) const noexcept { return in_frames; }
        /// Inputs the next out_frames outputs need: out_frames.
        std::size_t frames_needed(std::size_t out_frames) const noexcept { return out_frames; }
        /// Input frames of zeros after which the output is silence (the file
        /// header: the history, or the count to an exactly zero state).
        std::size_t window_frames() const noexcept { return m_window; }
        /// Frames flush() writes: window_frames().
        std::size_t flush_output_frames() const noexcept { return m_window; }

        /// Return to the zero-primed initial state (out of line: cold).
        TAP_SR_RATIONAL_NOINLINE void reset() noexcept {
            for (auto& v : m_state) {
                v = state{};
            }
            for (auto& v : m_hist) {
                v = tap::dsp::sample_traits<S>::silence();
            }
            m_end = m_taps == 0 ? 0 : m_taps - 1;
        }

        // ANCHOR: rational_guard_band_latency
        /// The nonlinear form's nominal latency in frames (I5): its group
        /// delay at DC.
        double nominal_latency_output_frames() const noexcept
            requires(P == guard_phase::nonlinear)
        {
            return group_delay_output_frames(0.0);
        }
        /// How far the passband's delay strays from the nominal, at most, in
        /// frames: over 0 to the passband edge on 1025 points.
        double latency_deviation_output_frames() const noexcept
            requires(P == guard_phase::nonlinear)
        {
            const double t0    = group_delay_output_frames(0.0);
            double       worst = 0.0;
            for (int g = 1; g <= 1024; ++g) {
                const double dev =
                    std::fabs(group_delay_output_frames(m_passband * static_cast<double>(g) / 1024.0) - t0);
                worst = dev > worst ? dev : worst;
            }
            return worst;
        }
        /// The group delay at f, cycles per frame, in frames (within the
        /// passband: away from the numerators' zeros).
        double group_delay_output_frames(double f) const noexcept
            requires(P == guard_phase::nonlinear)
        {
            return tap::dsp::sections_group_delay(
                std::span<const tap::dsp::iir_section>(m_design.sections.data(), m_design.count), f);
        }

        /// The linear form's latency, exact: (N - 1) / 2 frames (R7's form,
        /// so tap::dsp::chain sums it with the FIR stages' exactly).
        tap::dsp::exact_ratio latency_output_frames() const noexcept
            requires(P == guard_phase::linear)
        {
            return tap::dsp::exact_ratio{static_cast<std::uint64_t>(m_taps - 1), 2};
        }
        /// latency_output_frames() in seconds at the filter's rate.
        double latency_seconds(double rate_hz) const noexcept
            requires(P == guard_phase::linear)
        {
            return latency_output_frames().value() / rate_hz;
        }
        // ANCHOR_END: rational_guard_band_latency

        /// Multiplies per output frame per channel, as the kernel runs them:
        /// five per section (the first-order one too), or the FIR's N.
        double macs_per_output() const noexcept {
            if constexpr (k_is_linear) {
                return static_cast<double>(m_taps);
            }
            else {
                return static_cast<double>(m_table.multiplies());
            }
        }

        /// The passband edge, a fraction of the filter's rate.
        double passband() const noexcept { return m_passband; }
        /// The stopband attenuation A, dB.
        double stopband_db() const noexcept { return m_stop_db; }
        /// Where the filter reaches A, a fraction of its rate: the Chebyshev
        /// II's stop edge at its order (both forms).
        double stop_edge() const noexcept { return m_stop_edge; }
        /// The Chebyshev II order the settings name (both forms).
        int order() const noexcept { return m_order; }
        /// Whether the design is a named setting's (compile-time or committed)
        /// rather than a run-time one.
        bool tabulated() const noexcept { return m_tabulated; }

        /// The nonlinear form's design, in double.
        const tap::dsp::cheby2_design& design() const noexcept
            requires(P == guard_phase::nonlinear)
        {
            return m_design;
        }
        /// The nonlinear form's table, in its format (tap/dsp/iir_kernels.h).
        const table_type& table() const noexcept
            requires(P == guard_phase::nonlinear)
        {
            return m_table;
        }
        /// The recursive states, every channel's, read-only: the cold-path
        /// accessor for tests (exactly zero after flush() but for the honest
        /// limit, I6).
        std::span<const state> states() const noexcept
            requires(P == guard_phase::nonlinear)
        {
            return m_state;
        }

        /// The linear form's length N, even.
        std::size_t taps() const noexcept
            requires(P == guard_phase::linear)
        {
            return m_taps;
        }
        /// The linear form's design, in double, symmetric bit for bit: a
        /// committed row (in Q15, DspTap's Q15 row at a 70 dB named setting)
        /// or the run-time Kaiser design.
        std::span<const double> design_taps() const noexcept
            requires(P == guard_phase::linear)
        {
            return m_design_taps;
        }
        /// The linear form's taps in the format, symmetric bit for bit.
        std::span<const coeff> coefficients() const noexcept
            requires(P == guard_phase::linear)
        {
            return m_rows;
        }

      protected:
        /// Designs (a custom setting) or looks up (a named one) and allocates
        /// for `channels` interleaved channels; setup time only, out of line.
        /// Rejects what validate() rejects, with its text.
        TAP_SR_RATIONAL_NOINLINE guard_band_filter_base(std::size_t channels, const guard_band_settings& s) {
            if (const char* why = tap::sr::rational::validate(s)) {
                tap::dsp::raise<std::invalid_argument>(why);
            }
            m_passband    = s.passband_frac;
            m_stop_db     = s.stopband_atten_db;
            m_order       = s.order;
            const int key = detail::find_guard_band_key(s);
            m_tabulated   = key >= 0;
            if constexpr (k_is_linear) {
                if (m_tabulated) {
                    const auto                    k    = static_cast<std::size_t>(key);
                    const std::span<const double> taps = detail::guard_band_named_taps<S>(k);
                    m_design_taps.assign(taps.begin(), taps.end());
                    m_stop_edge = detail::k_guard_band_linear_rows[k].stop_edge;
                }
                else {
                    m_stop_edge = s.stop_edge();
                    const std::size_t n =
                        tap::dsp::search_guard_band_linear_length(s.passband_frac, m_stop_edge, s.stopband_atten_db);
                    if (n == 0) {
                        tap::dsp::raise<std::runtime_error>(k_guard_band_linear_unmeetable_reason);
                    }
                    m_design_taps.assign(n, 0.0);
                    tap::dsp::design_guard_band_linear(m_design_taps, s.passband_frac, m_stop_edge,
                                                       s.stopband_atten_db);
                }
                build_rows();
                m_taps     = m_rows.size();
                m_hist_cap = (m_taps - 1) + k_hist_slack;
                m_hist.assign(channels * m_hist_cap, tap::dsp::sample_traits<S>::silence());
                m_window = m_taps - 1;
            }
            else {
                m_design    = m_tabulated ? detail::k_guard_band_cheby2_designs[static_cast<std::size_t>(key)]
                                          : detail::guard_band_cheby2(s);
                m_table     = table_type{m_design};
                m_stop_edge = m_design.stop_edge;
                m_state.assign(channels * m_table.state_size(), state{});
                m_window = window_from();
            }
            m_scratch.assign(k_pop_chunk * channels, tap::dsp::sample_traits<S>::silence());
            reset();
        }

        static constexpr std::size_t k_hist_slack = 64; ///< appends between compactions (the linear form)
        static constexpr std::size_t k_pop_chunk  = 16; ///< pull()'s bulk-pop granularity

        // ANCHOR: rational_guard_band_window
        /// The nonlinear form's window_frames() (the file header): floating
        /// point, the format's largest finite state down to its smallest
        /// normal through the largest pole radius, plus a frame per section;
        /// fixed point, twice (1 plus each section's magnitude-truncated decay
        /// through its pole radius from the full headroom to one LSB, plus a
        /// frame), every state zero within it after full-scale drive
        /// (guard_band_fixed_test.FixedPointSilenceIsExactlyZeroAfterTheWindow).
        std::size_t window_from() const noexcept {
            const std::size_t sections = m_design.count;
            if constexpr (std::is_floating_point_v<S>) {
                // The radii ascend (design_cheby2_lowpass sorts them).
                return tap::dsp::floating_flush_frames<S>(m_design.pole_radius[sections - 1], sections);
            }
            else {
                using tr          = tap::dsp::iir_traits<S>;
                const double span = static_cast<double>(tr::k_state_frac_bits + 2) * std::numbers::ln2;
                std::size_t  sum  = 1;
                for (std::size_t i = 0; i < sections; ++i) {
                    const double r = m_design.pole_radius[i];
                    sum += r <= 0.0 ? std::size_t{1} : static_cast<std::size_t>(std::ceil(span / -std::log(r))) + 1;
                }
                return 2 * sum;
            }
        }
        // ANCHOR_END: rational_guard_band_window

        /// The linear form's table: the design quantized by
        /// tap::dsp::quantize_symmetric_row_preserving_sum (its first half
        /// with the sum preserved, mirrored), so the format's taps are
        /// symmetric bit for bit, the zero at Nyquist exact in the quantized
        /// taps, and their sum within one coefficient LSB of the design's.
        /// Stored oldest-first for dot_row, which for a symmetric row is the
        /// row itself.
        void build_rows() {
            m_rows.assign(m_design_taps.size(), coeff{});
            tap::dsp::quantize_symmetric_row_preserving_sum<S>(m_design_taps, m_rows);
        }

        tap::dsp::cheby2_design m_design{};          ///< nonlinear: the design, double
        table_type              m_table{};           ///< nonlinear: the format's table
        std::vector<double>     m_design_taps;       ///< linear: the design, double
        std::vector<coeff>      m_rows;              ///< linear: the format's taps, symmetric
        double                  m_passband  = 0.0;   ///< p, of the filter's rate
        double                  m_stop_db   = 0.0;   ///< A
        double                  m_stop_edge = 0.0;   ///< of the filter's rate
        int                     m_order     = 0;     ///< the Chebyshev II order
        bool                    m_tabulated = false; ///< a named setting's design
        std::size_t             m_window    = 0;     ///< window_frames()
        std::size_t             m_taps      = 0;     ///< linear: N
        std::size_t             m_hist_cap  = 0;     ///< linear: each channel's history line
        std::vector<state>      m_state;             ///< nonlinear: channels x table.state_size()
        std::vector<S>          m_hist;              ///< linear: channels planar lines of m_hist_cap
        std::vector<S>          m_scratch;           ///< pull()'s staging, interleaved
        std::size_t             m_end = 0;           ///< linear: newest sample index + 1 per line
    };
    // ANCHOR_END: rational_guard_band_base

    // ANCHOR: rational_guard_band_filter
    /// A guard band filter over sample format S in form P, running Ch
    /// interleaved channels (a compile-time count of at least 1): the 1:1
    /// stage a caller places after a decimator or before an interpolator in a
    /// tap::dsp::chain; see the file header. Its state and cold code are
    /// guard_band_filter_base<S, P>'s.
    template <tap::dsp::sample_type S, std::size_t Ch, guard_phase P>
    class guard_band_filter : public guard_band_filter_base<S, P> {
        static_assert(Ch >= 1, "tap::sr::rational: a stage's channel count is at least 1");
        using base = guard_band_filter_base<S, P>;

      public:
        using base::k_down;
        using base::k_is_linear;
        using base::k_up;
        using base::outputs_for;
        using base::window_frames;
        using typename base::coeff;
        using typename base::state;
        using typename base::table_type;

        /// The channel count, the type's: tap::dsp::sync_stage reads it.
        static constexpr std::size_t channels() noexcept { return Ch; }

        /// Designs or looks up the filter and allocates; setup time only.
        /// Rejects what validate() rejects, with its text.
        explicit guard_band_filter(const guard_band_settings& s = guard_band_settings{})
            : base(Ch, s) {}

        /// Push-transform: consumes all in_frames interleaved frames and
        /// writes as many (out must hold them); returns that.
        TAP_SR_RATIONAL_NOINLINE std::size_t process(const S* in, std::size_t in_frames, S* out) noexcept {
            if constexpr (!k_is_linear) {
                TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED(S);
            }
            cursor b = open();
            run<false>(b, in, in_frames, out);
            close(b);
            return in_frames;
        }

        /// Pull: produce exactly out_frames frames, drawing input through
        /// pop(dst, max_frames) -> frames delivered (interleaved; fewer means
        /// the source ran dry and the call returns short, resumable). Bit-
        /// identical to process() on the same stream. PopFn must be noexcept.
        template <typename PopFn>
        TAP_SR_RATIONAL_NOINLINE std::size_t pull(S* out, std::size_t out_frames, PopFn&& pop) noexcept {
            static_assert(std::is_nothrow_invocable_r_v<std::size_t, PopFn&, S*, std::size_t>,
                          "pull() requires a noexcept PopFn: std::size_t(S*, std::size_t) noexcept");
            if constexpr (!k_is_linear) {
                TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED(S);
            }
            cursor      b    = open();
            std::size_t made = 0;
            while (made < out_frames) {
                const std::size_t want = out_frames - made < k_pop_chunk ? out_frames - made : k_pop_chunk;
                const std::size_t got  = pop(b.scratch, want);
                if (got == 0) {
                    break; // dry
                }
                const std::size_t take = got < want ? got : want;
                run<false>(b, b.scratch, take, out + made * Ch);
                made += take;
            }
            close(b);
            return made;
        }

        /// End of stream: feeds window_frames() zeros and writes as many
        /// outputs. The filter is left in the zero-fed state; reset() before
        /// reuse.
        TAP_SR_RATIONAL_NOINLINE std::size_t flush(S* out) noexcept {
            if constexpr (!k_is_linear) {
                TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED(S);
            }
            cursor b = open();
            run<true>(b, nullptr, window_frames(), out);
            close(b);
            return window_frames();
        }

      private:
        using base::k_pop_chunk;
        using base::m_end;
        using base::m_hist;
        using base::m_hist_cap;
        using base::m_rows;
        using base::m_scratch;
        using base::m_state;
        using base::m_table;
        using base::m_taps;

        // ANCHOR: rational_guard_band_cursor
        /// The hot loops' view of the filter for one call (A12): the stream
        /// state and the buffers as locals, loaded by open(), stored back by
        /// close(); the members stay authoritative between calls.
        struct cursor {
            const table_type* table;      ///< nonlinear
            state*            states;     ///< nonlinear
            std::size_t       state_size; ///< nonlinear
            S*                hist;       ///< linear
            const coeff*      rows;       ///< linear
            std::size_t       taps;       ///< linear
            std::size_t       cap;        ///< linear
            std::size_t       end;        ///< linear
            S*                scratch;
        };

        cursor open() noexcept {
            cursor b{};
            b.table      = &m_table;
            b.states     = m_state.data();
            b.state_size = m_table.state_size();
            b.hist       = m_hist.data();
            b.rows       = m_rows.data();
            b.taps       = m_taps;
            b.cap        = m_hist_cap;
            b.end        = m_end;
            b.scratch    = m_scratch.data();
            return b;
        }

        void close(const cursor& b) noexcept { m_end = b.end; }
        // ANCHOR_END: rational_guard_band_cursor

        // ANCHOR: rational_guard_band_run
        /// The block: n frames from in (or silence when Zero), one output
        /// frame per input frame.
        template <bool Zero>
        TAP_SR_RATIONAL_ALWAYS_INLINE static void run(cursor& b, const S* in, std::size_t n, S* out) noexcept {
            std::array<S, Ch> silence{};
            for (std::size_t i = 0; i < n; ++i) {
                const S* frame = Zero ? silence.data() : in + i * Ch;
                S*       y     = out + i * Ch;
                if constexpr (k_is_linear) {
                    append(b, frame);
                    const S* window = b.hist + (b.end - b.taps);
                    for (std::size_t c = 0; c < Ch; ++c) {
                        y[c] = tap::dsp::dot_row<S>(b.rows, window + c * b.cap, b.taps);
                    }
                }
                else {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        y[c] = tap::dsp::biquad_cascade<S>(*b.table, b.states + c * b.state_size, frame[c]);
                    }
                }
            }
        }
        // ANCHOR_END: rational_guard_band_run

        /// Appends one interleaved frame to every channel's history line;
        /// compacts the lines first when they are full.
        TAP_SR_RATIONAL_ALWAYS_INLINE static void append(cursor& b, const S* frame) noexcept {
            if (b.end == b.cap) {
                compact(b);
            }
            for (std::size_t c = 0; c < Ch; ++c) {
                b.hist[c * b.cap + b.end] = frame[c];
            }
            ++b.end;
        }

        /// Slides the newest taps - 1 samples of every line to its front.
        static void compact(cursor& b) noexcept {
            const std::size_t keep = b.taps - 1;
            for (std::size_t c = 0; c < Ch; ++c) {
                S* line = b.hist + c * b.cap;
                std::memmove(line, line + (b.end - keep), keep * sizeof(S));
            }
            b.end = keep;
        }
    };
    // ANCHOR_END: rational_guard_band_filter

    static_assert(tap::dsp::sync_stage<guard_band_filter<float, 1, guard_phase::nonlinear>>);
    static_assert(tap::dsp::sync_stage<guard_band_filter<std::int16_t, 2, guard_phase::linear>>);
    static_assert(tap::dsp::reports_nominal_latency<guard_band_filter<float, 1, guard_phase::nonlinear>>);

} // namespace tap::sr::rational
