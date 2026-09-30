// Computational comparison against general-purpose resamplers at a fixed,
// known near-unity ratio (docs/COMPARISON.md).
//
// Methodology: every engine converts the same float signal at the same
// fixed ratio (48000 -> 48000*(1+200e-6)), streaming in 128-frame blocks.
// SampleRateTap runs its datapath (fractional_resampler) with a constant
// rate deviation — the servo is quiescent at a fixed ratio, and the
// competitors take the ratio as an input rather than estimating it, so
// this is the apples-to-apples configuration. Items processed are output
// frames; items/s divided by 48000 is the ×realtime figure per stream.
//
// Quality pairing (stopband attenuation, vendor-stated):
//   srt balanced (120 dB)     ~ libsamplerate MEDIUM (121 dB) ~ soxr HQ (~120 dB)
//                             ~ r8brain ReqAtten=120 (default 2% transition band,
//                               and 8%: the lowest-latency setting flat to 20 kHz)
//   srt transparent (140 dB)  ~ libsamplerate BEST (144 dB)   ~ soxr VHQ (~170 dB)
//                             ~ r8brain CDSPResampler16 (136.45 dB)
// plus r8brain's CDSPResampler24 (180.15 dB), its preset for 24-bit/float work.
//
// r8brain is mono per instance with double-precision I/O, so the harness runs
// one instance per channel and pays the float<->double (de)interleave inside
// the timed loop — the cost any float-interleaved caller pays to use it.
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <vector>

#include <CDSPResampler.h>
#include <benchmark/benchmark.h>
#include <samplerate.h>
#include <soxr.h>

#include "tap/sr/async/polyphase_filter.h"
#include "tap/sr/async/sample_traits.h"

namespace {

    constexpr double      kRatio = 1.0 + 200e-6; // output rate / input rate
    constexpr std::size_t kBlock = 128;          // streaming block, frames

    std::vector<float> sineInput(std::size_t frames, std::size_t channels) {
        std::vector<float> out(frames * channels);
        const double       w = 2.0 * std::numbers::pi * 997.0 / 48000.0;
        for (std::size_t i = 0; i < frames; ++i)
            for (std::size_t c = 0; c < channels; ++c)
                out[i * channels + c] = static_cast<float>(0.5 * std::sin(w * static_cast<double>(i)));
        return out;
    }

    /// Cycling cursor over a pregenerated interleaved buffer, so input delivery
    /// costs the same (a bounded copy) for every engine.
    class InputTap {
      public:
        InputTap(std::size_t frames, std::size_t channels)
            : buf_(sineInput(frames, channels))
            , frames_(frames)
            , ch_(channels) {}

        std::size_t pop(float* dst, std::size_t maxFrames) {
            const std::size_t n = std::min(maxFrames, frames_ - pos_);
            std::copy_n(buf_.data() + pos_ * ch_, n * ch_, dst);
            pos_ += n;
            if (pos_ == frames_)
                pos_ = 0;
            return n;
        }

        /// Borrow a contiguous run (for engines that consume in place).
        const float* run(std::size_t frames) {
            if (pos_ + frames > frames_)
                pos_ = 0;
            const float* p = buf_.data() + pos_ * ch_;
            pos_ += frames;
            return p;
        }

      private:
        std::vector<float> buf_;
        std::size_t        frames_;
        std::size_t        ch_;
        std::size_t        pos_ = 0;
    };

