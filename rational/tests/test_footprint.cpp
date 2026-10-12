// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The footprint contract (README, "Footprint"): the heap one stage or chain
// holds after construction, as a formula in its geometry, pinned against
// DspTap's heap meter exactly; a stage's peak while it designs too. Its own
// executable: the meter replaces the global operator new for the whole
// program (tests/CMakeLists.txt).
//
// A stage, with N = 2 m B - 1 taps (taps()), T the row length (row_length()),
// R the stored rows (k_rows: L, or M decimating), P the history lines per
// channel (M decimating, else 1), c the coefficient size, s the sample size,
// ch channels and V = sizeof(std::vector<S>):
//
//   held  = R T c                     the rows
//         + (T + 64) P ch s           the history, 64 frames of append slack
//         + 16 ch s                   pull()'s pop scratch
//         + P ch V                    the history's vectors
//   peak  = max(design, 8 N + held)   the double design h lives to the end of
//                                     the constructor (design: below)
//
// A chain holds its stages, plus a scratch between each pair (cap_i ch s, cap_i
// the most a 64-frame chunk can reach stage i with: tap::dsp::chain) and 64 ch
// s of zeros for its flush. A chain's peak depends on the order its
// stages are constructed in (unspecified for function arguments), so it is
// printed, not pinned.
#define TAP_DSP_HEAP_METER_REPLACE_GLOBAL_NEW
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "tap/dsp/analysis/heap_meter.h"
#include "tap/sr/rational/chain.h"
#include "tap/sr/rational/stage.h"

namespace {

    using tap::dsp::analysis::heap_meter;
    using tap::dsp::analysis::heap_stats;
    using namespace tap::sr::rational;

    constexpr std::size_t k_hist_slack   = 64; // basic_stage's append slack
    constexpr std::size_t k_pop_chunk    = 16; // and its pull() granularity
    constexpr std::size_t k_double_bytes = sizeof(double);

    template <typename Stage>
    std::size_t stage_held(const Stage& st) {
        using sample         = typename Stage::sample;
        using coeff          = typename tap::dsp::sample_traits<sample>::coeff;
        const std::size_t T  = st.row_length();
        const std::size_t P  = Stage::k_is_decimator ? Stage::k_down : 1;
        const std::size_t c  = sizeof(coeff);
        const std::size_t s  = sizeof(sample);
        const std::size_t ch = st.channels();
        return Stage::k_rows * T * c + (T + k_hist_slack) * P * ch * s + k_pop_chunk * ch * s
               + P * ch * sizeof(std::vector<sample>);
    }

    // The design's own peak, inside build_table (stage.h), for a pinned profile
    // (no length search): the double h of N taps is live throughout.
    template <typename Stage>
    std::size_t stage_design_peak(const Stage& st) {
        using sample        = typename Stage::sample;
        using coeff         = typename tap::dsp::sample_traits<sample>::coeff;
        const std::size_t N = st.taps();
        const std::size_t T = st.row_length();
        const std::size_t c = sizeof(coeff);
        const std::size_t d = k_double_bytes;
        const std::size_t h = d * N;
        if constexpr (!Stage::k_is_decimator) {
            // rows, then a double row and its quantized copy (and the
            // fixed-point quantizer's remainder) per phase
            const std::size_t q = std::is_integral_v<sample> ? 1 : 0;
            return h + Stage::k_rows * T * c + d * T + T * c + q * d * T;
        }
        else if constexpr (Stage::k_branch_quantized) {
            // Q15 decimator: the quantized taps, then per branch a double branch,
            // its Q15 copy and the quantizer's remainder; the rows after
            const std::size_t m = (N + 1) / (2 * Stage::k_band);
            return std::max(h + c * N + d * 2 * m + c * 2 * m + d * 2 * m, h + c * N + c * (N + 1));
        }
        else {
            // other decimators: the quantized taps and their scaled doubles
            // (and Q31's remainder), then the rows beside the taps
            const std::size_t q = std::is_same_v<sample, std::int32_t> ? 1 : 0;
            return std::max(h + c * N + d * N + q * d * N, h + c * N + c * (N + 1));
        }
    }

