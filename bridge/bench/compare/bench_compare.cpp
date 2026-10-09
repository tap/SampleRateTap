// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Computational comparison against general-purpose resamplers at the fixed
// rational ratio pair this engine exists for (docs/COMPARISON.md).
//
// Methodology: every engine converts the same float stereo signal in the
// same direction (48 -> 44.1, then 44.1 -> 48), streaming in 128-frame input
// blocks; the competitors take the ratio as 44100/48000 (an exact rational
// for every one of them), bridge takes it as its type. Items processed are
// output frames; items/s divided by the output rate is the x-realtime figure
// per stream.
//
// Quality pairing — matched spec, then frontier (docs/COMPARISON.md states
// the rule and the notebook measures every pick): a competitor's "matched"
// setting is its cheapest whose measured stopband is at least the tier's
// (70 dB economy, 120 dB transparent) and whose passband droops no more than
// 0.1 dB at the tier's edge (18 kHz, 20 kHz) in both directions; "frontier"
// is the library's best.
//   economy (70 dB, 18 kHz):      libsamplerate MEDIUM (its FASTEST droops
//                                 4 dB at 18 kHz), soxr 16-bit at a 0.816
//                                 passband (its lowest precision), r8brain
//                                 70 dB at a 12 % band, SpeexDSP quality 3
//   transparent (120 dB, 20 kHz): libsamplerate BEST, soxr HQ (20-bit),
//                                 r8brain 120 dB at a 6 % band, SpeexDSP
//                                 quality 9
//   frontier:                     libsamplerate BEST, soxr VHQ, r8brain
//                                 CDSPResampler24 (180.15 dB), SpeexDSP
//                                 quality 10
// plus libsamplerate FASTEST (below the ladder: 97 dB but a 17 kHz passband)
// for the record, and the fixed-point rows: bridge's Q15 economy beside
// SpeexDSP's FIXED_POINT build at quality 3 and 10.
//
// r8brain is mono per instance with double-precision I/O, so the harness runs
// one instance per channel and pays the float<->double (de)interleave inside
// the timed loop — the cost any float-interleaved caller pays to use it.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <type_traits>
#include <vector>

#include <CDSPResampler.h>
#include <benchmark/benchmark.h>
#include <samplerate.h>
#include <soxr.h>
// SpeexDSP twice: its fixed-point and float builds, the header read once per
// build under that build's prefix into its own namespace (the include guard
// undefined in between; tools/compare/speexdsp.cmake builds the two
// libraries). The header's speex_resampler_* names are macros over
// RANDOM_PREFIX, expanded where they are used, so the calls below name the
// prefixed symbols directly.
namespace speex_fixed {
#define OUTSIDE_SPEEX 1
#define EXPORT
#define RANDOM_PREFIX tap_sr_cmp_fixed
#define FIXED_POINT 1
#include <speex_resampler.h>
} // namespace speex_fixed
namespace speex_float {
#undef SPEEX_RESAMPLER_H
#undef RANDOM_PREFIX
#undef FIXED_POINT
#define RANDOM_PREFIX tap_sr_cmp_float
#define FLOATING_POINT 1
#include <speex_resampler.h>
} // namespace speex_float

#include "tap/sr/bridge/ratio.h"

namespace {

    constexpr std::size_t k_block = 128; // streaming input block, frames
    constexpr std::size_t k_ch    = 2;

    using tap::sr::bridge::direction;

    template <direction D>
    struct rates;
    template <>
    struct rates<direction::down_to_44k1> {
        static constexpr double k_in  = 48000.0;
        static constexpr double k_out = 44100.0;
    };
    template <>
    struct rates<direction::up_to_48k> {
        static constexpr double k_in  = 44100.0;
        static constexpr double k_out = 48000.0;
    };

    template <typename S>
    S to_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

    /// One second of a 997 Hz stereo sine at the input rate, cycled block by
    /// block, so input delivery costs the same (a bounded copy) for every engine.
    template <typename S>
    class input_tap {
      public:
        explicit input_tap(double rate)
            : m_frames(static_cast<std::size_t>(rate))
            , m_buf(m_frames * k_ch) {
            const double w = 2.0 * std::numbers::pi * 997.0 / rate;
            for (std::size_t i = 0; i < m_frames; ++i) {
                for (std::size_t c = 0; c < k_ch; ++c) {
                    m_buf[i * k_ch + c] = to_sample<S>(0.5 * std::sin(w * static_cast<double>(i)));
                }
            }
        }
        const S* run() {
            if (m_pos + k_block > m_frames) {
                m_pos = 0;
            }
            const S* p = m_buf.data() + m_pos * k_ch;
            m_pos += k_block;
            return p;
        }

      private:
        std::size_t    m_frames;
        std::vector<S> m_buf;
        std::size_t    m_pos = 0;
    };

