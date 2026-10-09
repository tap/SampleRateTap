// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Output fingerprints of the datapath, for same-job A/B comparison.
//
// Each test drives the fractional resampler (the async engine's datapath,
// with the rate pinned instead of servoed so the run is deterministic) over
// a fixed two-channel multitone, for every sample format (float, Q15, Q31),
// every filter profile and two rate offsets, and prints an FNV-1a-64 hash of
// the raw output bytes:
//
//   [ measured ] hash <format>/<profile>/<ppm> <16 hex digits>
//
// The hashes are deliberately NOT pinned here. The float path's result
// depends on whether the compiler contracts multiply-adds into FMA, and on
// the platform's libm through the design math: measured, the float hashes
// differ between x86-64 and the Cortex-M33 build, while the Q15/Q31 hashes
// are identical on both. A float hash is therefore meaningful only against
// another hash from the same toolchain on the same host: the monorepo
// migration's gate G5 builds a reference tree and a candidate tree in one
// job and diffs these lines. What the test itself asserts is only that the
// output is non-trivial, so a silently zeroed datapath cannot produce a
// stable-looking hash.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "tap/sr/async/converter.h"

namespace {

    constexpr double      k_fs            = 48000.0;
    constexpr std::size_t k_channels      = 2;
    constexpr std::size_t k_in_frames     = 3000;
    constexpr std::size_t k_out_frames    = 2048;
    constexpr double      k_ppm_offsets[] = {200.0, -350.0};

    std::uint64_t fnv1a64(const void* data, std::size_t bytes) {
        std::uint64_t h = 0xcbf29ce484222325ULL;
        const auto*   p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < bytes; ++i) {
            h ^= p[i];
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    template <class S>
    S to_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::sr::async::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

    template <class S>
    const char* format_name() {
        if constexpr (std::is_same_v<S, float>) {
            return "float";
        }
        else if constexpr (std::is_same_v<S, std::int16_t>) {
            return "q15";
        }
        else {
            return "q31";
        }
    }

    // Two channels with different tone sets, so a channel swap or a
    // cross-channel leak changes the hash.
    template <class S>
    std::vector<S> multitone() {
        std::vector<S> x(k_in_frames * k_channels);
        for (std::size_t n = 0; n < k_in_frames; ++n) {
            const double t = static_cast<double>(n) / k_fs;
            const double l = 0.30 * std::sin(2.0 * std::numbers::pi * 997.0 * t)
                             + 0.20 * std::sin(2.0 * std::numbers::pi * 6001.0 * t)
                             + 0.10 * std::sin(2.0 * std::numbers::pi * 17503.0 * t);
            const double r = 0.35 * std::sin(2.0 * std::numbers::pi * 441.0 * t + 0.3)
                             + 0.25 * std::sin(2.0 * std::numbers::pi * 12007.0 * t);
            x[n * k_channels]     = to_sample<S>(l);
            x[n * k_channels + 1] = to_sample<S>(r);
        }
        return x;
    }

    template <class S>
    void hash_one(const char* profile_name, const tap::sr::async::filter_spec& spec, double ppm) {
        const tap::sr::async::polyphase_filter_bank<S> bank(spec, k_fs);
        tap::sr::async::fractional_resampler<S>        rs(bank, k_channels);
        const std::vector<S>                           x   = multitone<S>();
        std::size_t                                    fed = 0;
        auto pop = [&](S* dst, std::size_t max_frames) noexcept -> std::size_t {
            std::size_t n = 0;
            for (; n < max_frames && fed < k_in_frames; ++n, ++fed) {
                for (std::size_t c = 0; c < k_channels; ++c) {
                    dst[n * k_channels + c] = x[fed * k_channels + c];
                }
            }
            return n;
        };
        ASSERT_TRUE(rs.prime(pop));
        std::vector<S> y(k_out_frames * k_channels);
        ASSERT_EQ(rs.process(y.data(), k_out_frames, ppm * 1e-6, pop), k_out_frames);

        double energy = 0.0;
        for (const S v : y) {
            energy += static_cast<double>(v) * static_cast<double>(v);
        }
        EXPECT_GT(energy, 0.0) << "silent output would hash stably";

        std::printf("[ measured ] hash %s/%s/%+.0f %016llx\n", format_name<S>(), profile_name, ppm,
                    static_cast<unsigned long long>(fnv1a64(y.data(), y.size() * sizeof(S))));
    }

    void hash_profile(const char* profile_name, const tap::sr::async::filter_spec& spec) {
        for (const double ppm : k_ppm_offsets) {
            hash_one<float>(profile_name, spec, ppm);
            hash_one<std::int16_t>(profile_name, spec, ppm);
            hash_one<std::int32_t>(profile_name, spec, ppm);
        }
    }

    TEST(OutputHash, Fast) {
        hash_profile("fast", tap::sr::async::filter_spec::fast());
    }
    TEST(OutputHash, Economy) {
        hash_profile("program", tap::sr::async::filter_spec::program());
    }
    TEST(OutputHash, Balanced) {
        hash_profile("balanced", tap::sr::async::filter_spec::balanced());
    }
    TEST(OutputHash, Transparent) {
        hash_profile("transparent", tap::sr::async::filter_spec::transparent());
    }

} // namespace
