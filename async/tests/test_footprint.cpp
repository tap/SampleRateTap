// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The footprint contract (README, "Footprint"): the heap one converter holds
// after construction, as a formula in its geometry, pinned against DspTap's
// heap meter exactly, for every profile and format; and the peak during
// construction, printed as measured. Its own executable: the meter replaces
// the global operator new for the whole program (tests/CMakeLists.txt).
//
// Persistent bytes, with L phases and T taps per phase (filter_bank()), c the
// coefficient size and s the sample size (sample_traits), ch channels, F the
// ring's frames (fifo_frames, or max(1024, 4 (target_latency_frames + T))) and
// V = sizeof(std::vector<S>):
//
//   (L + 1) T c                       the polyphase table, one guard row
// + bit_ceil(F ch) s                  the SPSC ring
// + 16 ch s                           the pop scratch (16-frame chunks)
// + (T + 16) ch s                     the filter history
// + T c                               the blended row
// + H V                               the history's vectors: H = ch, or 1 frame-major
//
// The peak while constructing is the larger of that and the design's: with
// n = L T prototype taps, a plain Kaiser design (fast) peaks at the double
// prototype, the table and one double row (8 n + (L + 1) T c + 8 T, plus the
// fixed-point quantizer's remainder, 8 T); the image-compensated design the
// other profiles run (tap::dsp::design_prototype_compensated) holds the
// prototype beside its own working set W, two more n-double buffers and its
// grids, pinned below as measured (8 n + W). W is DspTap's: a bump that moves
// it moves these pins and re-records them with the reason.
#define TAP_DSP_HEAP_METER_REPLACE_GLOBAL_NEW
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "tap/dsp/analysis/heap_meter.h"
#include "tap/sr/async/converter.h"

namespace {

    using tap::dsp::analysis::heap_meter;
    using tap::dsp::analysis::heap_stats;
    using namespace tap::sr::async;

    constexpr std::size_t k_chunk_frames = 16; // basic_converter's pop chunk

    // The compensated designer's working set beyond the prototype, measured
    // (bytes): balanced (L 256, T 48), program (512, 32), transparent (512, 80).
    std::size_t compensated_working_set(const filter_spec& f) {
        if (f.num_phases == 256 && f.taps_per_phase == 48) {
            return 201752;
        }
        if (f.num_phases == 512 && f.taps_per_phase == 32) {
            return 262712;
        }
        if (f.num_phases == 512 && f.taps_per_phase == 80) {
            return 657528;
        }
        ADD_FAILURE() << "no pinned working set for L " << f.num_phases << " T " << f.taps_per_phase;
        return 0;
    }

    template <typename S>
    std::size_t peak_formula(std::size_t held, const config& cfg) {
        using coeff                    = typename sample_traits<S>::coeff;
        const std::size_t L            = cfg.filter.num_phases;
        const std::size_t T            = cfg.filter.taps_per_phase;
        const std::size_t n            = L * T;
        const std::size_t q            = std::is_integral_v<S> ? 1 : 0;
        const std::size_t table_phase  = 8 * n + (L + 1) * T * sizeof(coeff) + 8 * T + q * 8 * T;
        const std::size_t design_phase = cfg.filter.image_zeros ? 8 * n + compensated_working_set(cfg.filter) : 0;
        return std::max({held, table_phase, design_phase});
    }

    template <typename S>
    std::size_t persistent_formula(const basic_converter<S>& conv, const config& cfg) {
        using coeff          = typename sample_traits<S>::coeff;
        const std::size_t L  = conv.filter_bank().num_phases();
        const std::size_t T  = conv.filter_bank().taps();
        const std::size_t c  = sizeof(coeff);
        const std::size_t s  = sizeof(S);
        const std::size_t ch = cfg.channels;
        const std::size_t F =
            cfg.fifo_frames != 0 ? cfg.fifo_frames : std::max<std::size_t>(1024, 4 * (cfg.target_latency_frames + T));
        const bool frame_major = fractional_resampler<S>::k_channel_parallel && ch >= TAP_SR_ASYNC_CP_MIN_CHANNELS;
        const std::size_t H    = frame_major ? 1 : ch;
        return (L + 1) * T * c + std::bit_ceil(F * ch) * s + k_chunk_frames * ch * s + (T + k_chunk_frames) * ch * s
               + T * c + H * sizeof(std::vector<S>);
    }

