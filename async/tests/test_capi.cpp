// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The C ABI (capi/tap_sr_async_capi.h) is the notebooks' seam, so it must be
// the shipping C++ exactly: a converter in each format — float, Q15, Q31 —
// pushes and pulls bit for bit what basic_converter<S> does on the same
// single-threaded sequence, with the same telemetry (a call in another
// format than the converter's is refused and moves nothing); every entry
// point tolerates NULL; invalid configuration returns NULL; and the version
// probe pins the family encoding (D13). Links the shipped shared library, so
// it exists wherever TAP_SR_BUILD_CAPI builds one.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "../capi/tap_sr_async_capi.h"
#include "tap/sr/async/async.h"

namespace {

    TEST(CApi, VersionIsBitPacked) {
        constexpr unsigned k_packed =
            static_cast<unsigned>((TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH);
        const unsigned v = tap_sr_async_version();
        EXPECT_EQ(v, k_packed);
        EXPECT_EQ(v, 0x000600u); // 0.6.0
        EXPECT_EQ(v >> 16, 0u);
        EXPECT_EQ((v >> 8) & 0xFFu, 6u);
        EXPECT_EQ(v & 0xFFu, 0u);
    }

    // The ABI's typed entry points by sample type.
    std::size_t abi_push(tap_sr_async_converter* h, const float* in, std::size_t n) {
        return tap_sr_async_push(h, in, n);
    }
    std::size_t abi_push(tap_sr_async_converter* h, const std::int16_t* in, std::size_t n) {
        return tap_sr_async_push_q15(h, in, n);
    }
    std::size_t abi_push(tap_sr_async_converter* h, const std::int32_t* in, std::size_t n) {
        return tap_sr_async_push_q31(h, in, n);
    }
    std::size_t abi_pull(tap_sr_async_converter* h, float* out, std::size_t n) {
        return tap_sr_async_pull(h, out, n);
    }
    std::size_t abi_pull(tap_sr_async_converter* h, std::int16_t* out, std::size_t n) {
        return tap_sr_async_pull_q15(h, out, n);
    }
    std::size_t abi_pull(tap_sr_async_converter* h, std::int32_t* out, std::size_t n) {
        return tap_sr_async_pull_q31(h, out, n);
    }

    template <typename S>
    constexpr int format_tag() {
        return std::is_same_v<S, float>          ? TAP_SR_ASYNC_FORMAT_FLOAT
               : std::is_same_v<S, std::int16_t> ? TAP_SR_ASYNC_FORMAT_Q15
                                                 : TAP_SR_ASYNC_FORMAT_Q31;
    }

    template <typename S>
    S to_sample(double v) {
        if constexpr (std::is_same_v<S, float>) {
            return static_cast<float>(v);
        }
        else {
            return static_cast<S>(std::llround(v * std::numeric_limits<S>::max()));
        }
    }

    /// Deterministic stereo noise at half scale (a fixed LCG; the second
    /// channel negated), `frames` frames.
    template <typename S>
    std::vector<S> noise(std::size_t frames, std::uint32_t& seed) {
        std::vector<S> x(2 * frames);
        for (std::size_t i = 0; i < frames; ++i) {
            seed           = seed * 1664525u + 1013904223u;
            const double v = (static_cast<double>(seed >> 8) / 16777216.0 - 0.5); // [-0.5, 0.5)
            x[2 * i]       = to_sample<S>(v);
            x[2 * i + 1]   = to_sample<S>(-v);
        }
        return x;
    }

    /// The ABI's converter against basic_converter<S> on one single-threaded
    /// push / pull sequence (the converter is deterministic given the
    /// sequence): every pull bit-identical, every return value and the
    /// telemetry equal; a call in another format refused and moving nothing.
    template <typename S>
    void expect_abi_is_the_converter(int profile, const tap::sr::async::filter_spec& spec) {
        constexpr double        k_fs    = 48000.0;
        constexpr std::size_t   k_block = 240; // the notebooks' largest block: the setpoint raise fits the default FIFO
        tap_sr_async_converter* h       = std::is_same_v<S, float>
                                              ? tap_sr_async_create(k_fs, 0, profile, 2)
                                              : tap_sr_async_create_format(k_fs, 0, profile, format_tag<S>(), 2);
        ASSERT_NE(h, nullptr) << profile;
        EXPECT_EQ(tap_sr_async_format(h), format_tag<S>());
        tap::sr::async::config cfg;
        cfg.sample_rate_hz = k_fs;
        cfg.channels       = 2;
        cfg.filter         = spec;
        tap::sr::async::basic_converter<S> ref(cfg);
        EXPECT_DOUBLE_EQ(tap_sr_async_designed_latency_seconds(h), ref.designed_latency_seconds());

        std::uint32_t  seed_a = 12345u, seed_b = 12345u;
        std::vector<S> ya(2 * k_block), yb(2 * k_block);
        for (int round = 0; round < 120; ++round) {
            // A little more input than output per round, so the FIFO fills
            // and the servo leaves Filling within the sequence.
            const std::size_t in_frames = k_block + (round < 8 ? 24 : 0);
            const auto        xa        = noise<S>(in_frames, seed_a);
            const auto        xb        = noise<S>(in_frames, seed_b);
            ASSERT_EQ(xa, xb);
            EXPECT_EQ(abi_push(h, xa.data(), in_frames), ref.push(xb.data(), in_frames)) << round;
            EXPECT_EQ(abi_pull(h, ya.data(), k_block), ref.pull(yb.data(), k_block)) << round;
            EXPECT_TRUE(ya == yb) << round;
        }
        double                                 s[6] = {-1, -1, -1, -1, -1, -1};
        const tap::sr::async::converter_status st   = ref.status();
        tap_sr_async_status(h, s);
        EXPECT_EQ(s[0], static_cast<double>(static_cast<int>(st.state)));
        EXPECT_EQ(s[1], st.ppm);
        EXPECT_EQ(s[2], st.fifo_fill_frames);
        EXPECT_EQ(s[3], static_cast<double>(st.underruns));
        EXPECT_EQ(s[4], static_cast<double>(st.overruns));
        EXPECT_EQ(s[5], static_cast<double>(st.resyncs));
        EXPECT_NE(static_cast<int>(st.state), 0) << "the sequence leaves Filling";

        // Another format's call: refused, nothing moved.
        const double fill_before = s[2];
        if constexpr (!std::is_same_v<S, float>) {
            std::vector<float> fx(2 * k_block, 0.25f), fy(2 * k_block, 7.0f);
            EXPECT_EQ(tap_sr_async_push(h, fx.data(), k_block), 0u) << "wrong format";
            EXPECT_EQ(tap_sr_async_pull(h, fy.data(), k_block), 0u) << "wrong format";
            EXPECT_EQ(fy[0], 7.0f) << "nothing written";
        }
        else {
            std::vector<std::int16_t> qx(2 * k_block, 1000), qy(2 * k_block, 7);
            EXPECT_EQ(tap_sr_async_push_q15(h, qx.data(), k_block), 0u);
            EXPECT_EQ(tap_sr_async_pull_q15(h, qy.data(), k_block), 0u);
            EXPECT_EQ(qy[0], 7);
            std::vector<std::int32_t> wx(2 * k_block, 1000), wy(2 * k_block, 7);
            EXPECT_EQ(tap_sr_async_push_q31(h, wx.data(), k_block), 0u);
            EXPECT_EQ(tap_sr_async_pull_q31(h, wy.data(), k_block), 0u);
            EXPECT_EQ(wy[0], 7);
        }
        tap_sr_async_status(h, s);
        EXPECT_EQ(s[2], fill_before) << "the refused calls left the FIFO untouched";

        tap_sr_async_reset_from_consumer(h);
        ref.reset_from_consumer();
        tap_sr_async_status(h, s);
        EXPECT_EQ(s[0], 0.0) << "Filling again";
        EXPECT_EQ(s[2], ref.status().fifo_fill_frames);
        tap_sr_async_destroy(h);
    }

    TEST(CApi, EveryFormatIsTheConverter) {
        expect_abi_is_the_converter<float>(1, tap::sr::async::filter_spec::balanced());
        expect_abi_is_the_converter<std::int16_t>(1, tap::sr::async::filter_spec::balanced());
        expect_abi_is_the_converter<std::int32_t>(1, tap::sr::async::filter_spec::balanced());
        expect_abi_is_the_converter<float>(0, tap::sr::async::filter_spec::fast());
        expect_abi_is_the_converter<std::int16_t>(2, tap::sr::async::filter_spec::transparent());
    }

    TEST(CApi, NullAndInvalidAreSoft) {
        EXPECT_EQ(tap_sr_async_create(-1.0, 0, 1, 2), nullptr);
        EXPECT_EQ(tap_sr_async_create(48000.0, 0, 1, 0), nullptr);
        EXPECT_EQ(tap_sr_async_create(48000.0, 0, 3, 2), nullptr); // profile outside 0..2 (audit F09)
        EXPECT_EQ(tap_sr_async_create(48000.0, 0, -1, 2), nullptr);
        EXPECT_EQ(tap_sr_async_create_format(48000.0, 0, 1, 3, 2), nullptr); // unknown format
        EXPECT_EQ(tap_sr_async_create_format(48000.0, 0, 1, -1, 2), nullptr);
        EXPECT_EQ(TAP_SR_ASYNC_FORMAT_FLOAT, 0);
        EXPECT_EQ(TAP_SR_ASYNC_FORMAT_Q15, 1);
        EXPECT_EQ(TAP_SR_ASYNC_FORMAT_Q31, 2);
        // Every entry point tolerates NULL (the shim's error convention).
        float        f[2] = {1.0f, 1.0f};
        std::int16_t q[2] = {1, 1};
        std::int32_t w[2] = {1, 1};
        double       s[6] = {1, 1, 1, 1, 1, 1};
        EXPECT_EQ(tap_sr_async_format(nullptr), 0);
        EXPECT_EQ(tap_sr_async_push(nullptr, f, 1), 0u);
        EXPECT_EQ(tap_sr_async_pull(nullptr, f, 1), 0u);
        EXPECT_EQ(tap_sr_async_push_q15(nullptr, q, 1), 0u);
        EXPECT_EQ(tap_sr_async_pull_q15(nullptr, q, 1), 0u);
        EXPECT_EQ(tap_sr_async_push_q31(nullptr, w, 1), 0u);
        EXPECT_EQ(tap_sr_async_pull_q31(nullptr, w, 1), 0u);
        tap_sr_async_status(nullptr, s);
        for (double v : s) {
            EXPECT_EQ(v, 0.0);
        }
        EXPECT_EQ(tap_sr_async_designed_latency_seconds(nullptr), 0.0);
        tap_sr_async_reset_from_consumer(nullptr);
        tap_sr_async_destroy(nullptr);
    }

} // namespace