    template <typename S, direction D>
    void bridge_bench(benchmark::State& state, const tap::sr::bridge::profile& p) {
        tap::sr::bridge::basic_converter<S, D> conv(k_ch, p);
        input_tap<S>                           in(rates<D>::k_in);
        std::vector<S>                         out(static_cast<std::size_t>(conv.outputs_for(k_block) + 1) * k_ch);
        std::int64_t                           frames = 0;
        for (auto _ : state) {
            const std::size_t got = conv.process(in.run(), k_block, out.data());
            benchmark::DoNotOptimize(out.data());
            frames += static_cast<std::int64_t>(got);
        }
        state.counters["latency_frames"] = conv.latency_input_frames();
        state.SetItemsProcessed(frames);
    }

    template <direction D>
    void lsr_bench(benchmark::State& state, int converter) {
        int        err = 0;
        SRC_STATE* src = src_new(converter, static_cast<int>(k_ch), &err);
        if (src == nullptr) {
            state.SkipWithError(src_strerror(err));
            return;
        }
        input_tap<float>   in(rates<D>::k_in);
        std::vector<float> out(2 * k_block * k_ch);
        std::int64_t       frames = 0;
        for (auto _ : state) {
            SRC_DATA d{};
            d.data_in       = in.run();
            d.input_frames  = static_cast<long>(k_block);
            d.data_out      = out.data();
            d.output_frames = static_cast<long>(2 * k_block);
            d.src_ratio     = rates<D>::k_out / rates<D>::k_in;
            if (src_process(src, &d) != 0 || d.input_frames_used != static_cast<long>(k_block)) {
                state.SkipWithError("src_process failed");
                break;
            }
            benchmark::DoNotOptimize(out.data());
            frames += d.output_frames_gen;
        }
        src_delete(src);
        state.SetItemsProcessed(frames);
    }

    // recipe >= 0: a named soxr recipe; recipe < 0: the custom spec
    // (precision in bits, passband edge as a fraction of Nyquist).
    template <direction D>
    void soxr_bench(benchmark::State& state, int recipe, double precision, double passband_end) {
        soxr_error_t         err = nullptr;
        const soxr_io_spec_t io  = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
        soxr_quality_spec_t  q   = soxr_quality_spec(recipe < 0 ? SOXR_HQ : static_cast<unsigned long>(recipe), 0);
        if (recipe < 0) {
            q.precision    = precision;
            q.passband_end = passband_end;
        }
        soxr_t soxr = soxr_create(rates<D>::k_in, rates<D>::k_out, static_cast<unsigned>(k_ch), &err, &io, &q, nullptr);
        if (err != nullptr) {
            state.SkipWithError(soxr_strerror(err));
            return;
        }
        input_tap<float>   in(rates<D>::k_in);
        std::vector<float> out(2 * k_block * k_ch);
        std::int64_t       frames = 0;
        for (auto _ : state) {
            std::size_t idone = 0;
            std::size_t odone = 0;
            if (soxr_process(soxr, in.run(), k_block, &idone, out.data(), 2 * k_block, &odone) != nullptr
                || idone != k_block) {
                state.SkipWithError("soxr_process failed");
                break;
            }
            benchmark::DoNotOptimize(out.data());
            frames += static_cast<std::int64_t>(odone);
        }
        state.counters["latency_frames"] = benchmark::Counter(soxr_delay(soxr), benchmark::Counter::kAvgThreads);
        soxr_delete(soxr);
        state.SetItemsProcessed(frames);
    }

    template <direction D>
    void r8b_bench(benchmark::State& state, double req_atten, double trans_band_pct) {
        std::vector<std::unique_ptr<r8b::CDSPResampler>> rs;
        for (std::size_t c = 0; c < k_ch; ++c) {
            rs.push_back(std::make_unique<r8b::CDSPResampler>(rates<D>::k_in, rates<D>::k_out,
                                                              static_cast<int>(k_block), trans_band_pct, req_atten,
                                                              r8b::fprLinearPhase));
        }
        input_tap<float>    in(rates<D>::k_in);
        std::vector<double> ch_in(k_block);
        std::vector<float>  out(4 * k_block * k_ch);
        std::int64_t        frames = 0;
        for (auto _ : state) {
            const float* block = in.run();
            int          got   = 0;
            for (std::size_t c = 0; c < k_ch; ++c) {
                for (std::size_t i = 0; i < k_block; ++i) {
                    ch_in[i] = block[i * k_ch + c];
                }
                double* op = nullptr;
                got        = rs[c]->process(ch_in.data(), static_cast<int>(k_block), op);
                for (int i = 0; i < got; ++i) {
                    out[static_cast<std::size_t>(i) * k_ch + c] = static_cast<float>(op[i]);
                }
            }
            benchmark::DoNotOptimize(out.data());
            frames += got;
        }
        // Input frames consumed before the first output frame appears.
        state.counters["latency_frames"] = rs[0]->getInLenBeforeOutPos(0);
        state.SetItemsProcessed(frames);
    }

