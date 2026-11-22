/// @file iir_stage.h
/// @brief basic_iir_stage<S, Ch, R, P>: a recursive (IIR) polyphase stage by 2, both phase variants, and its aliases.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// A recursive stage is a stage type of its own beside basic_stage (the
// family's D20, docs/IIR_PLAN.md I2): not a profile of the FIR stage, whose
// machine, state and contracts differ, and not an engine. It converts by 2
// with the two-path allpass half-band (Valenzuela and Constantinides 1983),
//
//   H(z) = 1/2 [A0(z^2) + z^-1 A1(z^2)],
//
// in one of two phase variants, the type's P (iir_phase):
//   - nonlinear: A0 and A1 cascades of first-order allpass sections
//     (a + z^-2) / (1 + a z^-2), the elliptic (bireciprocal) half-band:
//     power complementary, the fewest multiplies and the lowest delay, its
//     group delay rising toward the passband edge;
//   - near_linear: A0 the pure delay z^-2K and A1 an allpass of order N that
//     approximates z^-(2K - 1) (Renfors and Saramaki 1987's structure), so
//     the passband delay is 2K higher-rate samples within a stated deviation,
//     at 1.25 to 2.4 times the multiplies.
// The designs, the per-format tables and the step
// kernels are DspTap's (tap/dsp/iir_design.h, iir_tables.h, iir_kernels.h);
// this header is the stage machine around them: the commutator and its
// latches, the near-linear delay path's history, the stream's phase, the
// channels, outputs_for / frames_needed, flush and the latency members, as
// basic_stage is the machine around dot_row.
//
// The machines (one channel shown; Ch interleaved channels share the table):
//   - down2: output k as input 2k arrives (decimate.h's and basic_stage's
//     alignment: n inputs give ceil(n / 2) outputs from reset), y[k] = 1/2
//     [A0(x[2k]) + A1(x[2k - 1])], x[< 0] = 0: the odd input waits in a
//     one-frame latch. near_linear: y[k] = 1/2 [x[2(k - K)] + A1(x[2k - 1])],
//     the even inputs' last K in a ring.
//   - up2: each input x[n] writes y[2n] = A0(x[n]) and y[2n + 1] = A1(x[n])
//     (the interpolator's gain of 2 included); near_linear: y[2n] = x[n - K].
// An interpolator computes its two outputs per input at once; pull() banks
// the one a call does not take (at most L - 1, the bound tap::dsp::chain
// allows a stage's pull, which is why a chain hands its stages out
// read-only).
//
// Profiles (I3): the family's four names at the same (A, passband edge)
// numbers as the FIR stages, so economy is 70 dB flat to 3/8 of the
// chain's lowest rate in every stage kind, and every one reachable by both
// variants. A stage inside a cascade runs profile::relaxed(d) at its design
// divisor d, exactly as an FIR stage does (chain.h; I13). Where the
// coefficients come from (I4):
//   - nonlinear: the named profiles at the divisors 1, 2, 4 and 8 are
//     compile-time designs (tap::dsp::k_nonlinear_halfband, Gazsi 1985's
//     explicit formulas, constexpr: no design and no libm at construction);
//     any other profile (a custom one, or a named one at another divisor)
//     calls the same function at construction, at run time;
//   - near_linear: the named profiles at the divisors 1, 2, 4 and 8, from
//     DspTap's committed tables (an iterative design); any other profile is
//     rejected, as validate() states.
// Sizes at divisor 1, multiplies per decimated output (I3, IIR-0, the
// committed designs): nonlinear 4 / 4 / 4 / 8 at super_economy / economy /
// balanced / transparent (orders 9, 9, 9, 17), near_linear 5 / 7 / 8 / 19.
//
// Contract, as numbers:
//   - zero-primed and causal; outputs_for(n) and frames_needed(k) exact
//     from the current position; pull() with a source delivering exactly
//     frames_needed(k) frames yields exactly k outputs and is bit-identical
//     to process() on the same stream, for any chunking; from any state
//     process() or flush() leaves, outputs_for(n) <= floor(n L / M) + 2;
//   - latency (I5): a recursive stage has no exact latency, so it never
//     provides latency_output_frames(); it reports
//     nominal_latency_output_frames() (nonlinear: the group delay at DC;
//     near_linear: its delay path), latency_deviation_output_
//     frames() (how far the passband's delay strays from the nominal, at
//     most) and group_delay_output_frames(f) (f in cycles per output frame,
//     from the coefficients, tap::dsp::reports_nominal_latency), all in its
//     own output frames, so tap::dsp::chain composes them. The near_linear
//     variant also reports delay_path_output_frames(), exact: K output
//     frames going down, 2K going up, which tap::dsp::subband_delta takes as
//     T (an exact_delay_stage); the nonlinear variant has none, so the
//     round-trip scaffold refuses it at compile time (X10). At economy,
//     divisor 1: nonlinear down2 1.20 (DC) to 2.48 frames at the passband
//     edge, near_linear down2 exactly 7 frames within 0.0088;
//   - flush() feeds window_frames() zeros and writes the outputs that become
//     ready, flush_output_frames() of them, and is bit for bit zero padding
//     (I6): window_frames() is the input count after which the state is
//     exactly zero, so every later output is silence. Floating point:
//     tap::dsp::floating_flush_frames over the stage's largest pole radius
//     (the format's largest finite state down to its smallest normal, plus
//     one frame per section) under the host's flush-to-zero; fixed point:
//     the floor-truncated decay from the full state headroom (I8), per path
//     1 plus the sum of each section's longest decay, exactly as IIR-0
//     bounds it for first-order sections, and for the near_linear second-
//     order sections the measured worst decay from full-scale drive with a
//     factor of two over it (rational.IirStage.FixedPointSilenceIsExactlyZero
//     AfterTheWindow); plus the latches and, near_linear, the delay path;
//   - flush-to-zero is the host's job (I9): no stage reads-and-writes the
//     floating-point environment; process(), pull() and flush() check
//     TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED(S) as a debug-build precondition
//     where a control bit governs S (tap/dsp/subnormals.h; nothing in a
//     release build, nothing under TAP_DSP_ALLOW_SUBNORMALS, which must be
//     one setting per image). What a host sets, per target: x86-64 MXCSR FTZ
//     | DAZ (_mm_setcsr(_mm_getcsr() | 0x8040)); AArch64 FPCR.FZ; Cortex-M
//     with an FPU and AArch32 FPSCR.FZ (and FPDSCR), at reset. Honest limit:
//     without it, or where no bit governs S (double on the M4F and M33, the
//     soft-float M4, Hexagon), the state sticks at a few multiples of the
//     smallest subnormal and the output after the flush window is that
//     residue, not silence (the bound pinned by
//     rational.IirSubnormals.WithoutFlushToZeroTheResidueIsBounded), and on
//     x86 the silence after signal runs about ten times slower;
//   - formats (tap/dsp/iir_kernels.h, I8, I14): float states and products
//     in the float profile, double the golden model; Q15 in Q2.29 states and
//     Q31 in Q2.60 (two integer guard bits), floor truncation in the
//     one-multiplier sections and magnitude truncation in the second-order
//     ones, the difference nodes in the 64-bit accumulator, saturation at
//     the output only. A full-scale square wave or clipped sine never takes
//     a state past its headroom at any profile (rational.IirStage.
//     FullScaleDriveNeverWrapsInside); the Q15 output is the double stage
//     rounded to Q15 within one LSB;
//   - the constructor designs (a custom nonlinear profile) and allocates;
//     process(), pull(), flush() and reset() are noexcept and
//     allocation-free; one stream per instance, of Ch channels, a
//     compile-time count of at least 1 (D18);
//   - the count-independent state and cold code (the design, the table, the
//     states' allocation, validate(), reset(), the accounting and the
//     latency members) are iir_stage_base<S, R, P>'s, built once per format,
//     ratio and variant (K12); process(), pull() and flush() are compiled
//     per count, out of line (A12, TAP_SR_RATIONAL_NOINLINE), each running
//     against a cursor of the stream state;
//   - honest limits: a non-finite input poisons the recursive state until
//     reset() (an FIR stage clears a burst after one window; I9, pinned by
//     rational.IirStage.NonFiniteInputPoisonsTheStateUntilReset); the
//     contracts hold without -ffast-math (or /fp:fast), whose reassociation
//     moves float outputs past the stated tolerances; the float and double
//     outputs are tolerances against the golden model, never bits (this
//     tree does not pass -ffp-contract=off), the Q15 and Q31 outputs bits.
//
// Ratios: up2 / down2, in both variants. Anything else is a static_assert
// whose message says why: by 3 (up3 / down3, the three-path structure) is
// planned by docs/IIR_PLAN.md and not implemented, and the four-path stage by
// 4 was measured on this machine over DspTap's kernels and does not ship
// (I12: it did not beat the two half-bands it would replace by R3's 10 % on
// the M33, the M55 and Hexagon; the record is docs/IIR_PLAN.md's).
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "tap/dsp/chain.h"
#include "tap/dsp/detail/expects.h"
#include "tap/dsp/iir_design.h"
#include "tap/dsp/iir_kernels.h"
#include "tap/dsp/sample_traits.h"
#include "tap/dsp/subnormals.h"
#include "tap/sr/rational/design.h"
#include "tap/sr/rational/ratio.h"

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

    // ANCHOR: rational_iir_phase
    /// The phase variant of a recursive stage (docs/IIR_PLAN.md I2), the
    /// type's: see the file header.
    enum class iir_phase : std::uint8_t {
        nonlinear,   ///< elliptic two-path: fewest multiplies, delay rising to the band edge
        near_linear, ///< one path a pure delay: the passband delayed by an exact delay path, within a deviation
    };
    // ANCHOR_END: rational_iir_phase

    namespace detail {

        /// The ratios a recursive stage runs (the file header).
        template <rational_ratio R>
        inline constexpr bool k_iir_by2 = std::is_same_v<R, up2> || std::is_same_v<R, down2>;

        /// The named profiles' spec points as numbers (A, p = num / den), in
        /// profile order (super_economy, economy, balanced, transparent), and
        /// the design divisors a power-of-2 cascade uses (I13): the keys of
        /// DspTap's designs.
        struct iir_spec_point {
            int stop_db;
            int pass_num;
            int pass_den;
        };
        inline constexpr std::array<iir_spec_point, 4> k_iir_spec_points = {
            {{70, 1, 3}, {70, 3, 8}, {70, 19, 48}, {120, 5, 12}}};
        inline constexpr std::array<int, 4> k_iir_divisors = {1, 2, 4, 8};

        /// Which tabulated spec point and divisor a (relaxed) profile is: the
        /// stopband equal and the passband fraction equal, bit for bit, to
        /// (num / den) / d, which is what profile::relaxed computes for the
        /// named profiles (a division by a power of two, exact).
        struct iir_key {
            int point   = -1; ///< index into k_iir_spec_points, -1: none
            int divisor = 0;
        };
        inline iir_key find_iir_key(const profile& p) noexcept {
            for (std::size_t i = 0; i < k_iir_spec_points.size(); ++i) {
                const iir_spec_point& s = k_iir_spec_points[i];
                if (!(p.stopband_atten_db == static_cast<double>(s.stop_db))) {
                    continue;
                }
                for (const int d : k_iir_divisors) {
                    const double frac =
                        static_cast<double>(s.pass_num) / static_cast<double>(s.pass_den) / static_cast<double>(d);
                    if (p.passband_frac == frac) {
                        return {static_cast<int>(i), d};
                    }
                }
            }
            return {};
        }

        /// The sixteen compile-time nonlinear designs, spec point major,
        /// divisor minor (I4: constexpr, no libm).
        inline constexpr std::array<tap::dsp::nonlinear_halfband_design, 16> k_iir_nonlinear_designs = {
            tap::dsp::k_nonlinear_halfband<70, 1, 3, 1>,   tap::dsp::k_nonlinear_halfband<70, 1, 3, 2>,
            tap::dsp::k_nonlinear_halfband<70, 1, 3, 4>,   tap::dsp::k_nonlinear_halfband<70, 1, 3, 8>,
            tap::dsp::k_nonlinear_halfband<70, 3, 8, 1>,   tap::dsp::k_nonlinear_halfband<70, 3, 8, 2>,
            tap::dsp::k_nonlinear_halfband<70, 3, 8, 4>,   tap::dsp::k_nonlinear_halfband<70, 3, 8, 8>,
            tap::dsp::k_nonlinear_halfband<70, 19, 48, 1>, tap::dsp::k_nonlinear_halfband<70, 19, 48, 2>,
            tap::dsp::k_nonlinear_halfband<70, 19, 48, 4>, tap::dsp::k_nonlinear_halfband<70, 19, 48, 8>,
            tap::dsp::k_nonlinear_halfband<120, 5, 12, 1>, tap::dsp::k_nonlinear_halfband<120, 5, 12, 2>,
            tap::dsp::k_nonlinear_halfband<120, 5, 12, 4>, tap::dsp::k_nonlinear_halfband<120, 5, 12, 8>};

        inline std::size_t iir_divisor_index(int divisor) noexcept {
            std::size_t i = 0;
            while (i + 1 < k_iir_divisors.size() && k_iir_divisors[i] != divisor) {
                ++i;
            }
            return i;
        }

        /// The committed design of a table (DspTap's) at a key, or nullptr.
        template <typename Design>
        const Design* find_committed(std::span<const Design> tables, const iir_key& key) noexcept {
            if (key.point < 0) {
                return nullptr;
            }
            const iir_spec_point& s = k_iir_spec_points[static_cast<std::size_t>(key.point)];
            for (const Design& d : tables) {
                if (d.stop_db == s.stop_db && d.pass_num == s.pass_num && d.pass_den == s.pass_den
                    && d.divisor == key.divisor) {
                    return &d;
                }
            }
            return nullptr;
        }

        /// The largest |f(x) - f(0)| over [0, edge] on 257 points: a passband
        /// delay's deviation from its DC value (the nonlinear delay rises
        /// monotonically to the edge, which the grid includes).
        template <typename F>
        double deviation_from_dc(F tau, double edge) noexcept {
            const double t0    = tau(0.0);
            double       worst = 0.0;
            for (int g = 1; g <= 256; ++g) {
                const double dev = std::fabs(tau(edge * static_cast<double>(g) / 256.0) - t0);
                worst            = dev > worst ? dev : worst;
            }
            return worst;
        }

    } // namespace detail

    /// The texts validate() returns and the constructor raises.
    inline constexpr const char* k_iir_near_linear_profile_reason =
        "tap::sr::rational::basic_iir_stage: the near-linear variant runs the named profiles at the design "
        "divisors 1, 2, 4 and 8 (committed designs, tap/dsp/iir_tables.h); this profile is not one of them";
    inline constexpr const char* k_iir_unmeetable_spec_reason =
        "tap::sr::rational::basic_iir_stage: no nonlinear half-band up to order 33 meets the profile's spec";

    // ANCHOR: rational_iir_stage_base
    /// The count-independent part of a recursive stage at ratio R over sample
    /// format S in variant P (K12): everything but the hot paths, built once
    /// per format, ratio and variant and shared by every channel count's
    /// basic_iir_stage<S, Ch, R, P>. See the file header.
    template <tap::dsp::sample_type S, rational_ratio R, iir_phase P>
    class iir_stage_base {
        static_assert(detail::k_iir_by2<R>,
                      "tap::sr::rational: a recursive stage runs by 2 (up2, down2); up3 / down3, the three-path "
                      "structure, is planned (docs/IIR_PLAN.md) and not implemented, and the four-path stage by 4 "
                      "(up4, down4) was measured and does not ship (docs/IIR_PLAN.md I12)");

      public:
        using sample = S;
        using traits = ratio_traits<R>;
        using state  = tap::dsp::iir_state<S>;

        static constexpr std::size_t k_up           = traits::k_up;
        static constexpr std::size_t k_down         = traits::k_down;
        static constexpr bool        k_is_decimator = traits::k_is_decimator;
        /// The factor, 2: the paths of the structure (the interfaces are
        /// written for P paths, P = L or M; only P = 2 is implemented).
        static constexpr std::size_t k_factor         = k_is_decimator ? k_down : k_up;
        static constexpr iir_phase   k_phase          = P;
        static constexpr bool        k_is_near_linear = P == iir_phase::near_linear;

        using table_type = std::conditional_t<k_is_near_linear, tap::dsp::near_linear_halfband_table<S>,
                                              tap::dsp::nonlinear_halfband_table<S>>;

        // ANCHOR: rational_iir_validate
        /// The reason a stage cannot be built from p, or nullptr when it can
        /// (D17): a bad profile (validate(p)'s text); for the near-linear
        /// variant a profile without a committed design; for the nonlinear
        /// variant a spec no order up to 33 meets. noexcept and
        /// allocation-free (a custom nonlinear profile runs the constexpr
        /// design at run time, as construction does). The constructor rejects
        /// with the same text through tap::dsp::raise (std::invalid_argument,
        /// or std::runtime_error for the unmeetable spec), so a build without
        /// exceptions calls this first.
        static const char* validate(const profile& p = profile::economy()) noexcept {
            if (const char* why = tap::sr::rational::validate(p)) {
                return why;
            }
            const detail::iir_key key = detail::find_iir_key(p);
            if constexpr (k_is_near_linear) {
                if (detail::find_committed<tap::dsp::near_linear_halfband_design>(
                        tap::dsp::k_near_linear_halfband_tables, key)
                    == nullptr) {
                    return k_iir_near_linear_profile_reason;
                }
            }
            else {
                if (key.point < 0
                    && tap::dsp::design_nonlinear_halfband(p.stopband_atten_db, p.passband_frac).order == 0) {
                    return k_iir_unmeetable_spec_reason;
                }
            }
            return nullptr;
        }
        // ANCHOR_END: rational_iir_validate

        /// Outputs the next in_frames inputs yield from the current position.
        std::size_t outputs_for(std::size_t in_frames) const noexcept {
            if constexpr (k_is_decimator) {
                const std::size_t first = 1 + (k_down - m_dphase) % k_down;
                return in_frames < first ? 0 : 1 + (in_frames - first) / k_down;
            }
            else {
                return m_banked + k_up * in_frames;
            }
        }

        /// Inputs the next out_frames outputs need: the smallest n with
        /// outputs_for(n) >= out_frames.
        std::size_t frames_needed(std::size_t out_frames) const noexcept {
            if (out_frames == 0) {
                return 0;
            }
            if constexpr (k_is_decimator) {
                return 1 + (k_down - m_dphase) % k_down + (out_frames - 1) * k_down;
            }
            else {
                return out_frames <= m_banked ? 0 : (out_frames - m_banked + k_up - 1) / k_up;
            }
        }

        /// Input frames of zeros after which the state is exactly zero and
        /// every output silence (the file header, I6).
        std::size_t window_frames() const noexcept { return m_window; }
        /// Frames flush() will write from the current position.
        std::size_t flush_output_frames() const noexcept { return outputs_for(window_frames()); }

        /// Return to the zero-primed initial state (out of line: cold).
        TAP_SR_RATIONAL_NOINLINE void reset() noexcept {
            for (auto& v : m_state) {
                v = state{};
            }
            for (auto& v : m_latch) {
                v = tap::dsp::sample_traits<S>::silence();
            }
            for (auto& v : m_delay) {
                v = tap::dsp::sample_traits<S>::silence();
            }
            for (auto& v : m_bank) {
                v = tap::dsp::sample_traits<S>::silence();
            }
            m_delay_at = 0;
            m_dphase   = 0;
            m_banked   = 0;
        }

        // ANCHOR: rational_iir_latency
        /// The nominal latency in output frames (I5): the group delay at DC,
        /// or the near-linear variant's delay path.
        double nominal_latency_output_frames() const noexcept {
            if constexpr (k_is_near_linear) {
                return delay_path_output_frames().value();
            }
            else {
                return group_delay_output_frames(0.0);
            }
        }

        /// How far the passband's delay strays from the nominal, at most, in
        /// output frames: over the stage's passband, 0 to its relaxed edge.
        double latency_deviation_output_frames() const noexcept {
            if constexpr (k_is_near_linear) {
                // The committed max |tau(f) - 2K| over the passband, in
                // higher-rate samples (tap/dsp/iir_tables.h).
                return m_near_linear->delay_deviation / (k_is_decimator ? 2.0 : 1.0);
            }
            else {
                const double edge = m_passband / (k_is_decimator ? 1.0 : static_cast<double>(k_factor));
                return detail::deviation_from_dc([this](double f) { return group_delay_output_frames(f); }, edge);
            }
        }

        /// The group delay at f, cycles per output frame, in output frames.
        double group_delay_output_frames(double f) const noexcept {
            constexpr double k = static_cast<double>(k_factor);
            // The structure's delay at its higher rate, in higher-rate samples.
            const double f_hi = k_is_decimator ? f / k : f;
            double       tau  = 0.0;
            if constexpr (k_is_near_linear) {
                tau = tap::dsp::halfband_group_delay(*m_near_linear, f_hi);
            }
            else {
                tau = tap::dsp::halfband_group_delay(m_nonlinear, f_hi);
            }
            return k_is_decimator ? tau / k : tau;
        }

        /// The near-linear variant's delay path in output frames, exact: K
        /// going down, 2K going up (tap::dsp::reports_delay_path; the
        /// round-trip scaffold's T).
        tap::dsp::exact_ratio delay_path_output_frames() const noexcept
            requires(P == iir_phase::near_linear)
        {
            const auto delay = static_cast<std::uint64_t>(m_table.delay);
            return tap::dsp::exact_ratio{k_is_decimator ? delay : 2 * delay, 1};
        }
        // ANCHOR_END: rational_iir_latency

        /// Multiplies per output frame per channel: the sections of every
        /// path per decimated output (the design's multiplies), and for an
        /// interpolator the same over its factor.
        double macs_per_output() const noexcept {
            const auto m = static_cast<double>(m_table.multiplies());
            return k_is_decimator ? m : m / static_cast<double>(k_factor);
        }
        /// Multiplies per decimated output (per input of an interpolator).
        std::size_t multiplies() const noexcept { return m_table.multiplies(); }

        /// The table this stage runs, in its format (tap/dsp/iir_kernels.h).
        const table_type& table() const noexcept { return m_table; }
        /// The passband edge this stage was designed at, a fraction of its
        /// lower rate (the profile's passband over the stage's divisor).
        double passband() const noexcept { return m_passband; }
        /// The stopband attenuation it was designed for, dB.
        double stopband_db() const noexcept { return m_stop_db; }
        /// Whether its coefficients are a tabulated design (a named profile at
        /// a divisor of 1, 2, 4 or 8) rather than a run-time one.
        bool tabulated() const noexcept { return m_tabulated; }
        /// The nonlinear half-band's design.
        const tap::dsp::nonlinear_halfband_design& nonlinear_design() const noexcept
            requires(P == iir_phase::nonlinear)
        {
            return m_nonlinear;
        }
        /// The near-linear half-band's committed design.
        const tap::dsp::near_linear_halfband_design& near_linear_design() const noexcept
            requires(P == iir_phase::near_linear)
        {
            return *m_near_linear;
        }
        /// The recursive states, every channel's, read-only: the cold-path
        /// accessor for tests (exactly zero after flush() under the host's
        /// flush-to-zero, I6).
        std::span<const state> states() const noexcept { return m_state; }
        /// The largest pole radius, in path (lower-rate) samples.
        double pole_radius() const noexcept { return m_pole_radius; }
        /// The decimator's phase: inputs since the last output.
        std::size_t decimation_phase() const noexcept { return m_dphase; }
        /// An interpolator's outputs computed and not yet delivered (pull()).
        std::size_t banked_frames() const noexcept { return m_banked; }

      protected:
        /// Builds the table and allocates the states for `channels`
        /// interleaved channels; setup time only, out of line. Rejects what
        /// validate() rejects, with its text.
        TAP_SR_RATIONAL_NOINLINE iir_stage_base(std::size_t channels, const profile& p) {
            if (const char* why = tap::sr::rational::validate(p)) {
                tap::dsp::raise<std::invalid_argument>(why);
            }
            m_passband                = p.passband_frac;
            m_stop_db                 = p.stopband_atten_db;
            const detail::iir_key key = detail::find_iir_key(p);
            m_tabulated               = key.point >= 0;
            std::size_t sections      = 0;
            if constexpr (k_is_near_linear) {
                m_near_linear = detail::find_committed<tap::dsp::near_linear_halfband_design>(
                    tap::dsp::k_near_linear_halfband_tables, key);
                if (m_near_linear == nullptr) {
                    tap::dsp::raise<std::invalid_argument>(k_iir_near_linear_profile_reason);
                }
                m_table       = table_type{*m_near_linear};
                m_pole_radius = m_near_linear->pole_radius;
                sections      = m_table.n2 + m_table.n1;
            }
            else {
                if (m_tabulated) {
                    m_nonlinear = detail::k_iir_nonlinear_designs[4 * static_cast<std::size_t>(key.point)
                                                                  + detail::iir_divisor_index(key.divisor)];
                }
                else {
                    m_nonlinear = tap::dsp::design_nonlinear_halfband(p.stopband_atten_db, p.passband_frac);
                    if (m_nonlinear.order == 0) {
                        tap::dsp::raise<std::runtime_error>(k_iir_unmeetable_spec_reason);
                    }
                }
                m_table       = table_type{m_nonlinear};
                m_pole_radius = m_nonlinear.coefficients[m_nonlinear.count - 1];
                sections      = m_table.multiplies();
            }
            m_state.assign(channels * m_table.state_size(), state{});
            m_latch.assign(k_is_decimator ? channels * (k_factor - 1) : 0, tap::dsp::sample_traits<S>::silence());
            if constexpr (k_is_near_linear) {
                m_delay.assign(channels * m_table.delay, tap::dsp::sample_traits<S>::silence());
            }
            m_bank.assign(k_is_decimator ? 0 : channels * k_factor, tap::dsp::sample_traits<S>::silence());
            m_window = window_from(sections);
            reset();
        }

        // ANCHOR: rational_iir_window
        /// window_frames() (the file header): path frames to an exactly zero
        /// state, plus the delay path and the latches, in input frames.
        std::size_t window_from(std::size_t sections) const noexcept {
            std::size_t path = 0;
            if constexpr (std::is_floating_point_v<S>) {
                path = tap::dsp::floating_flush_frames<S>(m_pole_radius, sections);
            }
            else if constexpr (k_is_near_linear) {
                path = 2 * fixed_near_linear_decay();
            }
            else {
                path = fixed_first_order_decay();
            }
            if constexpr (k_is_near_linear) {
                path += m_table.delay;
            }
            // Path frames are lower-rate frames: k_factor inputs each going
            // down (plus the latches' k_factor - 1), one input each going up.
            return k_is_decimator ? k_factor * (path + 1) : path;
        }

        /// IIR-0's bound for one-multiplier sections, exact: per path 1 plus
        /// the sum of each section's longest floor-truncated decay from the
        /// full state headroom (the kernel's own arithmetic, from both ends
        /// of the range; the map is monotone in |u|), the longest path.
        std::size_t fixed_first_order_decay() const noexcept
            requires(!std::is_floating_point_v<S> && !k_is_near_linear)
        {
            using tr           = tap::dsp::iir_traits<S>;
            const auto section = [](typename tr::coeff1 a) noexcept {
                const std::int64_t top   = std::int64_t{1} << (tr::k_state_frac_bits + 2);
                std::size_t        worst = 0;
                for (const std::int64_t start : {top - 1, -top}) {
                    auto        u = static_cast<state>(start);
                    std::size_t n = 0;
                    while (u != 0 && n < 100000) {
                        u = tr::allpass1(state{}, state{}, u, a);
                        ++n;
                    }
                    worst = n > worst ? n : worst;
                }
                return worst;
            };
            std::size_t p0 = 1;
            std::size_t p1 = 1;
            for (std::size_t i = 0; i < m_table.size0; ++i) {
                p0 += section(m_table.path0[i]);
            }
            for (std::size_t i = 0; i < m_table.size1; ++i) {
                p1 += section(m_table.path1[i]);
            }
            return p0 > p1 ? p0 : p1;
        }

        /// The near-linear path's magnitude-truncated decay: per section the
        /// zero-input decay of its pole radius from the full state headroom
        /// to one LSB, ceil((F + 2) ln 2 / ln(1 / r)), summed with one frame
        /// per section and 1 (window_from doubles it: the measured worst
        /// decay from full-scale drive is within this sum at every profile
        /// and divisor, rational.IirStage.FixedPointSilenceIsExactlyZero-
        /// AfterTheWindow).
        std::size_t fixed_near_linear_decay() const noexcept
            requires(!std::is_floating_point_v<S> && k_is_near_linear)
        {
            using tr           = tap::dsp::iir_traits<S>;
            const double span  = static_cast<double>(tr::k_state_frac_bits + 2) * std::numbers::ln2;
            const auto   decay = [span](double r) noexcept {
                return r <= 0.0 ? std::size_t{1} : static_cast<std::size_t>(std::ceil(span / -std::log(r))) + 1;
            };
            const std::span<const double> c     = m_near_linear->sections;
            std::size_t                   total = 1;
            for (std::size_t i = 0; i < m_table.n2; ++i) {
                const double c1   = c[2 * i];
                const double c2   = c[2 * i + 1];
                const double disc = c1 * c1 - 4.0 * c2;
                // Poles of 1 + c1 w^-1 + c2 w^-2: a complex pair at radius
                // sqrt(c2), or two real ones, the larger |(-c1 +- sqrt(disc)) / 2|.
                const double r = disc < 0.0 ? std::sqrt(c2) : 0.5 * (std::fabs(c1) + std::sqrt(disc));
                total += decay(r);
            }
            if (m_table.n1 != 0) {
                total += decay(std::fabs(c[2 * m_table.n2]));
            }
            return total;
        }
        // ANCHOR_END: rational_iir_window

        table_type                                   m_table{};
        tap::dsp::nonlinear_halfband_design          m_nonlinear{}; ///< nonlinear by 2
        const tap::dsp::near_linear_halfband_design* m_near_linear = nullptr;
        double                                       m_passband    = 0.0;
        double                                       m_stop_db     = 0.0;
        double                                       m_pole_radius = 0.0;
        bool                                         m_tabulated   = false;
        std::size_t                                  m_window      = 0;
        std::vector<state>                           m_state;        ///< channels x table.state_size()
        std::vector<S>                               m_latch;        ///< decimators: channels x (factor - 1)
        std::vector<S>                               m_delay;        ///< near-linear: channels x K, a ring
        std::vector<S>                               m_bank;         ///< interpolators: channels x factor
        std::size_t                                  m_delay_at = 0; ///< the ring's oldest entry
        std::uint32_t                                m_dphase   = 0; ///< decimators: inputs since the last output
        std::uint32_t                                m_banked   = 0; ///< interpolators: outputs not yet delivered
    };
    // ANCHOR_END: rational_iir_stage_base

    // ANCHOR: rational_iir_stage
    /// One recursive stage at ratio R over sample format S in phase variant
    /// P, running Ch interleaved channels (a compile-time count of at least
    /// 1); see the file header. Its state and cold code are
    /// iir_stage_base<S, R, P>'s.
    template <tap::dsp::sample_type S, std::size_t Ch, rational_ratio R, iir_phase P>
    class basic_iir_stage : public iir_stage_base<S, R, P> {
        static_assert(Ch >= 1, "tap::sr::rational: a stage's channel count is at least 1");
        using base = iir_stage_base<S, R, P>;

      public:
        using base::k_down;
        using base::k_factor;
        using base::k_is_decimator;
        using base::k_is_near_linear;
        using base::k_up;
        using base::outputs_for;
        using base::window_frames;
        using typename base::state;
        using typename base::table_type;

        /// The channel count, the type's: tap::dsp::sync_stage reads it.
        static constexpr std::size_t channels() noexcept { return Ch; }

        /// Builds the table and allocates the states; setup time only.
        /// Rejects what validate() rejects, with its text.
        explicit basic_iir_stage(const profile& p = profile::economy())
            : base(Ch, p) {}

        /// Push-transform: consumes all in_frames interleaved frames, writes
        /// outputs_for(in_frames) frames (out must hold them); returns that.
        TAP_SR_RATIONAL_NOINLINE std::size_t process(const S* in, std::size_t in_frames, S* out) noexcept {
            TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED(S);
            cursor            b    = open();
            const std::size_t made = run<false>(b, in, in_frames, out);
            close(b);
            return made;
        }

        /// Pull: produce exactly out_frames frames, drawing input through
        /// pop(dst, max_frames) -> frames delivered (interleaved; fewer means
        /// the source ran dry and the call returns short, resumable). Bit-
        /// identical to process() on the same stream. PopFn must be noexcept.
        template <typename PopFn>
        TAP_SR_RATIONAL_NOINLINE std::size_t pull(S* out, std::size_t out_frames, PopFn&& pop) noexcept {
            static_assert(std::is_nothrow_invocable_r_v<std::size_t, PopFn&, S*, std::size_t>,
                          "pull() requires a noexcept PopFn: std::size_t(S*, std::size_t) noexcept");
            TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED(S);
            cursor            b = open();
            std::array<S, Ch> frame{};
            for (std::size_t n = 0; n < out_frames; ++n) {
                if constexpr (k_is_decimator) {
                    // The inputs up to and including the one at phase 0, one
                    // by one, so a dry source leaves the exact partial state.
                    for (;;) {
                        if (pop(frame.data(), 1) == 0) {
                            close(b);
                            return n; // dry
                        }
                        const bool emits = b.dphase == 0;
                        step_down(b, frame.data(), out + n * Ch);
                        if (emits) {
                            break;
                        }
                    }
                }
                else {
                    if (b.banked == 0) {
                        if (pop(frame.data(), 1) == 0) {
                            close(b);
                            return n; // dry
                        }
                        step_up(b, frame.data(), b.bank, k_factor);
                        b.banked = static_cast<std::uint32_t>(k_factor);
                    }
                    const S* banked = b.bank + (k_factor - b.banked) * Ch;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[n * Ch + c] = banked[c];
                    }
                    --b.banked;
                }
            }
            close(b);
            return out_frames;
        }

        /// End of stream: feeds window_frames() zeros and writes the outputs
        /// that become ready, flush_output_frames() of them. The stage is
        /// left in the zero-fed state; reset() before reuse.
        TAP_SR_RATIONAL_NOINLINE std::size_t flush(S* out) noexcept {
            TAP_DSP_EXPECTS_SUBNORMALS_FLUSHED(S);
            cursor            b    = open();
            const std::size_t made = run<true>(b, nullptr, window_frames(), out);
            close(b);
            return made;
        }

      private:
        using base::m_bank;
        using base::m_banked;
        using base::m_delay;
        using base::m_delay_at;
        using base::m_dphase;
        using base::m_latch;
        using base::m_state;
        using base::m_table;

        // ANCHOR: rational_iir_cursor
        /// The hot loops' view of the stage for one call (A12): the stream
        /// state and the buffers as locals, loaded by open(), stored back by
        /// close(); the members stay authoritative between calls.
        struct cursor {
            const table_type* table;
            state*            states;
            S*                latch;
            S*                delay;
            S*                bank;
            std::size_t       state_size;
            std::size_t       delay_len;
            std::size_t       delay_at;
            std::uint32_t     dphase;
            std::uint32_t     banked;
        };

        cursor open() noexcept {
            cursor b{};
            b.table      = &m_table;
            b.states     = m_state.data();
            b.latch      = m_latch.data();
            b.delay      = m_delay.data();
            b.bank       = m_bank.data();
            b.state_size = m_table.state_size();
            if constexpr (k_is_near_linear) {
                b.delay_len = m_table.delay;
            }
            b.delay_at = m_delay_at;
            b.dphase   = m_dphase;
            b.banked   = m_banked;
            return b;
        }

        void close(const cursor& b) noexcept {
            m_delay_at = b.delay_at;
            m_dphase   = b.dphase;
            m_banked   = b.banked;
        }
        // ANCHOR_END: rational_iir_cursor

        /// The block: n frames from in (or silence when Zero), the banked
        /// outputs first; returns the frames written.
        template <bool Zero>
        TAP_SR_RATIONAL_ALWAYS_INLINE std::size_t run(cursor& b, const S* in, std::size_t n, S* out) noexcept {
            std::size_t       made = 0;
            std::array<S, Ch> silence{};
            if constexpr (k_is_decimator) {
                for (std::size_t i = 0; i < n; ++i) {
                    const bool emits = b.dphase == 0;
                    step_down(b, Zero ? silence.data() : in + i * Ch, out + made * Ch);
                    made += emits ? 1 : 0;
                }
            }
            else {
                for (; b.banked != 0; --b.banked, ++made) {
                    const S* banked = b.bank + (k_factor - b.banked) * Ch;
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[made * Ch + c] = banked[c];
                    }
                }
                for (std::size_t i = 0; i < n; ++i) {
                    step_up(b, Zero ? silence.data() : in + i * Ch, out + made * Ch, k_factor);
                    made += k_factor;
                }
            }
            return made;
        }

        // ANCHOR: rational_iir_steps
        /// A decimator's one input frame: at phase 0 it completes an output
        /// (written to out), else it waits in its latch.
        TAP_SR_RATIONAL_ALWAYS_INLINE static void step_down(cursor& b, const S* frame, S* out) noexcept {
            constexpr std::size_t lat = k_factor - 1;
            if (b.dphase == 0) {
                for (std::size_t c = 0; c < Ch; ++c) {
                    state* st = b.states + c * b.state_size;
                    S*     lc = b.latch + c * lat;
                    if constexpr (k_is_near_linear) {
                        // The ring's oldest even input is x[2(k - K)].
                        S*      ring     = b.delay + c * b.delay_len;
                        const S delayed  = ring[b.delay_at];
                        ring[b.delay_at] = frame[c];
                        out[c]           = tap::dsp::near_linear_decimate<S>(*b.table, st, delayed, lc[0]);
                    }
                    else {
                        out[c] = tap::dsp::halfband_decimate<S>(*b.table, st, frame[c], lc[0]);
                    }
                }
                if constexpr (k_is_near_linear) {
                    b.delay_at = b.delay_at + 1 == b.delay_len ? 0 : b.delay_at + 1;
                }
            }
            else {
                for (std::size_t c = 0; c < Ch; ++c) {
                    b.latch[c * lat + (b.dphase - 1)] = frame[c];
                }
            }
            b.dphase = b.dphase + 1 == k_factor ? 0 : b.dphase + 1;
        }

        /// An interpolator's one input frame: its k_factor output frames,
        /// written at out (stride Ch).
        TAP_SR_RATIONAL_ALWAYS_INLINE static void step_up(cursor& b, const S* frame, S* out,
                                                          std::size_t /*frames*/) noexcept {
            for (std::size_t c = 0; c < Ch; ++c) {
                state*                  st = b.states + c * b.state_size;
                std::array<S, k_factor> y{};
                if constexpr (k_is_near_linear) {
                    S*      ring     = b.delay + c * b.delay_len;
                    const S delayed  = ring[b.delay_at];
                    ring[b.delay_at] = frame[c];
                    tap::dsp::near_linear_interpolate<S>(*b.table, st, delayed, frame[c], y.data());
                }
                else {
                    tap::dsp::halfband_interpolate<S>(*b.table, st, frame[c], y.data());
                }
                for (std::size_t j = 0; j < k_factor; ++j) {
                    out[j * Ch + c] = y[j];
                }
            }
            if constexpr (k_is_near_linear) {
                b.delay_at = b.delay_at + 1 == b.delay_len ? 0 : b.delay_at + 1;
            }
        }
        // ANCHOR_END: rational_iir_steps
    };
    // ANCHOR_END: rational_iir_stage

    // ANCHOR: rational_iir_aliases
    /// The recursive converters of Ch channels at ratio R, beside converter<Ch,
    /// R> (converter.h): float (the golden-model profile), Q15 and Q31, the
    /// nonlinear variant and the near-linear one; basic_iir_stage<double, Ch,
    /// R, P> is the double oracle.
    template <std::size_t Ch, rational_ratio R>
    using iir_converter = basic_iir_stage<float, Ch, R, iir_phase::nonlinear>;
    template <std::size_t Ch, rational_ratio R>
    using iir_converter_q15 = basic_iir_stage<std::int16_t, Ch, R, iir_phase::nonlinear>;
    template <std::size_t Ch, rational_ratio R>
    using iir_converter_q31 = basic_iir_stage<std::int32_t, Ch, R, iir_phase::nonlinear>;
    template <std::size_t Ch, rational_ratio R>
    using iir_converter_near_linear = basic_iir_stage<float, Ch, R, iir_phase::near_linear>;
    template <std::size_t Ch, rational_ratio R>
    using iir_converter_near_linear_q15 = basic_iir_stage<std::int16_t, Ch, R, iir_phase::near_linear>;
    template <std::size_t Ch, rational_ratio R>
    using iir_converter_near_linear_q31 = basic_iir_stage<std::int32_t, Ch, R, iir_phase::near_linear>;
    // ANCHOR_END: rational_iir_aliases

    static_assert(tap::dsp::sync_stage<basic_iir_stage<float, 1, down2, iir_phase::nonlinear>>);
    static_assert(tap::dsp::sync_stage<basic_iir_stage<std::int16_t, 2, up2, iir_phase::near_linear>>);
    static_assert(tap::dsp::reports_nominal_latency<basic_iir_stage<float, 1, up2, iir_phase::nonlinear>>);

} // namespace tap::sr::rational
