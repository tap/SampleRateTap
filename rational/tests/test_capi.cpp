// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The C ABI (capi/tap_sr_rational_capi.h) is the notebooks' seam, so it must
// be the shipping C++ exactly: every chain constant equals its named
// basic_chain bit for bit on the reference noise, with the same exact
// latency, MACs and accounting; the stage constructor equals a stage built
// at the same divisor; invalid arguments return NULL; and the version probe
// pins the family encoding (D13). Links the shipped shared library, so it
// exists wherever TAP_SR_BUILD_CAPI builds one.
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "../capi/tap_sr_rational_capi.h"
#include "reference/reference_vectors.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
    using tap::dsp::exact_ratio;

    TEST(CApi, VersionIsBitPacked) {
        constexpr unsigned k_packed =
            static_cast<unsigned>((TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH);
        const unsigned v = tap_sr_rational_version();
        EXPECT_EQ(v, k_packed);
        EXPECT_EQ(v, 0x000500u); // 0.5.0
        EXPECT_EQ(v >> 16, 0u);
        EXPECT_EQ((v >> 8) & 0xFFu, 5u);
        EXPECT_EQ(v & 0xFFu, 0u);
    }

    /// The ABI's converter against the C++ chain, on the reference noise
    /// (stereo, the second channel negated): outputs bit-identical, the
    /// accounting, latency and MACs equal, flush equal.
    template <typename Chain>
    void expect_abi_is_the_chain(tap_sr_rational_converter* c, Chain& ref, const char* name) {
        ASSERT_NE(c, nullptr) << name;
        const auto&        n = rational_ref::k_input;
        std::vector<float> x(2 * n.size());
        for (std::size_t i = 0; i < n.size(); ++i) {
            x[2 * i]     = n[i];
            x[2 * i + 1] = -n[i];
        }
        unsigned l = 0, m = 0;
        tap_sr_rational_ratio(c, &l, &m);
        EXPECT_EQ(l, Chain::k_up) << name;
        EXPECT_EQ(m, Chain::k_down) << name;
        EXPECT_EQ(tap_sr_rational_stages(c), Chain::k_stages) << name;
        EXPECT_EQ(tap_sr_rational_stage_taps(c, 0), ref.template stage<0>().taps()) << name;
        EXPECT_EQ(tap_sr_rational_stage_taps(c, Chain::k_stages), 0u) << name;
        const std::uint64_t fresh = tap_sr_rational_outputs_for(c, n.size());
        EXPECT_EQ(fresh, ref.outputs_for(n.size())) << name;
        EXPECT_EQ(tap_sr_rational_frames_needed(c, 100), ref.frames_needed(100)) << name;
        std::uint64_t num = 0, den = 0;
        tap_sr_rational_latency_output_frames(c, &num, &den);
        EXPECT_EQ((exact_ratio{num, den}), ref.latency_output_frames()) << name;
        EXPECT_DOUBLE_EQ(tap_sr_rational_latency_seconds(c, 48000.0), ref.latency_seconds(48000.0)) << name;

        std::vector<float> ya(2 * ref.outputs_for(n.size()));
        std::vector<float> yb(ya.size());
        ASSERT_EQ(tap_sr_rational_process(c, x.data(), n.size(), ya.data()), ya.size() / 2) << name;
        ASSERT_EQ(ref.process(x.data(), n.size(), yb.data()), yb.size() / 2) << name;
        EXPECT_TRUE(ya == yb) << name;
        const std::size_t  tail = static_cast<std::size_t>(tap_sr_rational_flush_output_frames(c));
        std::vector<float> fa(2 * tail);
        std::vector<float> fb(2 * ref.flush_output_frames());
        ASSERT_EQ(fa.size(), fb.size()) << name;
        EXPECT_EQ(tap_sr_rational_flush(c, fa.data()), tail) << name;
        ref.flush(fb.data());
        EXPECT_TRUE(fa == fb) << name;
        tap_sr_rational_reset(c);
        EXPECT_EQ(tap_sr_rational_outputs_for(c, n.size()), fresh) << name << ": reset returns to the fresh state";
    }

    template <rational_ratio... Rs>
    void expect_named(int chain, const char* name) {
        tap_sr_rational_converter* c = tap_sr_rational_create(chain, 0, 2);
        basic_chain<float, Rs...>  ref(2, profile::economy());
        expect_abi_is_the_chain(c, ref, name);
        std::uint64_t num = 0, den = 0;
        tap_sr_rational_macs_per_output(c, &num, &den);
        EXPECT_EQ((exact_ratio{num, den}), ref.macs_per_output_exact()) << name;
        tap_sr_rational_destroy(c);
    }

    TEST(CApi, EveryChainEnumeratorIsItsNamedChain) {
        expect_named<up_2>(TAP_SR_RATIONAL_UP_2, "up_2");
        expect_named<down_2>(TAP_SR_RATIONAL_DOWN_2, "down_2");
        expect_named<up_3>(TAP_SR_RATIONAL_UP_3, "up_3");
        expect_named<down_3>(TAP_SR_RATIONAL_DOWN_3, "down_3");
        expect_named<ratio_3_2>(TAP_SR_RATIONAL_RATIO_3_2, "ratio_3_2");
        expect_named<ratio_2_3>(TAP_SR_RATIONAL_RATIO_2_3, "ratio_2_3");
        expect_named<ratio_4_3>(TAP_SR_RATIONAL_RATIO_4_3, "ratio_4_3");
        expect_named<ratio_3_4>(TAP_SR_RATIONAL_RATIO_3_4, "ratio_3_4");
        expect_named<up_2, up_2>(TAP_SR_RATIONAL_UP_2_UP_2, "up_2_up_2");
        expect_named<up_2, up_3>(TAP_SR_RATIONAL_UP_2_UP_3, "up_2_up_3");
        expect_named<up_2, ratio_4_3, up_3>(TAP_SR_RATIONAL_UP_2_RATIO_4_3_UP_3, "up_2_ratio_4_3_up_3");
        expect_named<up_3, ratio_8_3>(TAP_SR_RATIONAL_UP_3_RATIO_8_3, "up_3_ratio_8_3");
        expect_named<up_2, up_6>(TAP_SR_RATIONAL_UP_2_UP_6, "up_2_up_6");
        expect_named<up_2, up_8>(TAP_SR_RATIONAL_UP_2_UP_8, "up_2_up_8");
        expect_named<up_2, up_6, up_2>(TAP_SR_RATIONAL_UP_2_UP_6_UP_2, "up_2_up_6_up_2");
        expect_named<up_2, up_8, up_2>(TAP_SR_RATIONAL_UP_2_UP_8_UP_2, "up_2_up_8_up_2");
        expect_named<up_2, up_8, up_3>(TAP_SR_RATIONAL_UP_2_UP_8_UP_3, "up_2_up_8_up_3");
        expect_named<up_2, ratio_4_3>(TAP_SR_RATIONAL_UP_2_RATIO_4_3, "up_2_ratio_4_3");
        expect_named<ratio_3_4, down_2>(TAP_SR_RATIONAL_RATIO_3_4_DOWN_2, "ratio_3_4_down_2");
        expect_named<down_2, down_2>(TAP_SR_RATIONAL_DOWN_2_DOWN_2, "down_2_down_2");
        expect_named<down_3, down_2>(TAP_SR_RATIONAL_DOWN_3_DOWN_2, "down_3_down_2");
        expect_named<down_3, ratio_3_4, down_2>(TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2, "down_3_ratio_3_4_down_2");
        expect_named<ratio_3_8, down_3>(TAP_SR_RATIONAL_RATIO_3_8_DOWN_3, "ratio_3_8_down_3");
        expect_named<down_6, down_2>(TAP_SR_RATIONAL_DOWN_6_DOWN_2, "down_6_down_2");
        expect_named<down_8, down_2>(TAP_SR_RATIONAL_DOWN_8_DOWN_2, "down_8_down_2");
        expect_named<down_2, down_6, down_2>(TAP_SR_RATIONAL_DOWN_2_DOWN_6_DOWN_2, "down_2_down_6_down_2");
        expect_named<down_2, down_8, down_2>(TAP_SR_RATIONAL_DOWN_2_DOWN_8_DOWN_2, "down_2_down_8_down_2");
        expect_named<down_3, down_8, down_2>(TAP_SR_RATIONAL_DOWN_3_DOWN_8_DOWN_2, "down_3_down_8_down_2");
        EXPECT_EQ(TAP_SR_RATIONAL_CHAIN_COUNT, 28);
    }

    TEST(CApi, ProfilesSelectTheirDesigns) {
        const int     tags[] = {0, 1, 2, 3};
        const profile ps[]   = {profile::economy(), profile::transparent(), profile::balanced(),
                                profile::super_economy()};
        for (std::size_t i = 0; i < 4; ++i) {
            tap_sr_rational_converter* c = tap_sr_rational_create(TAP_SR_RATIONAL_DOWN_2, tags[i], 1);
            ASSERT_NE(c, nullptr);
            EXPECT_EQ(tap_sr_rational_stage_taps(c, 0), (basic_stage<float, down_2>(1, ps[i]).taps())) << i;
            tap_sr_rational_destroy(c);
        }
    }

    /// The stage constructor is a stage at profile.relaxed(divisor).
    template <rational_ratio R>
    void expect_stage(unsigned l, unsigned m, int tag, const profile& p, exact_ratio d) {
        tap_sr_rational_converter* c = tap_sr_rational_create_stage(l, m, tag, static_cast<std::uint32_t>(d.num),
                                                                    static_cast<std::uint32_t>(d.den), 2);
        tap::dsp::chain<basic_stage<float, R>> ref(2, basic_stage<float, R>(2, p.relaxed(d)));
        expect_abi_is_the_chain(c, ref, "stage");
        std::uint64_t num = 0, den = 0;
        tap_sr_rational_macs_per_output(c, &num, &den);
        const auto& s = ref.template stage<0>();
        EXPECT_EQ((exact_ratio{num, den}), (exact_ratio{s.macs_per_superblock(), R::k_up == 1 ? 1 : R::k_up}));
        tap_sr_rational_destroy(c);
    }

    TEST(CApi, StageConstructorIsTheStageAtTheDivisor) {
        expect_stage<up_2>(2, 1, 0, profile::economy(), exact_ratio{147, 160}); // 48 -> 88.2's up 2 after bridge
        expect_stage<down_2>(1, 2, 1, profile::transparent(), exact_ratio{2, 1});
        expect_stage<up_8>(8, 1, 0, profile::economy(), exact_ratio{2, 1});
        expect_stage<ratio_3_4>(3, 4, 0, profile::economy(), exact_ratio{2, 1});
        expect_stage<ratio_2_3>(2, 3, 2, profile::balanced(), exact_ratio{1, 1});
        expect_stage<down_6>(1, 6, 3, profile::super_economy(), exact_ratio{9, 1}); // a searched divisor
    }

    TEST(CApi, InvalidArgumentsReturnNull) {
        EXPECT_EQ(tap_sr_rational_create(-1, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(TAP_SR_RATIONAL_CHAIN_COUNT, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(0, 4, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(0, -1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(0, 0, 0), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(4, 1, 0, 1, 1, 1), nullptr); // no single 4th-band stage (decision 6)
        EXPECT_EQ(tap_sr_rational_create_stage(5, 1, 0, 1, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(2, 2, 0, 1, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(2, 1, 0, 0, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(2, 1, 0, 1, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(2, 1, 9, 1, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(258, 1, 0, 1, 1, 1), nullptr);
        tap_sr_rational_destroy(nullptr); // the free() convention
    }

} // namespace