    struct measured {
        heap_stats  stats;
        std::size_t formula = 0;
        std::size_t taps    = 0;
        std::size_t phases  = 0;
    };

    template <typename S>
    measured measure(const config& cfg) {
        measured                            m;
        std::unique_ptr<basic_converter<S>> conv;
        {
            const heap_meter meter;
            conv    = std::make_unique<basic_converter<S>>(cfg);
            m.stats = meter.stats();
        }
        // The converter object itself is on the heap here, so it is in the
        // count; the formula is the heap it owns.
        m.stats.live_bytes -= sizeof(basic_converter<S>);
        m.stats.peak_bytes -= sizeof(basic_converter<S>);
        m.formula = persistent_formula(*conv, cfg);
        m.taps    = conv->filter_bank().taps();
        m.phases  = conv->filter_bank().num_phases();
        return m;
    }

    struct named_profile {
        const char* name;
        filter_spec spec;
    };

    std::vector<named_profile> profiles() {
        return {{"fast", filter_spec::fast()},
                {"balanced", filter_spec::balanced()},
                {"transparent", filter_spec::transparent()},
                {"program", filter_spec::program()}};
    }

    template <typename S>
    void check_every_profile(const char* format) {
        ASSERT_TRUE(heap_meter::installed());
        for (const auto& p : profiles()) {
            for (const std::size_t ch : {std::size_t{1}, std::size_t{2}, std::size_t{12}}) {
                config cfg;
                cfg.channels = ch;
                cfg.filter   = p.spec;
                const auto m = measure<S>(cfg);
                EXPECT_EQ(m.stats.live_bytes, m.formula) << format << ' ' << p.name << ' ' << ch << " ch";
                EXPECT_EQ(m.stats.peak_bytes, peak_formula<S>(m.formula, cfg))
                    << format << ' ' << p.name << ' ' << ch << " ch";
                std::printf("[ measured ] async %-4s %-11s %2zu ch: L %zu T %zu, %zu B held, %zu B peak in "
                            "construction (sizeof(std::vector) %zu)\n",
                            format, p.name, ch, m.phases, m.taps, m.stats.live_bytes, m.stats.peak_bytes,
                            sizeof(std::vector<S>));
            }
        }
    }

    TEST(Footprint, HeapIsTheFormulaForEveryProfileFloat) {
        check_every_profile<float>("f32");
    }
    TEST(Footprint, HeapIsTheFormulaForEveryProfileQ15) {
        check_every_profile<std::int16_t>("q15");
    }
    TEST(Footprint, HeapIsTheFormulaForEveryProfileQ31) {
        check_every_profile<std::int32_t>("q31");
    }

    TEST(Footprint, AnExplicitFifoSizesTheRing) {
        config cfg;
        cfg.fifo_frames = 3000; // bit_ceil(6000) = 8192 samples
        const auto m    = measure<float>(cfg);
        EXPECT_EQ(m.stats.live_bytes, m.formula);
    }

    TEST(Footprint, ProcessingAllocatesNothing) {
        config                 cfg;
        basic_converter<float> conv(cfg);
        std::vector<float>     in(64 * cfg.channels, 0.25F);
        std::vector<float>     out(64 * cfg.channels);
        const heap_meter       meter;
        for (int i = 0; i < 200; ++i) {
            conv.push(in.data(), 64);
            conv.pull(out.data(), 64);
            static_cast<void>(conv.status());
        }
        conv.reset_from_consumer();
        conv.pull(out.data(), 64);
        EXPECT_EQ(meter.stats().allocations, 0U);
    }

} // namespace
