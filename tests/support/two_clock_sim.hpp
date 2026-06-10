// Deterministic single-threaded simulation of two independent clock domains
// driving one AsyncSampleRateConverter. Producer and consumer events are
// interleaved by next-event virtual time, so runs are exactly reproducible.
#ifndef SRT_TESTS_TWO_CLOCK_SIM_HPP
#define SRT_TESTS_TWO_CLOCK_SIM_HPP

#include <cstdint>
#include <functional>
#include <vector>

#include "srt/asrc.hpp"

namespace srt_test {

struct TwoClockSim {
    srt::AsyncSampleRateConverter& asrc;
    double fsIn;  ///< input-domain event rate (true input sample rate)
    double fsOut; ///< output-domain event rate (true output sample rate)
    std::size_t channels = 1;
    std::size_t chunkIn = 32;  ///< frames pushed per producer event
    std::size_t chunkOut = 32; ///< frames pulled per consumer event
    /// Input signal generator: value at input sample index i (all channels).
    std::function<float(std::uint64_t)> gen = [](std::uint64_t) { return 0.0f; };
    /// Optional input-rate modulation: fsIn scale factor at virtual time t
    /// (e.g. for drift-ramp tests). Defaults to constant 1.
    std::function<double(double)> fsInScale = [](double) { return 1.0; };

    /// Runs for `seconds` of output-clock virtual time. onOut receives every
    /// pulled block: (interleavedSamples, frames, virtualTime).
    template <typename OnOutput>
    void run(double seconds, OnOutput&& onOut) {
        std::vector<float> inBuf(chunkIn * channels);
        std::vector<float> outBuf(chunkOut * channels);
        double tIn = 0.0;
        double tOut = 0.0;
        std::uint64_t idx = 0;
        while (tOut < seconds) {
            if (tIn <= tOut) {
                for (std::size_t f = 0; f < chunkIn; ++f) {
                    const float v = gen(idx++);
                    for (std::size_t c = 0; c < channels; ++c)
                        inBuf[f * channels + c] = v;
                }
                asrc.push(inBuf.data(), chunkIn);
                tIn += static_cast<double>(chunkIn) / (fsIn * fsInScale(tIn));
            } else {
                asrc.pull(outBuf.data(), chunkOut);
                onOut(outBuf.data(), chunkOut, tOut);
                tOut += static_cast<double>(chunkOut) / fsOut;
            }
        }
    }
};

} // namespace srt_test

#endif // SRT_TESTS_TWO_CLOCK_SIM_HPP
