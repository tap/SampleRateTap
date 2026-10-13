// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The footprint contract (README, "Footprint"): the heap one converter holds
// after construction, and the peak while it designs, as formulas in its
// geometry, both pinned against DspTap's heap meter exactly for every
// direction, rate pair, profile and format. Its own executable: the meter
// replaces the global operator new for the whole program (tests/CMakeLists.txt).
//
// With L phases (k_phases), R = ceil(L / 2) stored rows (the mirror half is
// not held), T taps per output (taps()), c the coefficient size, s the sample
// size, ch channels and V = sizeof(std::vector<S>):
//
//   held  = R T c                     the half phase table
//         + (T + 64) ch s             the history, 64 frames of append slack
//         + 16 ch s                   pull()'s pop scratch
//         + ch V                      the history's vectors
//   peak  = max(held, R T c + 8 L T + 8 T + q 8 T)
//           the design: the table, the double prototype, the row being
//           quantized, and (q = 1 for Q15 and Q31) the quantizer's remainder
//
// Each term is one allocation (the history one of (T + 64) s per channel), and
// the test counts it as the request std::allocator makes for it
// (heap_meter::allocator_request: MSVC's on x86 and x64 pads a request of 4096
// bytes or more by 39 bytes), so the pins hold on every host; the README's
// formulas are the stored bytes.
#define TAP_DSP_HEAP_METER_REPLACE_GLOBAL_NEW
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "tap/dsp/analysis/heap_meter.h"
#include "tap/sr/bridge/converter.h"

namespace {

    using tap::dsp::analysis::heap_meter;
    using tap::dsp::analysis::heap_stats;
    using namespace tap::sr::bridge;

    constexpr std::size_t k_hist_slack = 64; // basic_converter's append slack
    constexpr std::size_t k_pop_chunk  = 16; // and its pull() granularity

    // One allocation of n stored bytes, as std::allocator requests it.
    constexpr std::size_t request(std::size_t n) {
        return heap_meter::allocator_request(n);
    }

    template <typename S, direction D, unsigned K>
    struct formula {
        std::size_t held = 0;
        std::size_t peak = 0;
    };

    template <typename S, direction D, unsigned K>
    formula<S, D, K> footprint_formula(const basic_converter<S, D, K>& conv) {
        using conv_t         = basic_converter<S, D, K>;
        using coeff          = typename tap::dsp::sample_traits<S>::coeff;
        const std::size_t L  = conv_t::k_phases;
        const std::size_t R  = basic_phase_table<S, D, K>::k_stored;
        const std::size_t T  = conv.taps();
        const std::size_t c  = sizeof(coeff);
        const std::size_t s  = sizeof(S);
        const std::size_t ch = conv.channels();
        const std::size_t q  = std::is_integral_v<S> ? 1 : 0;
        formula<S, D, K>  f;
        f.held = request(R * T * c) + ch * request((T + k_hist_slack) * s) + request(k_pop_chunk * ch * s)
                 + request(ch * sizeof(std::vector<S>));
        f.peak = std::max(f.held, request(R * T * c) + request(8 * L * T) + request(8 * T) + q * request(8 * T));
        return f;
    }

    template <typename S, direction D, unsigned K>
    void check(const char* name, const profile& p, std::size_t ch) {
        using conv_t = basic_converter<S, D, K>;
        std::unique_ptr<conv_t> conv;
        heap_stats              st;
        {
            const heap_meter meter;
            conv = std::make_unique<conv_t>(ch, p);
            st   = meter.stats();
        }
        // The converter object is on the heap here; the formulas are what it owns.
        st.live_bytes -= sizeof(conv_t);
        st.peak_bytes -= sizeof(conv_t);
        const auto f = footprint_formula(*conv);
        EXPECT_EQ(st.live_bytes, f.held) << name << ' ' << ch << " ch";
        EXPECT_EQ(st.peak_bytes, f.peak) << name << ' ' << ch << " ch";
        std::printf("[ measured ] bridge %-22s %2zu ch: L %zu T %2zu, %6zu B held, %7zu B peak in construction "
                    "(sizeof(std::vector) %zu)\n",
                    name, ch, conv_t::k_phases, conv->taps(), st.live_bytes, st.peak_bytes, sizeof(std::vector<S>));
    }

    struct named_profile {
        const char* name;
        profile     p;
    };

    template <typename S, direction D, unsigned K>
    void check_every_profile(const char* pair) {
        ASSERT_TRUE(heap_meter::installed());
        const named_profile ladder[] = {{"super_economy", profile::super_economy()},
                                        {"economy", profile::economy()},
                                        {"balanced", profile::balanced()},
                                        {"transparent", profile::transparent()}};
        for (const auto& np : ladder) {
            if (!basic_converter<S, D, K>::validate(1, np.p)) {
                for (const std::size_t ch : {std::size_t{1}, std::size_t{2}, std::size_t{8}}) {
                    char name[64];
                    std::snprintf(name, sizeof name, "%s %s", pair, np.name);
                    check<S, D, K>(name, np.p, ch);
                }
            }
        }
    }

    TEST(Footprint, HeapIsTheFormulaUpFloat) {
        check_every_profile<float, direction::up_to_48k, 0>("f32 44k1->48k");
    }
    TEST(Footprint, HeapIsTheFormulaDownFloat) {
        check_every_profile<float, direction::down_to_44k1, 0>("f32 48k->44k1");
    }
    TEST(Footprint, HeapIsTheFormulaUpQ15) {
        check_every_profile<std::int16_t, direction::up_to_48k, 0>("q15 44k1->48k");
    }
    TEST(Footprint, HeapIsTheFormulaDownQ15) {
        check_every_profile<std::int16_t, direction::down_to_44k1, 0>("q15 48k->44k1");
    }
    TEST(Footprint, HeapIsTheFormulaUpQ31) {
        check_every_profile<std::int32_t, direction::up_to_48k, 0>("q31 44k1->48k");
    }
    TEST(Footprint, HeapIsTheFormulaDownQ31) {
        check_every_profile<std::int32_t, direction::down_to_44k1, 0>("q31 48k->44k1");
    }
    TEST(Footprint, HeapIsTheFormulaAtTheDoubledRates) {
        check_every_profile<float, direction::up_to_48k, 1>("f32 88k2->96k");
        check_every_profile<float, direction::down_to_44k1, 1>("f32 96k->88k2");
        check_every_profile<std::int16_t, direction::up_to_48k, 1>("q15 88k2->96k");
    }

    TEST(Footprint, ProcessingAllocatesNothing) {
        converter_to_48k   conv(2);
        std::vector<float> in(441 * 2, 0.25F);
        std::vector<float> out((conv.outputs_for(441) + 1) * 2);
        const heap_meter   meter;
        for (int i = 0; i < 100; ++i) {
            static_cast<void>(conv.process(in.data(), 441, out.data()));
        }
        conv.reset();
        static_cast<void>(conv.process(in.data(), 441, out.data()));
        EXPECT_EQ(meter.stats().allocations, 0U);
    }

} // namespace
