// Deterministic fixed workload for the cross-resampler instruction-count
// comparison (docs/COMPARISON.md). Same shape as icount_main.cpp but the
// engine is selected at compile time and the ratio is fixed and known —
// SampleRateTap runs its bare datapath (fractional_resampler, constant eps)
// and libsamplerate / r8brain-free-src run their own process() with the same
// ratio, so the comparison is engine-vs-engine with no servo on either side.
//
// These binaries are intentionally named cmp_icount_* so the ratchet
// (scripts/icount.py, glob srt_icount_*) never sees them: competitor
// instruction counts are measured once and recorded in docs/COMPARISON.md,
// not gated.
//
// SRT_CMP_ENGINE: 0 = SampleRateTap (balanced), 1 = libsamplerate
//                 SRC_SINC_MEDIUM_QUALITY, 2 = libsamplerate
//                 SRC_SINC_BEST_QUALITY, 3 = r8brain 120 dB at its default
//                 2% transition band, 4 = r8brain 120 dB at 8% (the
//                 lowest-latency setting still flat to 20 kHz, like balanced),
//                 5 = SampleRateTap (balanced) Q15 — no competitor analog
//
// SRT_CMP_SECONDS (default 2) sets the workload length. Every count includes
// one-time construction (filter design, table and FFT setup), so CMake builds
// each engine at 2 s and 4 s: the difference is the steady-state cost of 2 s
// of audio, the remainder is construction (docs/COMPARISON.md reports both).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

#if SRT_CMP_ENGINE == 0 || SRT_CMP_ENGINE == 5
#include <type_traits>

#include "tap/sr/async/polyphase_filter.h"
#include "tap/sr/async/sample_traits.h"
#elif SRT_CMP_ENGINE <= 2
#include <samplerate.h>
#else
#include <memory>

#include <CDSPResampler.h>
#endif

namespace {

    constexpr std::size_t kCh    = 2;
    constexpr std::size_t kBlock = 32;
#ifndef SRT_CMP_SECONDS
#define SRT_CMP_SECONDS 2
#endif
    constexpr std::size_t kBlocks = SRT_CMP_SECONDS * 48000 / kBlock; // input at 48 kHz
    constexpr double      kRatio  = 1.0 + 200e-6;                     // output rate / input rate

    std::vector<float> sineInput(std::size_t frames) {
        std::vector<float> out(frames * kCh);
        const double       w = 2.0 * std::numbers::pi * 997.0 / 48000.0;
        for (std::size_t i = 0; i < frames; ++i)
            for (std::size_t c = 0; c < kCh; ++c)
                out[i * kCh + c] = static_cast<float>(0.5 * std::sin(w * static_cast<double>(i)));
        return out;
    }

#if SRT_CMP_ENGINE == 0 || SRT_CMP_ENGINE == 5

#if SRT_CMP_ENGINE == 0
    using Sample = float;
#else
    using Sample = std::int16_t;
#endif

    // A template so `if constexpr` discards the branch the sample type can't take.
    template <typename S>
    std::vector<S> toSample(const std::vector<float>& in) {
        std::vector<S> out(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            if constexpr (std::is_floating_point_v<S>)
                out[i] = in[i];
            else
                out[i] = tap::samplerate::detail::round_sat<S>(static_cast<double>(in[i])
                                                               * static_cast<double>(std::numeric_limits<S>::max()));
        }
        return out;
    }

