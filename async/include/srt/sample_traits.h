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
/// The format core — coefficient Q formats, mac/finalize, rounding and
/// saturation — lives in DspTap (tap::dsp::sample_traits, consumed via the
/// submodules/dsptap submodule) and is shared with RatioTap and the rest of
/// the family. This header layers the ASRC-specific stratum on top: the
/// inter-phase coefficient *blend* machinery (mu interpolation between
/// adjacent polyphase rows, including the Q0.64 phase-accumulator entry
/// points), which a fixed-ratio converter has no use for.
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

#include "tap/dsp/sample_traits.h"

namespace tap::samplerate {

    namespace detail {

        // Historical homes of the round/saturate helpers; the implementations
        // moved to DspTap with the format core.
        using tap::dsp::detail::clamp_sat;
        using tap::dsp::detail::round_sat;

    } // namespace detail

    // ANCHOR: st_primary
    /// Primary template intentionally undefined; specialize per sample type.
    /// Each specialization derives from the shared tap::dsp format core and
    /// adds the blend stratum.
    template <typename T>
    struct sample_traits;
    // ANCHOR_END: st_primary

    // ANCHOR: st_float
    /// Float datapath: the tap::dsp float core (double accumulation) plus a
    /// single-precision blend stratum.
    template <>
    struct sample_traits<float> : tap::dsp::sample_traits<float> {
        using blend_factor = float; ///< per-sample fractional blend representation

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

        /// Linear blend between two adjacent-phase coefficients.
        static coeff blend(coeff a, coeff b, blend_factor fr) noexcept { return a + fr * (b - a); }
    };
    // ANCHOR_END: st_float

    // ANCHOR: st_q15_header
    /// Q15 fixed-point datapath: the tap::dsp Q15 core (Q1.14 coefficients,
    /// exact int64 accumulation, single Q29 -> Q15 rounding) plus an
    /// integer-only blend stratum with the fraction carried in Q15.
    template <>
    struct sample_traits<std::int16_t> : tap::dsp::sample_traits<std::int16_t> {
        using blend_factor = std::int32_t; ///< fraction in Q15
        // ANCHOR_END: st_q15_header

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
    };

    // ANCHOR: st_q31
    /// Q31 fixed-point datapath: the tap::dsp Q31 core (Q1.30 coefficients,
    /// products pre-shifted to Q45) plus an integer-only blend stratum. The
    /// blend fraction uses Q20 (the int64 blend path makes the extra
    /// precision free).
    template <>
    struct sample_traits<std::int32_t> : tap::dsp::sample_traits<std::int32_t> {
        using blend_factor = std::int32_t; ///< fraction in Q20

        static blend_factor make_blend_factor(double fr) noexcept {
            return static_cast<blend_factor>(fr * 1048576.0); // Q20
        }

        /// Q20 blend factor straight from a Q0.64 fraction's top bits.
        static blend_factor blend_factor_from_q64(std::uint64_t frac) noexcept {
            return static_cast<blend_factor>(frac >> 44); // Q20
        }

        static coeff blend(coeff a, coeff b, blend_factor fr) noexcept {
            const std::int64_t diff = static_cast<std::int64_t>(b) - a;
            return static_cast<coeff>(a + ((fr * diff) >> 20));
        }
    };
    // ANCHOR_END: st_q31

    // ANCHOR: st_concept
    /// Satisfied by any type with a complete, well-formed sample_traits
    /// specialization: the tap::dsp format core (make_coeff/mac/finalize/
    /// silence — see tap::dsp::sample_type) refined with this library's blend
    /// contract.
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

} // namespace tap::samplerate
