// Computational comparison against general-purpose resamplers at a fixed,
// known near-unity ratio (docs/COMPARISON.md).
//
// Methodology: every engine converts the same float signal at the same
// fixed ratio (48000 -> 48000*(1+200e-6)), streaming in 128-frame blocks.
// SampleRateTap runs its datapath (FractionalResampler) with a constant
// rate deviation — the servo is quiescent at a fixed ratio, and the
// competitors take the ratio as an input rather than estimating it, so
// this is the apples-to-apples configuration. Items processed are output
// frames; items/s divided by 48000 is the ×realtime figure per stream.
//
// Quality pairing (stopband attenuation, vendor-stated):
//   srt balanced (120 dB)     ~ libsamplerate MEDIUM (121 dB) ~ soxr HQ (~120 dB)
//   srt transparent (140 dB)  ~ libsamplerate BEST (144 dB)   ~ soxr VHQ (~170 dB)
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <benchmark/benchmark.h>
#include <samplerate.h>
#include <soxr.h>

#include "srt/polyphase_filter.hpp"
#include "srt/sample_traits.hpp"

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
    void srtBench(benchmark::State& state, const srt::FilterSpec& spec, std::size_t channels) {
        const srt::PolyphaseFilterBank<S> bank(spec, 48000.0);
        srt::FractionalResampler<S>       rs(bank, channels);
        InputTap                          inFloat(48000, channels);
        // Requantize the shared float source once at setup for fixed-point runs.
        std::vector<S> buf(48000 * channels);
        {
            std::vector<float> tmp(48000 * channels);
            inFloat.pop(tmp.data(), 48000);
            for (std::size_t i = 0; i < tmp.size(); ++i) {
                if constexpr (std::is_floating_point_v<S>)
                    buf[i] = tmp[i];
                else
                    buf[i] = srt::detail::roundSat<S>(static_cast<double>(tmp[i])
                                                      * static_cast<double>(std::numeric_limits<S>::max()));
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
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kBlock);
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

    // --- ~120 dB tier: mono / stereo / 8ch -------------------------------------
    void BM_SRT_Balanced_1ch(benchmark::State& s) {
        srtBench<float>(s, srt::FilterSpec::balanced(), 1);
    }
    void BM_SRT_Balanced_2ch(benchmark::State& s) {
        srtBench<float>(s, srt::FilterSpec::balanced(), 2);
    }
    void BM_SRT_Balanced_8ch(benchmark::State& s) {
        srtBench<float>(s, srt::FilterSpec::balanced(), 8);
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

    // --- ~140 dB tier, stereo ---------------------------------------------------
    void BM_SRT_Transparent_2ch(benchmark::State& s) {
        srtBench<float>(s, srt::FilterSpec::transparent(), 2);
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

    // --- Fixed-point (no competitor analog; libsamplerate and soxr are
    // float-only engines — this is the row embedded targets actually run) ------
    void BM_SRT_Q15_Balanced_2ch(benchmark::State& s) {
        srtBench<std::int16_t>(s, srt::FilterSpec::balanced(), 2);
    }
    BENCHMARK(BM_SRT_Q15_Balanced_2ch);

} // namespace
