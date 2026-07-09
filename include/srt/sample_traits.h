// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
// ANCHOR: st_overview
/// \file sample_traits.h
/// \brief Sample-type customization point for the resampling datapath.
///
/// The datapath (polyphase_filter_bank, the interpolation kernel,
/// fractional_resampler, basic_async_sample_rate_converter) is templated on the
/// sample type through sample_traits<T>. Three sample types are provided:
///
///  - float        : float I/O and coefficients, double accumulation
///  - std::int16_t : Q15 samples, Q1.14 coefficients, int64 accumulation,
///                   saturating output
///  - std::int32_t : Q31 samples, Q1.30 coefficients, int64 accumulation,
///                   saturating output
///
/// The clock servo and the filter design always run in double regardless of
/// sample type (control path and one-time init, not the audio path), so the
/// fixed-point datapaths contain no floating-point inner loops.
// ANCHOR_END: st_overview
#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>

namespace srt {

    namespace detail {

        // ANCHOR: st_roundsat
        /// Round-and-saturate a double to a signed integer coefficient/sample type.
        template <typename I>
        constexpr I round_sat(double v) noexcept {
            constexpr double lo = static_cast<double>(std::numeric_limits<I>::min());
            constexpr double hi = static_cast<double>(std::numeric_limits<I>::max());
            const double     r  = v < 0.0 ? v - 0.5 : v + 0.5; // round half away from zero
            if (r <= lo) {
                return std::numeric_limits<I>::min();
            }
            if (r >= hi) {
                return std::numeric_limits<I>::max();
            }
            return static_cast<I>(r);
        }
        // ANCHOR_END: st_roundsat

        /// Saturate a 64-bit accumulator result to a narrower signed integer.
        template <typename I>
        constexpr I clamp_sat(std::int64_t v) noexcept {
            constexpr auto lo = static_cast<std::int64_t>(std::numeric_limits<I>::min());
            constexpr auto hi = static_cast<std::int64_t>(std::numeric_limits<I>::max());
            return static_cast<I>(v < lo ? lo : (v > hi ? hi : v));
        }

    } // namespace detail

    // ANCHOR: st_primary
    /// Primary template intentionally undefined; specialize per sample type.
    template <typename T>
    struct sample_traits;
    // ANCHOR_END: st_primary

    // ANCHOR: st_float
    /// Float datapath: float samples and coefficients, double accumulation.
    /// The double accumulator keeps the dot-product noise floor far below the
    /// 120 dB transparency target; float coefficient storage quantizes the
    /// filter at roughly -150 dB, negligible against the same target.
    template <>
    struct sample_traits<float> {
        using coeff        = float;  ///< stored filter coefficient type
        using accum        = double; ///< dot-product accumulator type
        using blend_factor = float;  ///< per-sample fractional blend representation

        /// Convert a double-precision designed coefficient to storage form.
        static coeff make_coeff(double c) noexcept { return static_cast<coeff>(c); }
        /// Coefficient units per 1.0 (used by the bank's row-sum-preserving
        /// quantization; unity for floating storage, where no correction runs).
        static constexpr double k_coeff_scale = 1.0;

        /// Convert the intra-phase fraction (in [0,1)) once per output sample.
        static blend_factor make_blend_factor(double fr) noexcept { return static_cast<blend_factor>(fr); }

        // ANCHOR: st_blend_q64_float
        /// Blend factor from the top bits of a Q0.64 intra-phase fraction.
        /// Single-precision only: the value is reduced to 24 bits first so the
        /// uint->float conversion is exact and no double op is needed
        /// (significant on targets without a double-precision FPU).
        static blend_factor blend_factor_from_q64(std::uint64_t frac) noexcept {
            return static_cast<float>(frac >> 40) * 0x1p-24f;
        }
        // ANCHOR_END: st_blend_q64_float

        /// acc + x * c, in the accumulator domain.
        static accum mac(accum acc, float x, coeff c) noexcept {
            return acc + static_cast<double>(x) * static_cast<double>(c);
        }

        /// Linear blend between two adjacent-phase coefficients.
        static coeff blend(coeff a, coeff b, blend_factor fr) noexcept { return a + fr * (b - a); }

        /// Convert the accumulator to an output sample (saturates for fixed point).
        static float finalize(accum acc) noexcept { return static_cast<float>(acc); }

        /// The zero/silence sample value.
        static float silence() noexcept { return 0.0f; }
    };
    // ANCHOR_END: st_float

    // ANCHOR: st_q15_header
    /// Q15 fixed-point datapath (samples are int16_t in Q0.15).
    ///
    /// Coefficients are stored in Q1.14: the prototype's peak tap reaches ~1.0
    /// (per-phase DC gain is 1), which does not fit Q0.15, so one headroom bit
    /// is traded for one precision bit. Products are Q0.15 x Q1.14 = Q29 and are
    /// summed exactly in int64 (48-80 taps add ~6-7 bits — no overflow, no
    /// intermediate rounding). The single rounding happens in finalize():
    /// Q29 -> Q15 with round-half-up and saturation. Coefficient quantization
    /// (Q14, ~-86 dB) and output quantization (Q15) set the noise floor — both
    /// at the format's own limit, so the converter is Q15-transparent.
    template <>
    struct sample_traits<std::int16_t> {
        using coeff        = std::int16_t;
        using accum        = std::int64_t;
        using blend_factor = std::int32_t; ///< fraction in Q15
        // ANCHOR_END: st_q15_header

        // ANCHOR: st_q15_coeff
        static coeff make_coeff(double c) noexcept {
            return detail::round_sat<coeff>(c * 16384.0); // Q1.14
        }
        static constexpr double k_coeff_scale = 16384.0; // Q1.14 units per 1.0
        // ANCHOR_END: st_q15_coeff