    template <typename Stage>
    heap_stats measure_owned(std::unique_ptr<Stage>& out, std::size_t ch, const profile& p) {
        heap_stats st;
        {
            const heap_meter meter;
            out = std::make_unique<Stage>(ch, p);
            st  = meter.stats();
        }
        st.live_bytes -= sizeof(Stage); // the object is on the heap here
        st.peak_bytes -= sizeof(Stage);
        return st;
    }

    struct named_profile {
        const char* name;
        profile     p;
    };

    std::vector<named_profile> ladder() {
        return {{"super_economy", profile::super_economy()},
                {"economy", profile::economy()},
                {"balanced", profile::balanced()},
                {"transparent", profile::transparent()}};
    }

    template <typename S, typename R>
    void check_stage(const char* fmt, const char* ratio_name) {
        using stage_t = basic_stage<S, R>;
        for (const auto& np : ladder()) {
            for (const std::size_t ch : {std::size_t{1}, std::size_t{2}}) {
                std::unique_ptr<stage_t> st;
                const auto               m    = measure_owned(st, ch, np.p);
                const std::size_t        held = stage_held(*st);
                EXPECT_EQ(m.live_bytes, held) << fmt << ' ' << ratio_name << ' ' << np.name << ' ' << ch;
                EXPECT_EQ(m.peak_bytes, std::max(stage_design_peak(*st), k_double_bytes * st->taps() + held))
                    << fmt << ' ' << ratio_name << ' ' << np.name << ' ' << ch;
                if (ch == 2) {
                    std::printf("[ measured ] rational stage %-3s %-9s %-13s 2 ch: N %3zu T %2zu, %6zu B held, %6zu "
                                "B peak in construction (sizeof(std::vector) %zu)\n",
                                fmt, ratio_name, np.name, st->taps(), st->row_length(), m.live_bytes, m.peak_bytes,
                                sizeof(std::vector<S>));
                }
            }
        }
    }

    template <typename S>
    void check_every_stage(const char* fmt) {
        ASSERT_TRUE(heap_meter::installed());
        check_stage<S, up_2>(fmt, "up_2");
        check_stage<S, down_2>(fmt, "down_2");
        check_stage<S, up_3>(fmt, "up_3");
        check_stage<S, down_3>(fmt, "down_3");
        check_stage<S, up_6>(fmt, "up_6");
        check_stage<S, down_6>(fmt, "down_6");
        check_stage<S, up_8>(fmt, "up_8");
        check_stage<S, down_8>(fmt, "down_8");
        check_stage<S, ratio_3_2>(fmt, "ratio_3_2");
        check_stage<S, ratio_2_3>(fmt, "ratio_2_3");
        check_stage<S, ratio_4_3>(fmt, "ratio_4_3");
        check_stage<S, ratio_3_4>(fmt, "ratio_3_4");
        check_stage<S, ratio_8_3>(fmt, "ratio_8_3");
        check_stage<S, ratio_3_8>(fmt, "ratio_3_8");
    }

    TEST(Footprint, StageHeapIsTheFormulaFloat) {
        check_every_stage<float>("f32");
    }
    TEST(Footprint, StageHeapIsTheFormulaQ15) {
        check_every_stage<std::int16_t>("q15");
    }
    TEST(Footprint, StageHeapIsTheFormulaQ31) {
        check_every_stage<std::int32_t>("q31");
    }

