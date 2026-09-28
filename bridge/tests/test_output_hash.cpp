// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// Output fingerprints of the converter, for same-job A/B comparison.
//
// Each test runs the streaming converter over a fixed two-channel multitone
// for both directions, every sample format (float, Q15, Q31) and one
// profile, and prints an FNV-1a-64 hash of the raw output bytes (process()
// output followed by flush()):
//
//   [ measured ] hash <direction>/<format>/<profile> <16 hex digits>
//
// The hashes are deliberately NOT pinned here. The integer paths are
// bit-exact by contract, but the prototype design runs through each
// platform's libm, and a float hash also depends on the compiler's
// multiply-add contraction; a hash is meaningful only against another hash
// from the same toolchain on the same host. The monorepo migration's gate
// G5 builds a reference tree and a candidate tree in one job and diffs
// these lines. What the test asserts is only that the output is
// non-trivial, so a silently zeroed datapath cannot hash stably.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "tap/ratio/converter.h"

namespace {

    using tap::ratio::basic_converter;
    using tap::ratio::direction;
    using tap::ratio::profile;

    constexpr std::size_t k_channels  = 2;
    constexpr std::size_t k_in_frames = 2940; // a whole number of 147-frame schedule periods

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
            const double scaled = std::nearbyint(v * static_cast<double>(std::numeric_limits<S>::max()));
            return static_cast<S>(scaled);
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
    // cross-channel leak changes the hash. Amplitudes stay well inside full
    // scale, so to_sample() never needs to saturate.
    template <class S>
    std::vector<S> multitone(double fs) {
        std::vector<S> x(k_in_frames * k_channels);
        for (std::size_t n = 0; n < k_in_frames; ++n) {
            const double t = static_cast<double>(n) / fs;
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

    template <class S, direction D>
    void hash_one(const char* profile_name, const profile& p) {
        basic_converter<S, D> conv(k_channels, p);
        const double          fs = D == direction::up_to_48k ? 44100.0 : 48000.0;
        const std::vector<S>  x  = multitone<S>(fs);
        std::vector<S>        y(static_cast<std::size_t>(conv.outputs_for(k_in_frames) + conv.flush_output_frames())
                                * k_channels);
        std::size_t           n = conv.process(x.data(), k_in_frames, y.data());
        n += conv.flush(y.data() + n * k_channels);
        y.resize(n * k_channels);
        ASSERT_GT(n, 0u);

        double energy = 0.0;
        for (const S v : y) {
            energy += static_cast<double>(v) * static_cast<double>(v);
        }
        EXPECT_GT(energy, 0.0) << "silent output would hash stably";

        std::printf("[ measured ] hash %s/%s/%s %016llx\n", D == direction::up_to_48k ? "up" : "down", format_name<S>(),
                    profile_name, static_cast<unsigned long long>(fnv1a64(y.data(), y.size() * sizeof(S))));
    }

    void hash_profile(const char* profile_name, const profile& p) {
        hash_one<float, direction::up_to_48k>(profile_name, p);
        hash_one<std::int16_t, direction::up_to_48k>(profile_name, p);
        hash_one<std::int32_t, direction::up_to_48k>(profile_name, p);
        hash_one<float, direction::down_to_44k1>(profile_name, p);
        hash_one<std::int16_t, direction::down_to_44k1>(profile_name, p);
        hash_one<std::int32_t, direction::down_to_44k1>(profile_name, p);
    }

    TEST(OutputHash, SuperEconomy) {
        hash_profile("super_economy", profile::super_economy());
    }
    TEST(OutputHash, Economy) {
        hash_profile("economy", profile::economy());
    }
    TEST(OutputHash, Balanced) {
        hash_profile("balanced", profile::balanced());
    }
    TEST(OutputHash, Transparent) {
        hash_profile("transparent", profile::transparent());
    }

} // namespace