        static blend_factor make_blend_factor(double fr) noexcept {
            return static_cast<blend_factor>(fr * 32768.0); // Q15
        }

        // ANCHOR: st_q15_q64
        /// Q15 blend factor straight from a Q0.64 fraction's top bits: no
        /// floating point at all on the fixed-point per-sample path.
        static blend_factor blend_factor_from_q64(std::uint64_t frac) noexcept {
            return static_cast<blend_factor>(frac >> 49); // Q15
        }
        // ANCHOR_END: st_q15_q64

        // ANCHOR: st_q15_mac
        static accum mac(accum acc, std::int16_t x, coeff c) noexcept {
            return acc + static_cast<std::int64_t>(static_cast<std::int32_t>(x) * static_cast<std::int32_t>(c));
        }
        // ANCHOR_END: st_q15_mac

        // ANCHOR: st_q15_blend
        static coeff blend(coeff a, coeff b, blend_factor fr) noexcept {
            // Q14 + (Q15 * Q14) >> 15, in int64: the worst-case int32 product
            // 32767 * 65535 = 2,147,385,345 sits 0.005% under INT32_MAX —
            // real adjacent-phase deltas are tiny (|diff| <= 41 measured on the
            // transparent table), but a margin that thin is not an invariant
            // worth relying on silently. One smull on 32-bit cores.
            const std::int64_t diff = static_cast<std::int64_t>(b) - a;
            return static_cast<coeff>(a + ((fr * diff) >> 15));
        }
        // ANCHOR_END: st_q15_blend

        // ANCHOR: st_q15_finalize
        static std::int16_t finalize(accum acc) noexcept {
            // Round-half-up, not half-even: the bias is a fraction of one
            // sub-LSB rounding step, far below the Q15 noise floor.
            return detail::clamp_sat<std::int16_t>((acc + (1 << 13)) >> 14); // Q29 -> Q15
        }
        // ANCHOR_END: st_q15_finalize

        static std::int16_t silence() noexcept { return 0; }
    };

    // ANCHOR: st_q31
    /// Q31 fixed-point datapath (samples are int32_t in Q0.31).
    ///
    /// Coefficients are stored in Q1.30 (one headroom bit for the ~1.0 peak
    /// tap). A full-precision product would be Q0.31 x Q1.30 = 62 bits, which
    /// overflows int64 once ~48 of them are summed, so each product is
    /// pre-shifted down 16 bits (Q45) before accumulation; the discarded bits
    /// sit 14 bits below the final Q31 LSB, far beneath the format's noise
    /// floor. finalize() rounds Q45 -> Q31 with saturation. The blend fraction
    /// uses Q20 (the int64 blend path makes the extra precision free).
    template <>
    struct sample_traits<std::int32_t> {
        using coeff        = std::int32_t;
        using accum        = std::int64_t;
        using blend_factor = std::int32_t; ///< fraction in Q20

        static coeff make_coeff(double c) noexcept {
            return detail::round_sat<coeff>(c * 1073741824.0); // Q1.30
        }
        static constexpr double k_coeff_scale = 1073741824.0; // Q1.30 units per 1.0

        static blend_factor make_blend_factor(double fr) noexcept {
            return static_cast<blend_factor>(fr * 1048576.0); // Q20
        }

        /// Q20 blend factor straight from a Q0.64 fraction's top bits.
        static blend_factor blend_factor_from_q64(std::uint64_t frac) noexcept {
            return static_cast<blend_factor>(frac >> 44); // Q20
        }

        // ANCHOR: st_q31_mac
        static accum mac(accum acc, std::int32_t x, coeff c) noexcept {
            return acc + ((static_cast<std::int64_t>(x) * c) >> 16); // Q61 -> Q45
        }
        // ANCHOR_END: st_q31_mac

        static coeff blend(coeff a, coeff b, blend_factor fr) noexcept {
            const std::int64_t diff = static_cast<std::int64_t>(b) - a;
            return static_cast<coeff>(a + ((fr * diff) >> 20));
        }

        static std::int32_t finalize(accum acc) noexcept {
            return detail::clamp_sat<std::int32_t>((acc + (1 << 13)) >> 14); // Q45 -> Q31
        }

        static std::int32_t silence() noexcept { return 0; }
    };
    // ANCHOR_END: st_q31

    // ANCHOR: st_concept
    /// Satisfied by any type with a complete, well-formed sample_traits
    /// specialization.
    template <typename T>
    concept sample_type = requires(T x, double d, typename sample_traits<T>::accum a,
                                   typename sample_traits<T>::coeff c, typename sample_traits<T>::blend_factor f) {
        { sample_traits<T>::make_coeff(d) } -> std::same_as<typename sample_traits<T>::coeff>;
        { sample_traits<T>::make_blend_factor(d) } -> std::same_as<typename sample_traits<T>::blend_factor>;
        {
            sample_traits<T>::blend_factor_from_q64(std::uint64_t{})
        } -> std::same_as<typename sample_traits<T>::blend_factor>;
        { sample_traits<T>::mac(a, x, c) } -> std::same_as<typename sample_traits<T>::accum>;
        { sample_traits<T>::blend(c, c, f) } -> std::same_as<typename sample_traits<T>::coeff>;
        { sample_traits<T>::finalize(a) } -> std::same_as<T>;
        { sample_traits<T>::silence() } -> std::same_as<T>;
    };

    static_assert(sample_type<float>);
    static_assert(sample_type<std::int16_t>);
    static_assert(sample_type<std::int32_t>);
    // ANCHOR_END: st_concept

} // namespace srt
