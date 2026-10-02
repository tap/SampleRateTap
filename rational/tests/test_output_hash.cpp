// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Output fingerprints of the stages, for same-job A/B comparison (bridge's
// test_output_hash.cpp reasoning): each test runs a stage over a fixed
// two-channel multitone for three ratios of the vocabulary, every sample
// format, the economy profile, and prints an FNV-1a-64 hash of the raw
// output bytes (process() output followed by flush()):
//
//   [ measured ] hash <ratio>/<format>/economy <16 hex digits>
//
// The hashes are deliberately NOT pinned: the integer paths are bit-exact
// by contract, but the design runs through each platform's libm and a float
// hash depends on the compiler's contraction, so a hash is meaningful only
// against another from the same toolchain on the same host. What is
// asserted is that the output is non-trivial.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)

    constexpr std::size_t k_channels  = 2;
    constexpr std::size_t k_in_frames = 2400; // whole superblocks of every ratio here

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
            return static_cast<S>(std::nearbyint(v * static_cast<double>(std::numeric_limits<S>::max())));
        }
    }

    template <class S>
    const char* format_name() {
        if constexpr (std::is_same_v<S, double>) {
            return "double";
        }
        else if constexpr (std::is_same_v<S, float>) {
            return "float";
        }
        else if constexpr (std::is_same_v<S, std::int16_t>) {
            return "q15";
        }
        else {
            return "q31";
        }
    }

    template <class S>
    std::vector<S> multitone() {
        std::vector<S> x(k_in_frames * k_channels);
        for (std::size_t i = 0; i < k_in_frames; ++i) {
            const double t = static_cast<double>(i) / 48000.0;
            const double a = 0.30 * std::sin(2.0 * std::numbers::pi * 997.0 * t)
                             + 0.20 * std::sin(2.0 * std::numbers::pi * 5003.0 * t);
            const double b = 0.25 * std::sin(2.0 * std::numbers::pi * 1999.0 * t)
                             + 0.15 * std::sin(2.0 * std::numbers::pi * 8011.0 * t);
            x[i * k_channels]     = to_sample<S>(a);
            x[i * k_channels + 1] = to_sample<S>(b);
        }
        return x;
    }

    template <class S, rational_ratio R>
    void hash_one(const char* ratio_name) {
        basic_stage<S, R> c(k_channels);
        const auto        x = multitone<S>();
        std::vector<S>    y((c.outputs_for(k_in_frames) + c.flush_output_frames()) * k_channels);
        const std::size_t made = c.process(x.data(), k_in_frames, y.data());
        const std::size_t tail = c.flush(y.data() + made * k_channels);
        y.resize((made + tail) * k_channels);
        double energy = 0.0;
        for (const S v : y) {
            energy += static_cast<double>(v) * static_cast<double>(v);
        }
        EXPECT_GT(energy, 0.0);
        std::printf("[ measured ] hash %s/%s/economy %016llx\n", ratio_name, format_name<S>(),
                    static_cast<unsigned long long>(fnv1a64(y.data(), y.size() * sizeof(S))));
    }

    template <typename S>
    class output_hash_test : public ::testing::Test {};
    using sample_types = ::testing::Types<double, float, std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(output_hash_test, sample_types, );

    TYPED_TEST(output_hash_test, PrintsHashes) {
        hash_one<TypeParam, up_2>("up_2");
        hash_one<TypeParam, down_3>("down_3");
        hash_one<TypeParam, ratio_2_3>("ratio_2_3");
    }

} // namespace
