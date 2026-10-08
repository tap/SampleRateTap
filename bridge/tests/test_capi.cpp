// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The C ABI (capi/tap_sr_bridge_capi.h) is the notebooks' seam, so it must
// be the shipping C++ exactly: each direction equals its basic_converter bit
// for bit on the reference noise, in float, Q15 and Q31 (a call in another
// format than the converter's is refused), with the same accounting,
// latency and taps; invalid arguments return NULL; and the version probe
// pins the family encoding (D13). Links the shipped shared library, so it
// exists wherever TAP_SR_BUILD_CAPI builds one.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "../capi/tap_sr_bridge_capi.h"
#include "reference/reference_vectors.h"
#include "tap/sr/bridge/ratio.h"

namespace {

    using namespace tap::sr::bridge; // NOLINT(google-build-using-namespace)

    TEST(CApi, VersionIsBitPacked) {
        constexpr unsigned k_packed =
            static_cast<unsigned>((TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH);
        const unsigned v = tap_sr_bridge_version();
        EXPECT_EQ(v, k_packed);
        EXPECT_EQ(v, 0x000500u); // 0.5.0
        EXPECT_EQ(v >> 16, 0u);
        EXPECT_EQ((v >> 8) & 0xFFu, 5u);
        EXPECT_EQ(v & 0xFFu, 0u);
    }

    // The ABI's typed entry points by sample type.
    std::size_t abi_process(tap_sr_bridge_converter* c, const float* in, std::size_t n, float* out) {
        return tap_sr_bridge_process(c, in, n, out);
    }
    std::size_t abi_process(tap_sr_bridge_converter* c, const std::int16_t* in, std::size_t n, std::int16_t* out) {
        return tap_sr_bridge_process_q15(c, in, n, out);
    }
    std::size_t abi_process(tap_sr_bridge_converter* c, const std::int32_t* in, std::size_t n, std::int32_t* out) {
        return tap_sr_bridge_process_q31(c, in, n, out);
    }
    std::size_t abi_flush(tap_sr_bridge_converter* c, float* out) {
        return tap_sr_bridge_flush(c, out);
    }
    std::size_t abi_flush(tap_sr_bridge_converter* c, std::int16_t* out) {
        return tap_sr_bridge_flush_q15(c, out);
    }
    std::size_t abi_flush(tap_sr_bridge_converter* c, std::int32_t* out) {
        return tap_sr_bridge_flush_q31(c, out);
    }

    template <typename S>
    constexpr int format_tag() {
        return std::is_same_v<S, float>          ? TAP_SR_BRIDGE_FORMAT_FLOAT
               : std::is_same_v<S, std::int16_t> ? TAP_SR_BRIDGE_FORMAT_Q15
                                                 : TAP_SR_BRIDGE_FORMAT_Q31;
    }

    template <typename S>
    S to_sample(float v) {
        if constexpr (std::is_same_v<S, float>) {
            return v;
        }
        else {
            return static_cast<S>(std::llround(static_cast<double>(v) * std::numeric_limits<S>::max()));
        }
    }

    /// The ABI's converter against the C++ one, on the reference noise
    /// (stereo, the second channel negated) in the converter's format:
    /// outputs bit-identical, the accounting, latency and taps equal, flush
    /// equal; a call in another format is refused and leaves the converter
    /// untouched; reset returns to the fresh state.
    template <typename S, direction D>
    void expect_abi_is_the_converter(int dir_tag, int profile_tag, const profile& p) {
        tap_sr_bridge_converter* c = std::is_same_v<S, float>
                                         ? tap_sr_bridge_create(dir_tag, profile_tag, 2)
                                         : tap_sr_bridge_create_format(dir_tag, profile_tag, format_tag<S>(), 2);
        ASSERT_NE(c, nullptr) << dir_tag << " " << profile_tag;
        basic_converter<S, D> ref(2, p);
        EXPECT_EQ(tap_sr_bridge_format(c), format_tag<S>());
        const auto&    n = ratio_ref::k_input;
        std::vector<S> x(2 * n.size());
        for (std::size_t i = 0; i < n.size(); ++i) {
            x[2 * i]     = to_sample<S>(n[i]);
            x[2 * i + 1] = to_sample<S>(-n[i]);
        }
        EXPECT_EQ(tap_sr_bridge_taps(c), ref.taps());
        EXPECT_DOUBLE_EQ(tap_sr_bridge_latency_input_frames(c), ref.latency_input_frames());
        const std::uint64_t fresh = tap_sr_bridge_outputs_for(c, n.size());
        EXPECT_EQ(fresh, ref.outputs_for(n.size()));
        EXPECT_EQ(tap_sr_bridge_frames_needed(c, 100), ref.frames_needed(100));
        EXPECT_EQ(tap_sr_bridge_flush_output_frames(c), ref.flush_output_frames());

        if constexpr (!std::is_same_v<S, float>) {
            std::vector<float> fx(2 * n.size()), fy(2 * n.size() * 2 + 64);
            EXPECT_EQ(tap_sr_bridge_process(c, fx.data(), n.size(), fy.data()), 0u) << "wrong format";
            EXPECT_EQ(tap_sr_bridge_flush(c, fy.data()), 0u) << "wrong format";
        }
        else {
            std::vector<std::int16_t> qx(2 * n.size()), qy(2 * n.size() * 2 + 64);
            EXPECT_EQ(tap_sr_bridge_process_q15(c, qx.data(), n.size(), qy.data()), 0u);
            EXPECT_EQ(tap_sr_bridge_flush_q15(c, qy.data()), 0u);
            std::vector<std::int32_t> wx(2 * n.size()), wy(2 * n.size() * 2 + 64);
            EXPECT_EQ(tap_sr_bridge_process_q31(c, wx.data(), n.size(), wy.data()), 0u);
            EXPECT_EQ(tap_sr_bridge_flush_q31(c, wy.data()), 0u);
        }
        EXPECT_EQ(tap_sr_bridge_outputs_for(c, n.size()), fresh) << "the refused call left it untouched";

        std::vector<S> ya(2 * ref.outputs_for(n.size()));
        std::vector<S> yb(ya.size());
        ASSERT_EQ(abi_process(c, x.data(), n.size(), ya.data()), ya.size() / 2);
        ASSERT_EQ(ref.process(x.data(), n.size(), yb.data()), yb.size() / 2);
        EXPECT_TRUE(ya == yb);
        const std::size_t tail = static_cast<std::size_t>(tap_sr_bridge_flush_output_frames(c));
        std::vector<S>    fa(2 * tail);
        std::vector<S>    fb(2 * ref.flush_output_frames());
        ASSERT_EQ(fa.size(), fb.size());
        EXPECT_EQ(abi_flush(c, fa.data()), tail);
        ref.flush(fb.data());
        EXPECT_TRUE(fa == fb);
        tap_sr_bridge_reset(c);
        EXPECT_EQ(tap_sr_bridge_outputs_for(c, n.size()), fresh) << "reset returns to the fresh state";
        tap_sr_bridge_destroy(c);
    }

    template <typename S>
    void expect_both_directions() {
        expect_abi_is_the_converter<S, direction::up_to_48k>(0, 0, profile::economy());
        expect_abi_is_the_converter<S, direction::down_to_44k1>(1, 0, profile::economy());
        expect_abi_is_the_converter<S, direction::up_to_48k>(0, 1, profile::transparent());
        expect_abi_is_the_converter<S, direction::down_to_44k1>(1, 2, profile::balanced());
        expect_abi_is_the_converter<S, direction::up_to_48k>(0, 3, profile::super_economy());
    }

    TEST(CApi, EveryDirectionAndFormatIsTheConverter) {
        expect_both_directions<float>();
        expect_both_directions<std::int16_t>();
        expect_both_directions<std::int32_t>();
    }

    TEST(CApi, InvalidArgumentsReturnNull) {
        EXPECT_EQ(tap_sr_bridge_create(2, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_bridge_create(-1, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_bridge_create(0, 4, 1), nullptr);
        EXPECT_EQ(tap_sr_bridge_create(0, -1, 1), nullptr);
        EXPECT_EQ(tap_sr_bridge_create(0, 0, 0), nullptr);
        EXPECT_EQ(tap_sr_bridge_create_format(0, 0, 3, 1), nullptr); // unknown format
        EXPECT_EQ(tap_sr_bridge_create_format(0, 0, -1, 1), nullptr);
        EXPECT_EQ(TAP_SR_BRIDGE_FORMAT_FLOAT, 0);
        EXPECT_EQ(TAP_SR_BRIDGE_FORMAT_Q15, 1);
        EXPECT_EQ(TAP_SR_BRIDGE_FORMAT_Q31, 2);
        tap_sr_bridge_destroy(nullptr); // the free() convention
    }

} // namespace
