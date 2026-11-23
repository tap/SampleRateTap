// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The C ABI (capi/tap_sr_rational_capi.h) is the notebooks' seam, so it must
// be the shipping C++ exactly: every chain constant equals its named
// basic_chain bit for bit on the reference noise, in float, Q15 and Q31
// (a call in another format than the converter's is refused), with the same exact
// latency, MACs and accounting; the stage constructor equals a stage built
// at the same divisor; invalid arguments return NULL; and the version probe
// pins the family encoding (D13). The recursive entry points
// (create_iir_format, create_guard_band_format; docs/IIR_PLAN.md I11) are
// their C++ types the same way: every direction, factor, phase, form, kind,
// profile and format, outputs bit for bit, the latency members, the MACs,
// the refusals stated. Links the shipped shared library, so it exists
// wherever TAP_SR_BUILD_CAPI builds one.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "../capi/tap_sr_rational_capi.h"
#include "reference/reference_vectors.h"
#include "support/flush_to_zero.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
    using tap::dsp::exact_ratio;

    TEST(CApi, VersionIsBitPacked) {
        constexpr unsigned k_packed =
            static_cast<unsigned>((TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH);
        const unsigned v = tap_sr_rational_version();
        EXPECT_EQ(v, k_packed);
        EXPECT_EQ(v, 0x000900u); // 0.9.0
        EXPECT_EQ(v >> 16, 0u);
        EXPECT_EQ((v >> 8) & 0xFFu, 9u);
        EXPECT_EQ(v & 0xFFu, 0u);
    }

    // The ABI's typed entry points by sample type.
    std::size_t abi_process(tap_sr_rational_converter* c, const float* in, std::size_t n, float* out) {
        return tap_sr_rational_process(c, in, n, out);
    }
    std::size_t abi_process(tap_sr_rational_converter* c, const std::int16_t* in, std::size_t n, std::int16_t* out) {
        return tap_sr_rational_process_q15(c, in, n, out);
    }
    std::size_t abi_process(tap_sr_rational_converter* c, const std::int32_t* in, std::size_t n, std::int32_t* out) {
        return tap_sr_rational_process_q31(c, in, n, out);
    }
    std::size_t abi_flush(tap_sr_rational_converter* c, float* out) {
        return tap_sr_rational_flush(c, out);
    }
    std::size_t abi_flush(tap_sr_rational_converter* c, std::int16_t* out) {
        return tap_sr_rational_flush_q15(c, out);
    }
    std::size_t abi_flush(tap_sr_rational_converter* c, std::int32_t* out) {
        return tap_sr_rational_flush_q31(c, out);
    }

    template <typename S>
    constexpr int format_tag() {
        return std::is_same_v<S, float>          ? TAP_SR_RATIONAL_FORMAT_FLOAT
               : std::is_same_v<S, std::int16_t> ? TAP_SR_RATIONAL_FORMAT_Q15
                                                 : TAP_SR_RATIONAL_FORMAT_Q31;
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

    /// The ABI's converter against the C++ chain, on the reference noise
    /// (stereo, the second channel negated) in the chain's format: outputs
    /// bit-identical, the accounting, latency and MACs equal, flush equal; a
    /// call in another format is refused and leaves the converter untouched.
    template <typename Chain>
    void expect_abi_is_the_chain(tap_sr_rational_converter* c, Chain& ref, const char* name) {
        using sample = typename Chain::sample;
        ASSERT_NE(c, nullptr) << name;
        EXPECT_EQ(tap_sr_rational_format(c), format_tag<sample>()) << name;
        const auto&         n = rational_ref::k_input;
        std::vector<sample> x(2 * n.size());
        for (std::size_t i = 0; i < n.size(); ++i) {
            x[2 * i]     = to_sample<sample>(n[i]);
            x[2 * i + 1] = to_sample<sample>(-n[i]);
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

        if constexpr (!std::is_same_v<sample, float>) {
            std::vector<float> fx(2 * n.size()), fy(2 * n.size() * 8 + 64);
            EXPECT_EQ(tap_sr_rational_process(c, fx.data(), n.size(), fy.data()), 0u) << name << ": wrong format";
            EXPECT_EQ(tap_sr_rational_flush(c, fy.data()), 0u) << name << ": wrong format";
        }
        else {
            std::vector<std::int16_t> qx(2 * n.size()), qy(2 * n.size() * 8 + 64);
            EXPECT_EQ(tap_sr_rational_process_q15(c, qx.data(), n.size(), qy.data()), 0u) << name;
            EXPECT_EQ(tap_sr_rational_flush_q15(c, qy.data()), 0u) << name;
        }
        EXPECT_EQ(tap_sr_rational_outputs_for(c, n.size()), fresh) << name << ": the refused call left it untouched";
        std::vector<sample> ya(2 * ref.outputs_for(n.size()));
        std::vector<sample> yb(ya.size());
        ASSERT_EQ(abi_process(c, x.data(), n.size(), ya.data()), ya.size() / 2) << name;
        ASSERT_EQ(ref.process(x.data(), n.size(), yb.data()), yb.size() / 2) << name;
        EXPECT_TRUE(ya == yb) << name;
        const std::size_t   tail = static_cast<std::size_t>(tap_sr_rational_flush_output_frames(c));
        std::vector<sample> fa(2 * tail);
        std::vector<sample> fb(2 * ref.flush_output_frames());
        ASSERT_EQ(fa.size(), fb.size()) << name;
        EXPECT_EQ(abi_flush(c, fa.data()), tail) << name;
        ref.flush(fb.data());
        EXPECT_TRUE(fa == fb) << name;
        tap_sr_rational_reset(c);
        EXPECT_EQ(tap_sr_rational_outputs_for(c, n.size()), fresh) << name << ": reset returns to the fresh state";
    }

    template <typename S, rational_ratio... Rs>
    void expect_named_in(int chain, const char* name) {
        tap_sr_rational_converter* c = std::is_same_v<S, float>
                                           ? tap_sr_rational_create(chain, 0, 2)
                                           : tap_sr_rational_create_format(chain, 0, format_tag<S>(), 2);
        basic_chain<S, 2, Rs...>   ref(profile::economy());
        expect_abi_is_the_chain(c, ref, name);
        std::uint64_t num = 0, den = 0;
        tap_sr_rational_macs_per_output(c, &num, &den);
        EXPECT_EQ((exact_ratio{num, den}), ref.macs_per_output_exact()) << name;
        tap_sr_rational_destroy(c);
    }

    /// Every format: the float ABI, and Q15 and Q31 through create_format.
    template <rational_ratio... Rs>
    void expect_named(int chain, const char* name) {
        expect_named_in<float, Rs...>(chain, name);
        expect_named_in<std::int16_t, Rs...>(chain, name);
        expect_named_in<std::int32_t, Rs...>(chain, name);
    }

    TEST(CApi, EveryChainEnumeratorIsItsNamedChain) {
        expect_named<up2>(TAP_SR_RATIONAL_UP2, "up2");
        expect_named<down2>(TAP_SR_RATIONAL_DOWN2, "down2");
        expect_named<up3>(TAP_SR_RATIONAL_UP3, "up3");
        expect_named<down3>(TAP_SR_RATIONAL_DOWN3, "down3");
        expect_named<ratio3over2>(TAP_SR_RATIONAL_RATIO3OVER2, "ratio3over2");
        expect_named<ratio2over3>(TAP_SR_RATIONAL_RATIO2OVER3, "ratio2over3");
        expect_named<ratio4over3>(TAP_SR_RATIONAL_RATIO4OVER3, "ratio4over3");
        expect_named<ratio3over4>(TAP_SR_RATIONAL_RATIO3OVER4, "ratio3over4");
        expect_named<up2, up2>(TAP_SR_RATIONAL_UP2_UP2, "up2_up2");
        expect_named<up2, up3>(TAP_SR_RATIONAL_UP2_UP3, "up2_up3");
        expect_named<up2, up6>(TAP_SR_RATIONAL_UP2_UP6, "up2_up6");
        expect_named<up2, up8>(TAP_SR_RATIONAL_UP2_UP8, "up2_up8");
        expect_named<up2, ratio4over3>(TAP_SR_RATIONAL_UP2_RATIO4OVER3, "up2_ratio4over3");
        expect_named<ratio3over4, down2>(TAP_SR_RATIONAL_RATIO3OVER4_DOWN2, "ratio3over4_down2");
        expect_named<down2, down2>(TAP_SR_RATIONAL_DOWN2_DOWN2, "down2_down2");
        expect_named<down3, down2>(TAP_SR_RATIONAL_DOWN3_DOWN2, "down3_down2");
        expect_named<down6, down2>(TAP_SR_RATIONAL_DOWN6_DOWN2, "down6_down2");
        expect_named<down8, down2>(TAP_SR_RATIONAL_DOWN8_DOWN2, "down8_down2");
        expect_named<up2, up4>(TAP_SR_RATIONAL_UP2_UP4, "up2_up4");
        expect_named<up3, up4>(TAP_SR_RATIONAL_UP3_UP4, "up3_up4");
        expect_named<up2, up4, up2>(TAP_SR_RATIONAL_UP2_UP4_UP2, "up2_up4_up2");
        expect_named<up2, up3, up4>(TAP_SR_RATIONAL_UP2_UP3_UP4, "up2_up3_up4");
        expect_named<up2, up4, up4>(TAP_SR_RATIONAL_UP2_UP4_UP4, "up2_up4_up4");
        expect_named<up3, up4, up4>(TAP_SR_RATIONAL_UP3_UP4_UP4, "up3_up4_up4");
        expect_named<down4, down2>(TAP_SR_RATIONAL_DOWN4_DOWN2, "down4_down2");
        expect_named<down4, down3>(TAP_SR_RATIONAL_DOWN4_DOWN3, "down4_down3");
        expect_named<down2, down4, down2>(TAP_SR_RATIONAL_DOWN2_DOWN4_DOWN2, "down2_down4_down2");
        expect_named<down4, down3, down2>(TAP_SR_RATIONAL_DOWN4_DOWN3_DOWN2, "down4_down3_down2");
        expect_named<down4, down4, down2>(TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN2, "down4_down4_down2");
        expect_named<down4, down4, down3>(TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN3, "down4_down4_down3");
        expect_named<up2, ratio3over2>(TAP_SR_RATIONAL_UP2_RATIO3OVER2, "up2_ratio3over2");
        expect_named<up2, up6, up4>(TAP_SR_RATIONAL_UP2_UP6_UP4, "up2_up6_up4");
        expect_named<down4, down6, down2>(TAP_SR_RATIONAL_DOWN4_DOWN6_DOWN2, "down4_down6_down2");
    }

    // The values are stable (X7 of docs/X4_X8_PLAN.md): a retired chain's
    // value is never reused and create refuses it, CHAIN_END is one past the
    // largest value ever assigned, and every other value below it is a live
    // chain that constructs in every format.
    constexpr int k_retired_chains[] = {10, 11, 14, 15, 16, 21, 22, 25, 26, 27};
    constexpr int k_live_chains[]    = {
        TAP_SR_RATIONAL_UP2,
        TAP_SR_RATIONAL_DOWN2,
        TAP_SR_RATIONAL_UP3,
        TAP_SR_RATIONAL_DOWN3,
        TAP_SR_RATIONAL_RATIO3OVER2,
        TAP_SR_RATIONAL_RATIO2OVER3,
        TAP_SR_RATIONAL_RATIO4OVER3,
        TAP_SR_RATIONAL_RATIO3OVER4,
        TAP_SR_RATIONAL_UP2_UP2,
        TAP_SR_RATIONAL_UP2_UP3,
        TAP_SR_RATIONAL_UP2_UP6,
        TAP_SR_RATIONAL_UP2_UP8,
        TAP_SR_RATIONAL_UP2_RATIO4OVER3,
        TAP_SR_RATIONAL_RATIO3OVER4_DOWN2,
        TAP_SR_RATIONAL_DOWN2_DOWN2,
        TAP_SR_RATIONAL_DOWN3_DOWN2,
        TAP_SR_RATIONAL_DOWN6_DOWN2,
        TAP_SR_RATIONAL_DOWN8_DOWN2,
        TAP_SR_RATIONAL_UP2_UP4,
        TAP_SR_RATIONAL_UP3_UP4,
        TAP_SR_RATIONAL_UP2_UP4_UP2,
        TAP_SR_RATIONAL_UP2_UP3_UP4,
        TAP_SR_RATIONAL_UP2_UP4_UP4,
        TAP_SR_RATIONAL_UP3_UP4_UP4,
        TAP_SR_RATIONAL_DOWN4_DOWN2,
        TAP_SR_RATIONAL_DOWN4_DOWN3,
        TAP_SR_RATIONAL_DOWN2_DOWN4_DOWN2,
        TAP_SR_RATIONAL_DOWN4_DOWN3_DOWN2,
        TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN2,
        TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN3,
        TAP_SR_RATIONAL_UP2_RATIO3OVER2,
        TAP_SR_RATIONAL_UP2_UP6_UP4,
        TAP_SR_RATIONAL_DOWN4_DOWN6_DOWN2,
    };

    TEST(CApi, ChainValuesAreStableAndRetiredOnesAreRefused) {
        // X-a4 (X4) took 40 to 42 for the 120 dB tier's new chains.
        EXPECT_EQ(TAP_SR_RATIONAL_CHAIN_END, 43);
        EXPECT_EQ(std::size(k_live_chains), 33u);
        EXPECT_EQ(std::size(k_live_chains) + std::size(k_retired_chains),
                  static_cast<std::size_t>(TAP_SR_RATIONAL_CHAIN_END));
        for (int v = 0; v < TAP_SR_RATIONAL_CHAIN_END; ++v) {
            const bool live =
                std::find(std::begin(k_live_chains), std::end(k_live_chains), v) != std::end(k_live_chains);
            const bool retired =
                std::find(std::begin(k_retired_chains), std::end(k_retired_chains), v) != std::end(k_retired_chains);
            EXPECT_NE(live, retired) << v << ": every value below CHAIN_END is live or retired, never both";
            for (const int format :
                 {TAP_SR_RATIONAL_FORMAT_FLOAT, TAP_SR_RATIONAL_FORMAT_Q15, TAP_SR_RATIONAL_FORMAT_Q31}) {
                tap_sr_rational_converter* c = tap_sr_rational_create_format(v, 0, format, 1);
                if (live) {
                    EXPECT_NE(c, nullptr) << v << " format " << format;
                }
                else {
                    EXPECT_EQ(c, nullptr) << v << " format " << format << ": a retired value";
                }
                tap_sr_rational_destroy(c);
            }
        }
        EXPECT_EQ(tap_sr_rational_create(TAP_SR_RATIONAL_CHAIN_END, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(TAP_SR_RATIONAL_CHAIN_END + 1, 0, 1), nullptr);
        for (const int v : k_retired_chains) {
            EXPECT_EQ(tap_sr_rational_create(v, 0, 1), nullptr) << v;
            EXPECT_EQ(tap_sr_rational_create(v, 1, 2), nullptr) << v;
        }
    }

    // The chains by 4, 8 and 16 (X7 of docs/X4_X8_PLAN.md): each _BY constant
    // is its chain's constant, a second spelling of a live value and never a
    // value of its own (CHAIN_END and the live set above are unchanged), and
    // what it creates is chain.h's alias of the same name, at the profile its
    // tier is selected at, bit for bit.
    static_assert(TAP_SR_RATIONAL_DOWN_BY4 == TAP_SR_RATIONAL_DOWN2_DOWN2);
    static_assert(TAP_SR_RATIONAL_DOWN_BY8 == TAP_SR_RATIONAL_DOWN4_DOWN2);
    static_assert(TAP_SR_RATIONAL_DOWN_BY16 == TAP_SR_RATIONAL_DOWN2_DOWN4_DOWN2);
    static_assert(TAP_SR_RATIONAL_DOWN_BY16_TRANSPARENT == TAP_SR_RATIONAL_DOWN8_DOWN2);
    static_assert(TAP_SR_RATIONAL_UP_BY4 == TAP_SR_RATIONAL_UP2_UP2);
    static_assert(TAP_SR_RATIONAL_UP_BY8 == TAP_SR_RATIONAL_UP2_UP4);
    static_assert(TAP_SR_RATIONAL_UP_BY16 == TAP_SR_RATIONAL_UP2_UP4_UP2);
    static_assert(TAP_SR_RATIONAL_UP_BY16_TRANSPARENT == TAP_SR_RATIONAL_UP2_UP8);

    template <typename Alias>
    void expect_by(int chain, int profile_tag, const profile& p, const char* name) {
        EXPECT_NE(std::find(std::begin(k_live_chains), std::end(k_live_chains), chain), std::end(k_live_chains))
            << name << ": a live chain value";
        tap_sr_rational_converter* c = tap_sr_rational_create(chain, profile_tag, 2);
        Alias                      ref(p);
        expect_abi_is_the_chain(c, ref, name);
        std::uint64_t num = 0, den = 0;
        tap_sr_rational_macs_per_output(c, &num, &den);
        EXPECT_EQ((exact_ratio{num, den}), ref.macs_per_output_exact()) << name;
        tap_sr_rational_destroy(c);
    }

    TEST(CApi, ByConstantsAreTheirChainsConstants) {
        expect_by<down_by4<float, 2>>(TAP_SR_RATIONAL_DOWN_BY4, 0, profile::economy(), "down_by4");
        expect_by<down_by8<float, 2>>(TAP_SR_RATIONAL_DOWN_BY8, 0, profile::economy(), "down_by8");
        expect_by<down_by16<float, 2>>(TAP_SR_RATIONAL_DOWN_BY16, 0, profile::economy(), "down_by16");
        expect_by<down_by16_transparent<float, 2>>(TAP_SR_RATIONAL_DOWN_BY16_TRANSPARENT, 1, profile::transparent(),
                                                   "down_by16_transparent");
        expect_by<up_by4<float, 2>>(TAP_SR_RATIONAL_UP_BY4, 0, profile::economy(), "up_by4");
        expect_by<up_by8<float, 2>>(TAP_SR_RATIONAL_UP_BY8, 0, profile::economy(), "up_by8");
        expect_by<up_by16<float, 2>>(TAP_SR_RATIONAL_UP_BY16, 0, profile::economy(), "up_by16");
        expect_by<up_by16_transparent<float, 2>>(TAP_SR_RATIONAL_UP_BY16_TRANSPARENT, 1, profile::transparent(),
                                                 "up_by16_transparent");
        // By 4 and by 8 are both tiers' chain: the plain name at transparent.
        expect_by<down_by8<float, 2>>(TAP_SR_RATIONAL_DOWN_BY8, 1, profile::transparent(), "down_by8 at transparent");
        // And in the fixed-point formats, as every chain value is.
        tap_sr_rational_converter* q =
            tap_sr_rational_create_format(TAP_SR_RATIONAL_DOWN_BY16, 0, TAP_SR_RATIONAL_FORMAT_Q15, 2);
        down_by16<std::int16_t, 2> ref;
        expect_abi_is_the_chain(q, ref, "down_by16 in Q15");
        tap_sr_rational_destroy(q);
    }

    TEST(CApi, ProfilesSelectTheirDesigns) {
        const int     tags[] = {0, 1, 2, 3};
        const profile ps[]   = {profile::economy(), profile::transparent(), profile::balanced(),
                                profile::super_economy()};
        for (std::size_t i = 0; i < 4; ++i) {
            tap_sr_rational_converter* c = tap_sr_rational_create(TAP_SR_RATIONAL_DOWN2, tags[i], 1);
            ASSERT_NE(c, nullptr);
            EXPECT_EQ(tap_sr_rational_stage_taps(c, 0), (basic_stage<float, 1, down2>(ps[i]).taps())) << i;
            tap_sr_rational_destroy(c);
        }
    }

    /// The stage constructor is a stage at profile.relaxed(divisor).
    template <rational_ratio R, typename S = float>
    void expect_stage(unsigned l, unsigned m, int tag, const profile& p, exact_ratio d) {
        tap_sr_rational_converter* c = tap_sr_rational_create_stage_format(
            l, m, tag, static_cast<std::uint32_t>(d.num), static_cast<std::uint32_t>(d.den), format_tag<S>(), 2);
        tap::dsp::chain<basic_stage<S, 2, R>> ref(basic_stage<S, 2, R>(p.relaxed(d)));
        expect_abi_is_the_chain(c, ref, "stage");
        std::uint64_t num = 0, den = 0;
        tap_sr_rational_macs_per_output(c, &num, &den);
        const auto& s = ref.template stage<0>();
        EXPECT_EQ((exact_ratio{num, den}), (exact_ratio{s.macs_per_superblock(), R::k_up == 1 ? 1 : R::k_up}));
        tap_sr_rational_destroy(c);
    }

    TEST(CApi, StageConstructorIsTheStageAtTheDivisor) {
        expect_stage<up2>(2, 1, 0, profile::economy(), exact_ratio{147, 160}); // 48 -> 88.2's up 2 after bridge
        expect_stage<down2>(1, 2, 1, profile::transparent(), exact_ratio{2, 1});
        expect_stage<up8>(8, 1, 0, profile::economy(), exact_ratio{2, 1});
        expect_stage<ratio3over4>(3, 4, 0, profile::economy(), exact_ratio{2, 1});
        expect_stage<ratio2over3>(2, 3, 2, profile::balanced(), exact_ratio{1, 1});
        expect_stage<down6>(1, 6, 3, profile::super_economy(), exact_ratio{9, 1}); // a searched divisor
        // The 4th-band stage (X-a3): 192 -> 11.025's down 4 ahead of bridge
        // (divisor 4), 11.025 -> 384's up 4 after it (divisor 8), and the
        // single stages at their own rate.
        expect_stage<down4>(1, 4, 0, profile::economy(), exact_ratio{4, 1});
        expect_stage<up4>(4, 1, 0, profile::economy(), exact_ratio{8, 1});
        expect_stage<up4>(4, 1, 1, profile::transparent(), exact_ratio{1, 1});
        expect_stage<down4, std::int16_t>(1, 4, 0, profile::economy(), exact_ratio{1, 1});
        expect_stage<up4, std::int32_t>(4, 1, 2, profile::balanced(), exact_ratio{3, 1});
        expect_stage<ratio3over4, std::int16_t>(3, 4, 0, profile::economy(), exact_ratio{2, 1});
        expect_stage<down6, std::int16_t>(1, 6, 0, profile::economy(), exact_ratio{1, 1});
        expect_stage<up8, std::int32_t>(8, 1, 1, profile::transparent(), exact_ratio{2, 1});
        expect_stage<ratio3over8, std::int32_t>(3, 8, 0, profile::economy(), exact_ratio{1, 1});
        // create_stage is create_stage_format in float.
        tap_sr_rational_converter* c = tap_sr_rational_create_stage(2, 1, 0, 1, 1, 1);
        ASSERT_NE(c, nullptr);
        EXPECT_EQ(tap_sr_rational_format(c), TAP_SR_RATIONAL_FORMAT_FLOAT);
        tap_sr_rational_destroy(c);
    }

    // The 4th-band stage is one of the vocabulary since X-a3 (decision 6 as
    // amended; it was refused before), so the cross-family rows that run a
    // single down 4 or up 4 beside bridge can be built through the ABI.
    TEST(CApi, StageConstructorAcceptsTheFourthBand) {
        for (const int format :
             {TAP_SR_RATIONAL_FORMAT_FLOAT, TAP_SR_RATIONAL_FORMAT_Q15, TAP_SR_RATIONAL_FORMAT_Q31}) {
            for (const unsigned up : {1u, 4u}) {
                const unsigned             down = up == 1 ? 4u : 1u;
                tap_sr_rational_converter* c    = tap_sr_rational_create_stage_format(up, down, 0, 1, 1, format, 1);
                ASSERT_NE(c, nullptr) << up << "/" << down << " format " << format;
                unsigned l = 0, m = 0;
                tap_sr_rational_ratio(c, &l, &m);
                EXPECT_EQ(l, up);
                EXPECT_EQ(m, down);
                EXPECT_EQ(tap_sr_rational_stage_taps(c, 0), 79u); // N = 2 m B - 1, m = 10 at economy
                tap_sr_rational_destroy(c);
            }
        }
    }

    TEST(CApi, InvalidArgumentsReturnNull) {
        EXPECT_EQ(tap_sr_rational_create(-1, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(TAP_SR_RATIONAL_CHAIN_END, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(0, 4, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(0, -1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create(0, 0, 0), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(4, 3, 0, 1, 1, 0), nullptr); // no channel
        EXPECT_EQ(tap_sr_rational_create_stage(4, 2, 0, 1, 1, 1), nullptr); // not in lowest terms
        EXPECT_EQ(tap_sr_rational_create_stage(5, 1, 0, 1, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(2, 2, 0, 1, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(2, 1, 0, 0, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(2, 1, 0, 1, 0, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(2, 1, 9, 1, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage(258, 1, 0, 1, 1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_format(0, 0, 3, 1), nullptr); // unknown format
        EXPECT_EQ(tap_sr_rational_create_format(0, 0, -1, 1), nullptr);
        EXPECT_EQ(tap_sr_rational_create_stage_format(2, 1, 0, 1, 1, 7, 1), nullptr);
        EXPECT_EQ(TAP_SR_RATIONAL_FORMAT_FLOAT, 0);
        EXPECT_EQ(TAP_SR_RATIONAL_FORMAT_Q15, 1);
        EXPECT_EQ(TAP_SR_RATIONAL_FORMAT_Q31, 2);
        tap_sr_rational_destroy(nullptr); // the free() convention
    }

    // The fan-out (A13's K8, option (b)): at a count other than one or two the
    // C ABI runs that many one-channel converters behind the handle. Bit for
    // bit the native converter of that count, on N channels of the reference
    // noise (channel c scaled by (c + 1) / (N + 1), odd channels negated):
    // one call longer than the fan-out's 256-frame staging, then a short one,
    // the accounting from mid-stream, flush, and reset.
    template <typename S, std::size_t N, typename Native>
    void expect_fan_out_is_native(tap_sr_rational_converter* c, Native& ref, const char* name) {
        ASSERT_NE(c, nullptr) << name;
        const auto&    n = rational_ref::k_input;
        std::vector<S> x(N * n.size());
        for (std::size_t i = 0; i < n.size(); ++i) {
            for (std::size_t ch = 0; ch < N; ++ch) {
                const float sign  = ch % 2 == 0 ? 1.0F : -1.0F;
                const float scale = static_cast<float>(ch + 1) / static_cast<float>(N + 1);
                x[i * N + ch]     = to_sample<S>(sign * scale * n[i]);
            }
        }
        const std::size_t first = n.size() - 7;
        ASSERT_GT(first, std::size_t{256}) << name << ": longer than the staging";
        std::vector<S>    ya(N * (ref.outputs_for(n.size()) + 8));
        std::vector<S>    yb(ya.size());
        const std::size_t a1 = abi_process(c, x.data(), first, ya.data());
        const std::size_t b1 = ref.process(x.data(), first, yb.data());
        ASSERT_EQ(a1, b1) << name;
        const std::size_t a2 = abi_process(c, x.data() + N * first, 7, ya.data() + N * a1);
        const std::size_t b2 = ref.process(x.data() + N * first, 7, yb.data() + N * b1);
        ASSERT_EQ(a2, b2) << name;
        ya.resize(N * (a1 + a2));
        yb.resize(ya.size());
        EXPECT_TRUE(ya == yb) << name;
        EXPECT_EQ(tap_sr_rational_outputs_for(c, 100), ref.outputs_for(100)) << name;
        EXPECT_EQ(tap_sr_rational_frames_needed(c, 100), ref.frames_needed(100)) << name;
        std::uint64_t num = 0, den = 0;
        tap_sr_rational_latency_output_frames(c, &num, &den);
        EXPECT_EQ((exact_ratio{num, den}), ref.latency_output_frames()) << name;
        const std::size_t tail = static_cast<std::size_t>(tap_sr_rational_flush_output_frames(c));
        ASSERT_EQ(tail, ref.flush_output_frames()) << name;
        std::vector<S> fa(N * tail);
        std::vector<S> fb(N * tail);
        EXPECT_EQ(abi_flush(c, fa.data()), tail) << name;
        ref.flush(fb.data());
        EXPECT_TRUE(fa == fb) << name;
        tap_sr_rational_reset(c);
        ref.reset();
        EXPECT_EQ(tap_sr_rational_outputs_for(c, n.size()), ref.outputs_for(n.size())) << name;
        tap_sr_rational_destroy(c);
    }

    template <typename S, std::size_t N, rational_ratio... Rs>
    void expect_fan_out_chain(int tag, const char* name) {
        basic_chain<S, N, Rs...> ref(profile::economy());
        expect_fan_out_is_native<S, N>(tap_sr_rational_create_format(tag, 0, format_tag<S>(), N), ref, name);
    }

    template <typename S, std::size_t N>
    void expect_fan_out_at() {
        expect_fan_out_chain<S, N, up2>(TAP_SR_RATIONAL_UP2, "up2");
        expect_fan_out_chain<S, N, ratio2over3>(TAP_SR_RATIONAL_RATIO2OVER3, "ratio2over3");
        expect_fan_out_chain<S, N, down3, down2>(TAP_SR_RATIONAL_DOWN3_DOWN2, "down3_down2");
        // The stage constructor: up 2 at the 147/160 divisor.
        tap::dsp::chain<basic_stage<S, N, up2>> ref(basic_stage<S, N, up2>(profile::economy().relaxed({147, 160})));
        expect_fan_out_is_native<S, N>(tap_sr_rational_create_stage_format(2, 1, 0, 147, 160, format_tag<S>(), N), ref,
                                       "stage up2 at 147/160");
    }

    TEST(CApi, FanOutIsTheNativeConverterBitForBit) {
        expect_fan_out_at<float, 3>();
        expect_fan_out_at<float, 6>();
        expect_fan_out_at<float, 12>();
        expect_fan_out_at<std::int16_t, 3>();
        expect_fan_out_at<std::int16_t, 6>();
        expect_fan_out_at<std::int16_t, 12>();
        expect_fan_out_at<std::int32_t, 3>();
        expect_fan_out_at<std::int32_t, 6>();
        expect_fan_out_at<std::int32_t, 12>();
    }

    // ------------------------------------------------------------------
    // The recursive stages and the guard band filter (docs/IIR_PLAN.md I11)
    // ------------------------------------------------------------------

    /// The floating recursive stages take the host's flush-to-zero (I9): the
    /// fixture sets it on the calling thread, which the library runs on.
    class capi_recursive : public tap::sr::rational::test::flush_to_zero_fixture {};

    /// The group delay at f > 0 is a double the shared library and this test
    /// each compute from libm's sin / cos and libgcc's complex arithmetic, in
    /// two binaries: its last bits are not a contract, the outputs are. On
    /// CI's Linux GCC runner (Ubuntu 26.04, GCC 15.2) the two differed by up
    /// to 7.6e-14 relative at f = 0.05 to 0.15 (#117), while CI's Clang,
    /// AppleClang and MSVC builds and a local Ubuntu 26.04 GCC 15.2 build
    /// agree exactly, and at f = 0 (trig exact) every build agrees; hence a
    /// tolerance two orders above that, and exact equality at 0.
    constexpr double k_group_delay_rel = 1e-12;

    /// The ABI's converter against its C++ type on the reference noise in N
    /// channels (channel c scaled by (c + 1) / (N + 1), odd channels
    /// negated): one call longer than the fan-out's staging and a short one,
    /// outputs bit for bit, the accounting, the latency members (the nominal
    /// equal to the C++ one, the group delay within k_group_delay_rel of it;
    /// the exact one where the type has it, else refused as 0 / 0 and NaN
    /// seconds), the MACs, flush and reset; a call in another format is
    /// refused.
    template <typename S, std::size_t N, typename T>
    void expect_abi_is_recursive(tap_sr_rational_converter* c, T& ref, const char* name, double macs,
                                 std::size_t stages, std::size_t taps) {
        ASSERT_NE(c, nullptr) << name;
        EXPECT_EQ(tap_sr_rational_format(c), format_tag<S>()) << name;
        unsigned l = 0, m = 0;
        tap_sr_rational_ratio(c, &l, &m);
        EXPECT_EQ(l, T::k_up) << name;
        EXPECT_EQ(m, T::k_down) << name;
        EXPECT_EQ(tap_sr_rational_stages(c), stages) << name;
        EXPECT_EQ(tap_sr_rational_stage_taps(c, 0), taps) << name;
        EXPECT_EQ(tap_sr_rational_stage_taps(c, stages), 0u) << name;
        std::uint64_t num = 0, den = 0;
        tap_sr_rational_macs_per_output(c, &num, &den);
        EXPECT_EQ(static_cast<double>(num) / static_cast<double>(den), macs) << name;
        tap_sr_rational_latency_output_frames(c, &num, &den);
        if constexpr (requires { ref.latency_output_frames(); }) {
            EXPECT_EQ((exact_ratio{num, den}), ref.latency_output_frames()) << name;
            EXPECT_DOUBLE_EQ(tap_sr_rational_latency_seconds(c, 48000.0), ref.latency_seconds(48000.0)) << name;
            EXPECT_EQ(tap_sr_rational_nominal_latency_output_frames(c), ref.latency_output_frames().value()) << name;
            EXPECT_EQ(tap_sr_rational_group_delay_output_frames(c, 0.1), ref.latency_output_frames().value()) << name;
        }
        else {
            EXPECT_EQ(num, 0u) << name << ": no exact latency, refused";
            EXPECT_EQ(den, 0u) << name << ": no exact latency, refused";
            EXPECT_TRUE(std::isnan(tap_sr_rational_latency_seconds(c, 48000.0))) << name;
            EXPECT_EQ(tap_sr_rational_nominal_latency_output_frames(c), ref.nominal_latency_output_frames()) << name;
            EXPECT_EQ(tap_sr_rational_group_delay_output_frames(c, 0.0), ref.group_delay_output_frames(0.0)) << name;
            for (const double f : {0.05, 0.1, 0.15}) {
                const double want = ref.group_delay_output_frames(f);
                EXPECT_NEAR(tap_sr_rational_group_delay_output_frames(c, f), want, k_group_delay_rel * want)
                    << name << " at " << f;
            }
        }
        const auto&    n = rational_ref::k_input;
        std::vector<S> x(N * n.size());
        for (std::size_t i = 0; i < n.size(); ++i) {
            for (std::size_t ch = 0; ch < N; ++ch) {
                const float sign  = ch % 2 == 0 ? 1.0F : -1.0F;
                const float scale = static_cast<float>(ch + 1) / static_cast<float>(N + 1);
                x[i * N + ch]     = to_sample<S>(sign * scale * n[i]);
            }
        }
        const std::uint64_t fresh = tap_sr_rational_outputs_for(c, n.size());
        EXPECT_EQ(fresh, ref.outputs_for(n.size())) << name;
        EXPECT_EQ(tap_sr_rational_frames_needed(c, 100), ref.frames_needed(100)) << name;
        if constexpr (!std::is_same_v<S, float>) {
            std::vector<float> fx(N * n.size()), fy(N * n.size() * 16 + 64);
            EXPECT_EQ(tap_sr_rational_process(c, fx.data(), n.size(), fy.data()), 0u) << name << ": wrong format";
            EXPECT_EQ(tap_sr_rational_flush(c, fy.data()), 0u) << name << ": wrong format";
        }
        else {
            std::vector<std::int16_t> qx(N * n.size()), qy(N * n.size() * 16 + 64);
            EXPECT_EQ(tap_sr_rational_process_q15(c, qx.data(), n.size(), qy.data()), 0u) << name;
            EXPECT_EQ(tap_sr_rational_flush_q15(c, qy.data()), 0u) << name;
        }
        EXPECT_EQ(tap_sr_rational_outputs_for(c, n.size()), fresh) << name << ": the refused call left it untouched";
        const std::size_t first = n.size() - 7;
        std::vector<S>    ya(N * (ref.outputs_for(n.size()) + 8));
        std::vector<S>    yb(ya.size());
        const std::size_t a1 = abi_process(c, x.data(), first, ya.data());
        const std::size_t b1 = ref.process(x.data(), first, yb.data());
        ASSERT_EQ(a1, b1) << name;
        const std::size_t a2 = abi_process(c, x.data() + N * first, 7, ya.data() + N * a1);
        const std::size_t b2 = ref.process(x.data() + N * first, 7, yb.data() + N * b1);
        ASSERT_EQ(a2, b2) << name;
        ya.resize(N * (a1 + a2));
        yb.resize(ya.size());
        EXPECT_TRUE(ya == yb) << name;
        const std::size_t tail = static_cast<std::size_t>(tap_sr_rational_flush_output_frames(c));
        ASSERT_EQ(tail, ref.flush_output_frames()) << name;
        std::vector<S> fa(N * tail);
        std::vector<S> fb(N * tail);
        EXPECT_EQ(abi_flush(c, fa.data()), tail) << name;
        ref.flush(fb.data());
        EXPECT_TRUE(fa == fb) << name;
        tap_sr_rational_reset(c);
        ref.reset();
        EXPECT_EQ(tap_sr_rational_outputs_for(c, n.size()), ref.outputs_for(n.size())) << name;
        tap_sr_rational_destroy(c);
    }

    template <typename S, std::size_t N, iir_phase P, bool Up, std::size_t D>
    void expect_iir(const profile& p, int profile_tag, const char* name) {
        using stage_t = std::conditional_t<D == 2, basic_iir_stage<S, N, std::conditional_t<Up, up2, down2>, P>,
                                           basic_iir_cascade<S, N, P, D, Up>>;
        stage_t     ref(p);
        std::size_t stages = 1;
        if constexpr (D > 2) {
            stages = stage_t::k_stages;
        }
        tap_sr_rational_converter* c = tap_sr_rational_create_iir_format(
            Up ? TAP_SR_RATIONAL_DIRECTION_UP : TAP_SR_RATIONAL_DIRECTION_DOWN, static_cast<unsigned>(D),
            P == iir_phase::nonlinear ? TAP_SR_RATIONAL_IIR_NONLINEAR : TAP_SR_RATIONAL_IIR_NEAR_LINEAR, profile_tag,
            format_tag<S>(), static_cast<unsigned>(N));
        expect_abi_is_recursive<S, N>(c, ref, name, ref.macs_per_output(), stages, 0);
    }

    /// Both directions and both variants at factor D in format S, N channels.
    template <typename S, std::size_t N, std::size_t D>
    void expect_iir_at(const profile& p, int tag, const char* name) {
        expect_iir<S, N, iir_phase::nonlinear, true, D>(p, tag, name);
        expect_iir<S, N, iir_phase::nonlinear, false, D>(p, tag, name);
        expect_iir<S, N, iir_phase::near_linear, true, D>(p, tag, name);
        expect_iir<S, N, iir_phase::near_linear, false, D>(p, tag, name);
    }

    template <typename S>
    void expect_iir_in() {
        expect_iir_at<S, 2, 2>(profile::economy(), 0, "by 2 at economy");
        expect_iir_at<S, 2, 4>(profile::economy(), 0, "by 4 at economy");
        expect_iir_at<S, 2, 8>(profile::economy(), 0, "by 8 at economy");
        expect_iir_at<S, 2, 16>(profile::economy(), 0, "by 16 at economy");
        expect_iir_at<S, 1, 2>(profile::transparent(), 1, "by 2 at transparent, mono");
        expect_iir_at<S, 1, 2>(profile::balanced(), 2, "by 2 at balanced, mono");
        expect_iir_at<S, 1, 2>(profile::super_economy(), 3, "by 2 at super_economy, mono");
        expect_iir_at<S, 3, 2>(profile::economy(), 0, "by 2 at economy, 3 channels: the fan-out");
        expect_iir_at<S, 3, 8>(profile::economy(), 0, "by 8 at economy, 3 channels: the fan-out");
    }

    TEST_F(capi_recursive, EveryIirHandleIsItsStageOrCascadeBitForBit) {
        expect_iir_in<float>();
        expect_iir_in<std::int16_t>();
        expect_iir_in<std::int32_t>();
    }

    template <typename S, std::size_t N, guard_phase G>
    void expect_guard_band(int profile_tag, int kind_tag, half_band_kind kind, const char* name) {
        const guard_band_settings  s = profile_tag == 0   ? guard_band_settings::economy(kind)
                                       : profile_tag == 1 ? guard_band_settings::transparent(kind)
                                       : profile_tag == 2 ? guard_band_settings::balanced(kind)
                                                          : guard_band_settings::super_economy(kind);
        guard_band_filter<S, N, G> ref(s);
        std::size_t                taps = 0;
        if constexpr (G == guard_phase::linear) {
            taps = ref.taps();
        }
        tap_sr_rational_converter* c = tap_sr_rational_create_guard_band_format(
            G == guard_phase::nonlinear ? TAP_SR_RATIONAL_GUARD_NONLINEAR : TAP_SR_RATIONAL_GUARD_LINEAR, kind_tag,
            profile_tag, format_tag<S>(), static_cast<unsigned>(N));
        expect_abi_is_recursive<S, N>(c, ref, name, ref.macs_per_output(), 1, taps);
    }

    template <typename S, std::size_t N>
    void expect_guard_band_in() {
        const std::array<half_band_kind, 3> kinds = {half_band_kind::fir, half_band_kind::nonlinear,
                                                     half_band_kind::near_linear};
        const std::array<int, 3>            tags  = {TAP_SR_RATIONAL_HALF_BAND_FIR, TAP_SR_RATIONAL_HALF_BAND_NONLINEAR,
                                                     TAP_SR_RATIONAL_HALF_BAND_NEAR_LINEAR};
        for (int p = 0; p < 4; ++p) {
            for (std::size_t k = 0; k < 3; ++k) {
                expect_guard_band<S, N, guard_phase::nonlinear>(p, tags[k], kinds[k], "Chebyshev II");
                expect_guard_band<S, N, guard_phase::linear>(p, tags[k], kinds[k], "linear form");
            }
        }
    }

    TEST_F(capi_recursive, EveryGuardBandHandleIsItsFilterBitForBit) {
        expect_guard_band_in<float, 2>();
        expect_guard_band_in<std::int16_t, 2>();
        expect_guard_band_in<std::int32_t, 2>();
        expect_guard_band_in<float, 1>();
        expect_guard_band_in<std::int16_t, 3>();
    }

    TEST_F(capi_recursive, TheLatencyGettersAreStatedPerHandle) {
        // The exact getter: a chain's, the linear form's (N - 1) / 2; refused
        // (0 / 0, NaN seconds) for a recursive stage, the near-linear one
        // included (its exact delay path is its nominal, the double getter),
        // and for the Chebyshev II. The double getters answer for every
        // handle: an exact latency's value, or the C++ type's nominal and
        // group delay.
        std::uint64_t num = 1, den = 1;
        auto* li = tap_sr_rational_create_guard_band_format(TAP_SR_RATIONAL_GUARD_LINEAR, TAP_SR_RATIONAL_HALF_BAND_FIR,
                                                            0, TAP_SR_RATIONAL_FORMAT_FLOAT, 1);
        tap_sr_rational_latency_output_frames(li, &num, &den);
        EXPECT_EQ(num, 49u);
        EXPECT_EQ(den, 2u);
        EXPECT_EQ(tap_sr_rational_nominal_latency_output_frames(li), 24.5);
        tap_sr_rational_destroy(li);
        auto* q15 = tap_sr_rational_create_guard_band_format(
            TAP_SR_RATIONAL_GUARD_LINEAR, TAP_SR_RATIONAL_HALF_BAND_FIR, 2, TAP_SR_RATIONAL_FORMAT_Q15, 1);
        tap_sr_rational_latency_output_frames(q15, &num, &den);
        EXPECT_EQ(num, 59u) << "Q15 runs its Q15 row at balanced: 60 taps";
        EXPECT_EQ(den, 2u);
        tap_sr_rational_destroy(q15);
        auto* near = tap_sr_rational_create_iir_format(
            TAP_SR_RATIONAL_DIRECTION_DOWN, 2, TAP_SR_RATIONAL_IIR_NEAR_LINEAR, 0, TAP_SR_RATIONAL_FORMAT_FLOAT, 1);
        tap_sr_rational_latency_output_frames(near, &num, &den);
        EXPECT_EQ(num, 0u);
        EXPECT_EQ(den, 0u);
        EXPECT_EQ(tap_sr_rational_nominal_latency_output_frames(near), 7.0) << "the delay path, exact";
        EXPECT_NE(tap_sr_rational_group_delay_output_frames(near, 0.3), 7.0) << "the passband delay strays from it";
        tap_sr_rational_destroy(near);
        auto* cheb = tap_sr_rational_create_guard_band_format(
            TAP_SR_RATIONAL_GUARD_NONLINEAR, TAP_SR_RATIONAL_HALF_BAND_NONLINEAR, 0, TAP_SR_RATIONAL_FORMAT_FLOAT, 1);
        tap_sr_rational_latency_output_frames(cheb, &num, &den);
        EXPECT_EQ(den, 0u);
        EXPECT_NEAR(tap_sr_rational_nominal_latency_output_frames(cheb), 0.5353, 0.0005);
        tap_sr_rational_destroy(cheb);
        auto* fir = tap_sr_rational_create(TAP_SR_RATIONAL_DOWN2, 0, 1);
        tap_sr_rational_latency_output_frames(fir, &num, &den);
        EXPECT_EQ(den, 2u);
        EXPECT_EQ(tap_sr_rational_nominal_latency_output_frames(fir), static_cast<double>(num) / 2.0);
        EXPECT_EQ(tap_sr_rational_group_delay_output_frames(fir, 0.2), static_cast<double>(num) / 2.0);
        tap_sr_rational_destroy(fir);
    }

    TEST(CApi, InvalidRecursiveArgumentsReturnNull) {
        constexpr int up   = TAP_SR_RATIONAL_DIRECTION_UP;
        constexpr int nl   = TAP_SR_RATIONAL_IIR_NONLINEAR;
        constexpr int flt  = TAP_SR_RATIONAL_FORMAT_FLOAT;
        constexpr int cheb = TAP_SR_RATIONAL_GUARD_NONLINEAR;
        constexpr int fir  = TAP_SR_RATIONAL_HALF_BAND_FIR;
        EXPECT_EQ(tap_sr_rational_create_iir_format(2, 2, nl, 0, flt, 1), nullptr);  // direction
        EXPECT_EQ(tap_sr_rational_create_iir_format(-1, 2, nl, 0, flt, 1), nullptr); // direction
        for (const unsigned factor : {0u, 1u, 3u, 6u, 32u}) {
            EXPECT_EQ(tap_sr_rational_create_iir_format(up, factor, nl, 0, flt, 1), nullptr) << factor;
        }
        EXPECT_EQ(tap_sr_rational_create_iir_format(up, 2, 2, 0, flt, 1), nullptr);         // phase
        EXPECT_EQ(tap_sr_rational_create_iir_format(up, 2, nl, 4, flt, 1), nullptr);        // profile
        EXPECT_EQ(tap_sr_rational_create_iir_format(up, 2, nl, -1, flt, 1), nullptr);       // profile
        EXPECT_EQ(tap_sr_rational_create_iir_format(up, 2, nl, 0, 3, 1), nullptr);          // format
        EXPECT_EQ(tap_sr_rational_create_iir_format(up, 2, nl, 0, flt, 0), nullptr);        // channels
        EXPECT_EQ(tap_sr_rational_create_guard_band_format(2, fir, 0, flt, 1), nullptr);    // form
        EXPECT_EQ(tap_sr_rational_create_guard_band_format(cheb, 3, 0, flt, 1), nullptr);   // kind
        EXPECT_EQ(tap_sr_rational_create_guard_band_format(cheb, -1, 0, flt, 1), nullptr);  // kind
        EXPECT_EQ(tap_sr_rational_create_guard_band_format(cheb, fir, 4, flt, 1), nullptr); // profile
        EXPECT_EQ(tap_sr_rational_create_guard_band_format(cheb, fir, 0, -1, 1), nullptr);  // format
        EXPECT_EQ(tap_sr_rational_create_guard_band_format(cheb, fir, 0, flt, 0), nullptr); // channels
        EXPECT_EQ(TAP_SR_RATIONAL_DIRECTION_UP, 0);
        EXPECT_EQ(TAP_SR_RATIONAL_DIRECTION_DOWN, 1);
        EXPECT_EQ(TAP_SR_RATIONAL_IIR_NONLINEAR, 0);
        EXPECT_EQ(TAP_SR_RATIONAL_IIR_NEAR_LINEAR, 1);
        EXPECT_EQ(TAP_SR_RATIONAL_GUARD_NONLINEAR, 0);
        EXPECT_EQ(TAP_SR_RATIONAL_GUARD_LINEAR, 1);
        EXPECT_EQ(TAP_SR_RATIONAL_HALF_BAND_FIR, 0);
        EXPECT_EQ(TAP_SR_RATIONAL_HALF_BAND_NONLINEAR, 1);
        EXPECT_EQ(TAP_SR_RATIONAL_HALF_BAND_NEAR_LINEAR, 2);
    }

} // namespace