    // tap::dsp::chain's scratch: cap_i over every entry stage E <= i.
    template <typename Chain>
    std::size_t chain_held(const Chain& c) {
        using sample                 = typename Chain::sample;
        constexpr std::size_t k      = Chain::k_stages;
        std::size_t           held   = 0;
        std::size_t           cap[k] = {};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((held += stage_held(c.template stage<I>())), ...);
            constexpr std::size_t up[k]   = {std::remove_cvref_t<decltype(c.template stage<I>())>::k_up...};
            constexpr std::size_t down[k] = {std::remove_cvref_t<decltype(c.template stage<I>())>::k_down...};
            for (std::size_t e = 0; e < k; ++e) {
                std::size_t x = Chain::k_block;
                for (std::size_t i = e; i < k; ++i) {
                    x      = x * up[i] / down[i] + 2;
                    cap[i] = std::max(cap[i], x);
                }
            }
        }(std::make_index_sequence<k>{});
        for (std::size_t i = 0; i + 1 < k; ++i) {
            held += cap[i] * c.channels() * sizeof(sample);
        }
        return held + Chain::k_block * c.channels() * sizeof(sample);
    }

    template <typename Chain>
    void check_chain(const char* name) {
        for (const auto& np : ladder()) {
            std::unique_ptr<Chain> c;
            heap_stats             m;
            {
                const heap_meter meter;
                c = std::make_unique<Chain>(2, np.p);
                m = meter.stats();
            }
            m.live_bytes -= sizeof(Chain);
            m.peak_bytes -= sizeof(Chain);
            EXPECT_EQ(m.live_bytes, chain_held(*c)) << name << ' ' << np.name;
            EXPECT_GE(m.peak_bytes, m.live_bytes);
            std::printf("[ measured ] rational chain f32 %-24s %-13s 2 ch: %6zu B held, %6zu B peak in construction\n",
                        name, np.name, m.live_bytes, m.peak_bytes);
        }
    }

    TEST(Footprint, ChainHeapIsItsStagesAndScratch) {
        ASSERT_TRUE(heap_meter::installed());
        check_chain<up_2_up_2<float>>("up_2_up_2");
        check_chain<up_2_up_3<float>>("up_2_up_3");
        check_chain<up_2_ratio_4_3_up_3<float>>("up_2_ratio_4_3_up_3");
        check_chain<up_3_ratio_8_3<float>>("up_3_ratio_8_3");
        check_chain<up_2_up_6<float>>("up_2_up_6");
        check_chain<up_2_up_8<float>>("up_2_up_8");
        check_chain<up_2_up_6_up_2<float>>("up_2_up_6_up_2");
        check_chain<up_2_up_8_up_2<float>>("up_2_up_8_up_2");
        check_chain<up_2_up_8_up_3<float>>("up_2_up_8_up_3");
        check_chain<up_2_ratio_4_3<float>>("up_2_ratio_4_3");
        check_chain<ratio_3_4_down_2<float>>("ratio_3_4_down_2");
        check_chain<down_2_down_2<float>>("down_2_down_2");
        check_chain<down_3_down_2<float>>("down_3_down_2");
        check_chain<down_3_ratio_3_4_down_2<float>>("down_3_ratio_3_4_down_2");
        check_chain<ratio_3_8_down_3<float>>("ratio_3_8_down_3");
        check_chain<down_6_down_2<float>>("down_6_down_2");
        check_chain<down_8_down_2<float>>("down_8_down_2");
        check_chain<down_2_down_6_down_2<float>>("down_2_down_6_down_2");
        check_chain<down_2_down_8_down_2<float>>("down_2_down_8_down_2");
        check_chain<down_3_down_8_down_2<float>>("down_3_down_8_down_2");
        check_chain<down_3_down_2<std::int16_t>>("down_3_down_2 (q15)");
        check_chain<up_2_up_2<std::int32_t>>("up_2_up_2 (q31)");
    }

    TEST(Footprint, ProcessingAllocatesNothing) {
        up_2_ratio_4_3_up_3<float> c(2);
        std::vector<float>         in(256 * 2, 0.25F);
        std::vector<float>         out(c.outputs_for(256) * 2 + 64);
        const heap_meter           meter;
        for (int i = 0; i < 50; ++i) {
            static_cast<void>(c.process(in.data(), 256, out.data()));
        }
        c.reset();
        static_cast<void>(c.process(in.data(), 256, out.data()));
        EXPECT_EQ(meter.stats().allocations, 0U);
    }

} // namespace