    // SpeexDSP at a quality (0..10), interleaved, through one of its two
    // builds: the fixed-point build on Q15 samples, the float build on float.
    template <typename Api, typename S, direction D>
    void speex_bench(benchmark::State& state, int quality) {
        int   err = 0;
        auto* st  = Api::init(static_cast<spx_uint32_t>(k_ch), static_cast<spx_uint32_t>(rates<D>::k_in),
                              static_cast<spx_uint32_t>(rates<D>::k_out), quality, &err);
        if (st == nullptr) {
            state.SkipWithError("speex_resampler_init failed");
            return;
        }
        input_tap<S>   in(rates<D>::k_in);
        std::vector<S> out(4 * k_block * k_ch);
        std::int64_t   frames = 0;
        for (auto _ : state) {
            spx_uint32_t in_len  = static_cast<spx_uint32_t>(k_block);
            spx_uint32_t out_len = static_cast<spx_uint32_t>(4 * k_block);
            if (Api::process(st, in.run(), &in_len, out.data(), &out_len) != 0 || in_len != k_block) {
                state.SkipWithError("speex_resampler_process failed");
                break;
            }
            benchmark::DoNotOptimize(out.data());
            frames += out_len;
        }
        state.counters["latency_frames"] = Api::latency(st);
        Api::destroy(st);
        state.SetItemsProcessed(frames);
    }
    struct speex_fixed_api {
        static auto init(spx_uint32_t ch, spx_uint32_t in, spx_uint32_t out, int q, int* err) {
            return speex_fixed::tap_sr_cmp_fixed_resampler_init(ch, in, out, q, err);
        }
        static int process(speex_fixed::SpeexResamplerState* st, const std::int16_t* in, spx_uint32_t* in_len,
                           std::int16_t* out, spx_uint32_t* out_len) {
            return speex_fixed::tap_sr_cmp_fixed_resampler_process_interleaved_int(st, in, in_len, out, out_len);
        }
        static int latency(speex_fixed::SpeexResamplerState* st) {
            return speex_fixed::tap_sr_cmp_fixed_resampler_get_input_latency(st);
        }
        static void destroy(speex_fixed::SpeexResamplerState* st) {
            speex_fixed::tap_sr_cmp_fixed_resampler_destroy(st);
        }
    };
    struct speex_float_api {
        static auto init(spx_uint32_t ch, spx_uint32_t in, spx_uint32_t out, int q, int* err) {
            return speex_float::tap_sr_cmp_float_resampler_init(ch, in, out, q, err);
        }
        static int process(speex_float::SpeexResamplerState* st, const float* in, spx_uint32_t* in_len, float* out,
                           spx_uint32_t* out_len) {
            return speex_float::tap_sr_cmp_float_resampler_process_interleaved_float(st, in, in_len, out, out_len);
        }
        static int latency(speex_float::SpeexResamplerState* st) {
            return speex_float::tap_sr_cmp_float_resampler_get_input_latency(st);
        }
        static void destroy(speex_float::SpeexResamplerState* st) {
            speex_float::tap_sr_cmp_float_resampler_destroy(st);
        }
    };

