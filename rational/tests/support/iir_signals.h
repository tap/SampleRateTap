// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The signals and measurements the guard band filter's and the composition
// batteries share (test_guard_band.cpp, test_iir_composition.cpp): the
// formats' full scale, the reference input's Q15 integers in every format,
// deterministic noise and sines, a whole-stream run, the DTFT and the group
// delay of a sequence, and FNV-1a-64 for the bit pins. The same definitions
// as test_iir_stage.cpp's, which keeps its own.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>
#include <vector>

#include "tap/dsp/sample_traits.h"

namespace tap::sr::rational::test {

    /// Full scale of a format: 32768 (Q15), 2^31 (Q31), 1 (float, double).
    template <typename S>
    constexpr double k_full = std::is_same_v<S, std::int16_t>   ? 32768.0
                              : std::is_same_v<S, std::int32_t> ? 2147483648.0
                                                                : 1.0;

    template <typename S>
    double to_unit(S v) {
        return static_cast<double>(v) / k_full<S>;
    }

    /// The reference input's Q15 integer k as S: k / 32768, k, k * 2^16.
    template <typename S>
    S from_q15(std::int16_t k) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(static_cast<double>(k) / 32768.0);
        }
        else if constexpr (std::is_same_v<S, std::int16_t>) {
            return k;
        }
        else {
            return static_cast<S>(static_cast<std::int32_t>(k) * 65536);
        }
    }

    template <typename S>
    S from_unit(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * k_full<S>);
        }
    }

    /// Runs a stage or chain over a whole interleaved stream.
    template <typename Stage>
    std::vector<typename Stage::sample> run(Stage& st, const std::vector<typename Stage::sample>& in) {
        constexpr std::size_t               ch = Stage::channels();
        std::vector<typename Stage::sample> out(st.outputs_for(in.size() / ch) * ch);
        const std::size_t                   made = st.process(in.data(), in.size() / ch, out.data());
        out.resize(made * ch);
        return out;
    }

    /// Deterministic xorshift noise at `peak` of full scale.
    template <typename S>
    std::vector<S> noise(std::size_t n, std::uint32_t seed, double peak = 0.9) {
        std::vector<S> x(n);
        std::uint32_t  s = seed;
        for (auto& v : x) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            v = from_unit<S>(peak * (static_cast<double>(s % 65536u) - 32768.0) / 32768.0);
        }
        return x;
    }

    template <typename S>
    std::vector<S> sine(std::size_t n, double f, double amp) {
        std::vector<S> x(n);
        for (std::size_t i = 0; i < n; ++i) {
            x[i] = from_unit<S>(amp * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i)));
        }
        return x;
    }

    template <typename S>
    std::uint64_t fnv1a64(std::uint64_t h, const std::vector<S>& v) {
        for (const S s : v) {
            auto u = static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<S>>(s));
            for (std::size_t b = 0; b < sizeof(S); ++b) {
                h ^= (u >> (8 * b)) & 0xffu;
                h *= 0x100000001b3ull;
            }
        }
        return h;
    }
    inline constexpr std::uint64_t k_fnv_basis = 0xcbf29ce484222325ull;

    inline double db(double v) {
        return 20.0 * std::log10(std::max(v, 1e-300));
    }

    /// The DTFT of a sequence at f, cycles per sample.
    inline std::complex<double> dtft(const std::vector<double>& h, double f) {
        std::complex<double> acc = 0.0;
        const auto           w   = std::polar(1.0, -2.0 * std::numbers::pi * f);
        std::complex<double> e   = 1.0;
        for (const double v : h) {
            acc += v * e;
            e *= w;
        }
        return acc;
    }

    /// The group delay of a sequence at f (cycles per sample), in samples.
    inline double measured_group_delay(const std::vector<double>& h, double f) {
        std::complex<double> num = 0.0;
        std::complex<double> den = 0.0;
        for (std::size_t n = 0; n < h.size(); ++n) {
            const auto e = std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n));
            den += h[n] * e;
            num += static_cast<double>(n) * h[n] * e;
        }
        return std::real(num / den);
    }

} // namespace tap::sr::rational::test