    double run() {
        const tap::samplerate::polyphase_filter_bank<Sample> bank(tap::samplerate::filter_spec::balanced(), 48000.0);
        tap::samplerate::fractional_resampler<Sample>        rs(bank, kCh);
        const auto  input = toSample<Sample>(sineInput(12000)); // 0.25 s, cycled
        std::size_t pos   = 0;
        const auto  pop   = [&](Sample* dst, std::size_t n) {
            const std::size_t avail = 12000 - pos;
            const std::size_t take  = n < avail ? n : avail;
            for (std::size_t i = 0; i < take * kCh; ++i)
                dst[i] = input[pos * kCh + i];
            pos = (pos + take) % 12000;
            return take;
        };
        const double        eps = 1.0 / kRatio - 1.0;
        std::vector<Sample> out(kBlock * kCh);
        if (!rs.prime(pop))
            return std::numeric_limits<double>::quiet_NaN();

        double sink = 0.0;
        for (std::size_t b = 0; b < kBlocks; ++b) {
            if (rs.process(out.data(), kBlock, eps, pop) != kBlock)
                return std::numeric_limits<double>::quiet_NaN();
            sink += static_cast<double>(out[0]);
        }
        return sink;
    }

#elif SRT_CMP_ENGINE <= 2

    double run() {
#if SRT_CMP_ENGINE == 1
        constexpr int kConverter = SRC_SINC_MEDIUM_QUALITY;
#else
        constexpr int kConverter = SRC_SINC_BEST_QUALITY;
#endif
        int        err = 0;
        SRC_STATE* src = src_new(kConverter, kCh, &err);
        if (src == nullptr)
            return std::numeric_limits<double>::quiet_NaN();

        const auto         input = sineInput(12000); // 0.25 s, cycled
        std::size_t        pos   = 0;
        std::vector<float> inBlock(kBlock * kCh);
        std::vector<float> out(2 * kBlock * kCh);

        double sink = 0.0;
        for (std::size_t b = 0; b < kBlocks; ++b) {
            for (std::size_t i = 0; i < kBlock * kCh; ++i)
                inBlock[i] = input[pos * kCh + i];
            pos = (pos + kBlock) % 12000;

            SRC_DATA d{};
            d.data_in       = inBlock.data();
            d.input_frames  = static_cast<long>(kBlock);
            d.data_out      = out.data();
            d.output_frames = static_cast<long>(2 * kBlock);
            d.src_ratio     = kRatio;
            if (src_process(src, &d) != 0 || d.input_frames_used != static_cast<long>(kBlock)) {
                src_delete(src);
                return std::numeric_limits<double>::quiet_NaN();
            }
            if (d.output_frames_gen > 0)
                sink += static_cast<double>(out[0]);
        }
        src_delete(src);
        return sink;
    }

#else

    // r8brain is mono per instance with double-precision I/O: one instance per
    // channel, float<->double (de)interleave counted, as any float caller pays.
    double run() {
#if SRT_CMP_ENGINE == 3
        constexpr double kTransBandPct = 2.0;
#else
        constexpr double kTransBandPct = 8.0;
#endif
        std::unique_ptr<r8b::CDSPResampler> rs[kCh];
        for (auto& r : rs)
            r = std::make_unique<r8b::CDSPResampler>(48000.0, 48000.0 * kRatio, static_cast<int>(kBlock), kTransBandPct,
                                                     120.0, r8b::fprLinearPhase);

        const auto          input = sineInput(12000); // 0.25 s, cycled
        std::size_t         pos   = 0;
        std::vector<double> chIn(kBlock);
        std::vector<float>  out(4 * kBlock * kCh);

        double sink = 0.0;
        for (std::size_t b = 0; b < kBlocks; ++b) {
            int got = 0;
            for (std::size_t c = 0; c < kCh; ++c) {
                for (std::size_t i = 0; i < kBlock; ++i)
                    chIn[i] = static_cast<double>(input[(pos + i) * kCh + c]);
                double* op = nullptr;
                got        = rs[c]->process(chIn.data(), static_cast<int>(kBlock), op);
                for (int i = 0; i < got; ++i)
                    out[static_cast<std::size_t>(i) * kCh + c] = static_cast<float>(op[i]);
            }
            pos = (pos + kBlock) % 12000;
            if (got > 0)
                sink += static_cast<double>(out[0]);
        }
        return sink;
    }

#endif

} // namespace

int main() {
    const double checksum = run();
    const bool   ok       = checksum == checksum; // NaN check
    std::printf("SRT_ICOUNT_DONE ok=%d checksum=%.17g\n", ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