    // The matched settings (the header's table), named once.
    constexpr double k_r8b_eco_atten = 70.0, k_r8b_eco_band = 12.0;
    constexpr double k_r8b_tr_atten = 120.0, k_r8b_tr_band = 6.0;
    constexpr double k_r8b_24_atten = 180.15, k_r8b_24_band = 2.0; // CDSPResampler24's preset
    constexpr double k_soxr_eco_precision = 16.0, k_soxr_eco_passband = 18000.0 / 22050.0;
    constexpr int    k_speex_eco = 3, k_speex_tr = 9, k_speex_best = 10;

// One set of benchmarks per direction; the macro keeps the two lists
// identical, which is the point.
#define TAP_SR_BRIDGE_CMP_DIRECTION(TAG, D)                                                                            \
    void BM_##TAG##_Bridge_Economy(benchmark::State& s) {                                                              \
        bridge_bench<float, D>(s, tap::sr::bridge::profile::economy());                                                \
    }                                                                                                                  \
    void BM_##TAG##_Bridge_Transparent(benchmark::State& s) {                                                          \
        bridge_bench<float, D>(s, tap::sr::bridge::profile::transparent());                                            \
    }                                                                                                                  \
    void BM_##TAG##_Bridge_Economy_Q15(benchmark::State& s) {                                                          \
        bridge_bench<std::int16_t, D>(s, tap::sr::bridge::profile::economy());                                         \
    }                                                                                                                  \
    void BM_##TAG##_LSR_Fastest(benchmark::State& s) {                                                                 \
        lsr_bench<D>(s, SRC_SINC_FASTEST);                                                                             \
    }                                                                                                                  \
    void BM_##TAG##_LSR_Medium(benchmark::State& s) {                                                                  \
        lsr_bench<D>(s, SRC_SINC_MEDIUM_QUALITY);                                                                      \
    }                                                                                                                  \
    void BM_##TAG##_LSR_Best(benchmark::State& s) {                                                                    \
        lsr_bench<D>(s, SRC_SINC_BEST_QUALITY);                                                                        \
    }                                                                                                                  \
    void BM_##TAG##_SOXR_Eco16(benchmark::State& s) {                                                                  \
        soxr_bench<D>(s, -1, k_soxr_eco_precision, k_soxr_eco_passband);                                               \
    }                                                                                                                  \
    void BM_##TAG##_SOXR_HQ(benchmark::State& s) {                                                                     \
        soxr_bench<D>(s, SOXR_HQ, 0.0, 0.0);                                                                           \
    }                                                                                                                  \
    void BM_##TAG##_SOXR_VHQ(benchmark::State& s) {                                                                    \
        soxr_bench<D>(s, SOXR_VHQ, 0.0, 0.0);                                                                          \
    }                                                                                                                  \
    void BM_##TAG##_R8B_Eco(benchmark::State& s) {                                                                     \
        r8b_bench<D>(s, k_r8b_eco_atten, k_r8b_eco_band);                                                              \
    }                                                                                                                  \
    void BM_##TAG##_R8B_Tr(benchmark::State& s) {                                                                      \
        r8b_bench<D>(s, k_r8b_tr_atten, k_r8b_tr_band);                                                                \
    }                                                                                                                  \
    void BM_##TAG##_R8B_24bit(benchmark::State& s) {                                                                   \
        r8b_bench<D>(s, k_r8b_24_atten, k_r8b_24_band);                                                                \
    }                                                                                                                  \
    void BM_##TAG##_SPEEX_Float_Q3(benchmark::State& s) {                                                              \
        speex_bench<speex_float_api, float, D>(s, k_speex_eco);                                                        \
    }                                                                                                                  \
    void BM_##TAG##_SPEEX_Float_Q9(benchmark::State& s) {                                                              \
        speex_bench<speex_float_api, float, D>(s, k_speex_tr);                                                         \
    }                                                                                                                  \
    void BM_##TAG##_SPEEX_Float_Q10(benchmark::State& s) {                                                             \
        speex_bench<speex_float_api, float, D>(s, k_speex_best);                                                       \
    }                                                                                                                  \
    void BM_##TAG##_SPEEX_Fixed_Q3(benchmark::State& s) {                                                              \
        speex_bench<speex_fixed_api, std::int16_t, D>(s, k_speex_eco);                                                 \
    }                                                                                                                  \
    void BM_##TAG##_SPEEX_Fixed_Q10(benchmark::State& s) {                                                             \
        speex_bench<speex_fixed_api, std::int16_t, D>(s, k_speex_best);                                                \
    }                                                                                                                  \
    BENCHMARK(BM_##TAG##_Bridge_Economy);                                                                              \
    BENCHMARK(BM_##TAG##_Bridge_Transparent);                                                                          \
    BENCHMARK(BM_##TAG##_Bridge_Economy_Q15);                                                                          \
    BENCHMARK(BM_##TAG##_LSR_Fastest);                                                                                 \
    BENCHMARK(BM_##TAG##_LSR_Medium);                                                                                  \
    BENCHMARK(BM_##TAG##_LSR_Best);                                                                                    \
    BENCHMARK(BM_##TAG##_SOXR_Eco16);                                                                                  \
    BENCHMARK(BM_##TAG##_SOXR_HQ);                                                                                     \
    BENCHMARK(BM_##TAG##_SOXR_VHQ);                                                                                    \
    BENCHMARK(BM_##TAG##_R8B_Eco);                                                                                     \
    BENCHMARK(BM_##TAG##_R8B_Tr);                                                                                      \
    BENCHMARK(BM_##TAG##_R8B_24bit);                                                                                   \
    BENCHMARK(BM_##TAG##_SPEEX_Float_Q3);                                                                              \
    BENCHMARK(BM_##TAG##_SPEEX_Float_Q9);                                                                              \
    BENCHMARK(BM_##TAG##_SPEEX_Float_Q10);                                                                             \
    BENCHMARK(BM_##TAG##_SPEEX_Fixed_Q3);                                                                              \
    BENCHMARK(BM_##TAG##_SPEEX_Fixed_Q10);

    TAP_SR_BRIDGE_CMP_DIRECTION(Down, direction::down_to_44k1)
    TAP_SR_BRIDGE_CMP_DIRECTION(Up, direction::up_to_48k)

} // namespace