    template <typename S>
    void srtBench(benchmark::State& state, const tap::samplerate::filter_spec& spec, std::size_t channels) {
        const tap::samplerate::polyphase_filter_bank<S> bank(spec, 48000.0);
        tap::samplerate::fractional_resampler<S>        rs(bank, channels);
        InputTap                                        inFloat(48000, channels);
        // Requantize the shared float source once at setup for fixed-point runs.
        std::vector<S> buf(48000 * channels);
        {
            std::vector<float> tmp(48000 * channels);
            inFloat.pop(tmp.data(), 48000);
            for (std::size_t i = 0; i < tmp.size(); ++i) {
                if constexpr (std::is_floating_point_v<S>)
                    buf[i] = tmp[i];
                else
                    buf[i] = tap::samplerate::detail::round_sat<S>(
                        static_cast<double>(tmp[i]) * static_cast<double>(std::numeric_limits<S>::max()));
            }
        }
        std::size_t pos = 0;
        const auto  pop = [&](S* dst, std::size_t n) {
            const std::size_t avail = 48000 - pos;
            const std::size_t take  = n < avail ? n : avail;
            std::copy_n(buf.data() + pos * channels, take * channels, dst);
            pos = (pos + take) % 48000;
            return take;
        };
        // The datapath advances (1 + eps) input frames per output frame, so an
        // output/input ratio R means eps = 1/R - 1.
        const double   eps = 1.0 / kRatio - 1.0;
        std::vector<S> out(kBlock * channels);
        rs.prime(pop);

        for (auto _ : state) {
            const std::size_t got = rs.process(out.data(), kBlock, eps, pop);
            benchmark::DoNotOptimize(out.data());
            if (got != kBlock)
                state.SkipWithError("source ran dry");
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBlock));
    }

    void lsrBench(benchmark::State& state, int converter, std::size_t channels) {
        int        err = 0;
        SRC_STATE* src = src_new(converter, static_cast<int>(channels), &err);
        if (src == nullptr) {
            state.SkipWithError(src_strerror(err));
            return;
        }
        InputTap           in(48000, channels);
        std::vector<float> inBlock(kBlock * channels);
        std::vector<float> out(2 * kBlock * channels);

        std::int64_t frames = 0;
        for (auto _ : state) {
            in.pop(inBlock.data(), kBlock);
            SRC_DATA d{};
            d.data_in       = inBlock.data();
            d.input_frames  = static_cast<long>(kBlock);
            d.data_out      = out.data();
            d.output_frames = static_cast<long>(2 * kBlock);
            d.src_ratio     = kRatio;
            if (src_process(src, &d) != 0 || d.input_frames_used != static_cast<long>(kBlock)) {
                state.SkipWithError("src_process failed");
                break;
            }
            benchmark::DoNotOptimize(out.data());
            frames += d.output_frames_gen;
        }
        src_delete(src);
        state.SetItemsProcessed(frames);
    }

    void soxrBench(benchmark::State& state, unsigned long recipe, std::size_t channels) {
        soxr_error_t              err = nullptr;
        const soxr_io_spec_t      io  = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
        const soxr_quality_spec_t q   = soxr_quality_spec(recipe, 0);
        soxr_t soxr = soxr_create(48000.0, 48000.0 * kRatio, static_cast<unsigned>(channels), &err, &io, &q, nullptr);
        if (err != nullptr) {
            state.SkipWithError(soxr_strerror(err));
            return;
        }
        InputTap           in(48000, channels);
        std::vector<float> out(2 * kBlock * channels);

        std::int64_t frames = 0;
        for (auto _ : state) {
            const float* inPtr = in.run(kBlock);
            std::size_t  idone = 0;
            std::size_t  odone = 0;
            if (soxr_process(soxr, inPtr, kBlock, &idone, out.data(), 2 * kBlock, &odone) != nullptr
                || idone != kBlock) {
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

    void r8bBench(benchmark::State& state, double reqAtten, double transBandPct, std::size_t channels) {
        std::vector<std::unique_ptr<r8b::CDSPResampler>> rs;
        for (std::size_t c = 0; c < channels; ++c)
            rs.push_back(std::make_unique<r8b::CDSPResampler>(48000.0, 48000.0 * kRatio, static_cast<int>(kBlock),
                                                              transBandPct, reqAtten, r8b::fprLinearPhase));
        InputTap            in(48000, channels);
        std::vector<float>  inBlock(kBlock * channels);
        std::vector<double> chIn(kBlock);
        std::vector<float>  out(4 * kBlock * channels);

        std::int64_t frames = 0;
        for (auto _ : state) {
            in.pop(inBlock.data(), kBlock);
            int got = 0;
            for (std::size_t c = 0; c < channels; ++c) {
                for (std::size_t i = 0; i < kBlock; ++i)
                    chIn[i] = inBlock[i * channels + c];
                double* op = nullptr;
                got        = rs[c]->process(chIn.data(), static_cast<int>(kBlock), op);
                for (int i = 0; i < got; ++i)
                    out[static_cast<std::size_t>(i) * channels + c] = static_cast<float>(op[i]);
            }
            benchmark::DoNotOptimize(out.data());
            frames += got;
        }
        // Input frames consumed before the first output frame appears: r8brain
        // hides its filter delay by withholding output, so this is its latency.
        state.counters["latency_frames"] = rs[0]->getInLenBeforeOutPos(0);
        state.SetItemsProcessed(frames);
    }

    // --- ~120 dB tier: mono / stereo / 8ch -------------------------------------
    void BM_SRT_Balanced_1ch(benchmark::State& s) {
        srtBench<float>(s, tap::samplerate::filter_spec::balanced(), 1);
    }
    void BM_SRT_Balanced_2ch(benchmark::State& s) {
        srtBench<float>(s, tap::samplerate::filter_spec::balanced(), 2);
    }
    void BM_SRT_Balanced_8ch(benchmark::State& s) {
        srtBench<float>(s, tap::samplerate::filter_spec::balanced(), 8);
    }
    void BM_LSR_Medium_1ch(benchmark::State& s) {
        lsrBench(s, SRC_SINC_MEDIUM_QUALITY, 1);
    }
    void BM_LSR_Medium_2ch(benchmark::State& s) {
        lsrBench(s, SRC_SINC_MEDIUM_QUALITY, 2);
    }
    void BM_LSR_Medium_8ch(benchmark::State& s) {
        lsrBench(s, SRC_SINC_MEDIUM_QUALITY, 8);
    }
    void BM_SOXR_HQ_1ch(benchmark::State& s) {
        soxrBench(s, SOXR_HQ, 1);
    }
    void BM_SOXR_HQ_2ch(benchmark::State& s) {
        soxrBench(s, SOXR_HQ, 2);
    }
    void BM_SOXR_HQ_8ch(benchmark::State& s) {
        soxrBench(s, SOXR_HQ, 8);
    }
    BENCHMARK(BM_SRT_Balanced_1ch);
    BENCHMARK(BM_SRT_Balanced_2ch);
    BENCHMARK(BM_SRT_Balanced_8ch);
    BENCHMARK(BM_LSR_Medium_1ch);
    BENCHMARK(BM_LSR_Medium_2ch);
    BENCHMARK(BM_LSR_Medium_8ch);
    BENCHMARK(BM_SOXR_HQ_1ch);
    BENCHMARK(BM_SOXR_HQ_2ch);
    BENCHMARK(BM_SOXR_HQ_8ch);
    void BM_R8B_120dB_1ch(benchmark::State& s) {
        r8bBench(s, 120.0, 2.0, 1);
    }
    void BM_R8B_120dB_2ch(benchmark::State& s) {
        r8bBench(s, 120.0, 2.0, 2);
    }
    void BM_R8B_120dB_8ch(benchmark::State& s) {
        r8bBench(s, 120.0, 2.0, 8);
    }
    BENCHMARK(BM_R8B_120dB_1ch);
    BENCHMARK(BM_R8B_120dB_2ch);
    BENCHMARK(BM_R8B_120dB_8ch);
    // r8brain's default 2% transition band keeps its passband flat far past
    // 20 kHz and pays for it in delay. The passband-matched row takes the
    // lowest-latency linear-phase setting still flat to 20 kHz like srt
    // balanced, from the sweep in notebooks/asrc_comparison.ipynb: 8% (200
    // input frames; latency is not monotonic in the knob — 10% costs 212 —
    // and 12% already droops 0.11 dB at 20 kHz).
    void BM_R8B_120dB_TB8_2ch(benchmark::State& s) {
        r8bBench(s, 120.0, 8.0, 2);
    }
    BENCHMARK(BM_R8B_120dB_TB8_2ch);

    // --- ~140 dB tier, stereo ---------------------------------------------------
    void BM_SRT_Transparent_2ch(benchmark::State& s) {
        srtBench<float>(s, tap::samplerate::filter_spec::transparent(), 2);
    }
    void BM_LSR_Best_2ch(benchmark::State& s) {
        lsrBench(s, SRC_SINC_BEST_QUALITY, 2);
    }
    void BM_SOXR_VHQ_2ch(benchmark::State& s) {
        soxrBench(s, SOXR_VHQ, 2);
    }
    BENCHMARK(BM_SRT_Transparent_2ch);
    BENCHMARK(BM_LSR_Best_2ch);
    BENCHMARK(BM_SOXR_VHQ_2ch);
    void BM_R8B_16bit_2ch(benchmark::State& s) {
        r8bBench(s, 136.45, 2.0, 2); // CDSPResampler16's preset attenuation
    }
    void BM_R8B_24bit_2ch(benchmark::State& s) {
        r8bBench(s, 180.15, 2.0, 2); // CDSPResampler24's preset attenuation
    }
    BENCHMARK(BM_R8B_16bit_2ch);
    BENCHMARK(BM_R8B_24bit_2ch);

    // --- Fixed-point (no competitor analog; libsamplerate, soxr and r8brain
    // are floating-point engines — this is the row embedded targets actually run) ------
    void BM_SRT_Q15_Balanced_2ch(benchmark::State& s) {
        srtBench<std::int16_t>(s, tap::samplerate::filter_spec::balanced(), 2);
    }
    BENCHMARK(BM_SRT_Q15_Balanced_2ch);

} // namespace
