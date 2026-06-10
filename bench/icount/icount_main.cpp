// Deterministic fixed workloads for the instruction-count ratchet
// (docs/PERFORMANCE.md). One scenario per binary, selected at compile time
// (SRT_SC_KIND / SRT_SC_TYPE) because bare-metal targets have no argv.
// The qemu plugin counts the whole run including setup; workloads are sized
// so the measured loop dominates. The checksum both defeats dead-code
// elimination and pins down cross-run determinism.
//
// SRT_SC_KIND: 0 = kernel (interpolate in isolation), 1 = pipeline (duplex
//              push/pull through the full converter, stereo)
// SRT_SC_TYPE: 0 = float, 1 = Q15, 2 = Q31
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "srt/asrc.hpp"

namespace {

template <typename S>
S makeSample(double v) {
    if constexpr (std::is_floating_point_v<S>)
        return static_cast<S>(v);
    else
        return srt::detail::roundSat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
}

template <typename S>
std::vector<S> sineBlock(std::size_t samples, double freqHz, double amp) {
    std::vector<S> out(samples);
    const double w = 2.0 * std::numbers::pi * freqHz / 48000.0;
    for (std::size_t i = 0; i < samples; ++i)
        out[i] = makeSample<S>(amp * std::sin(w * static_cast<double>(i)));
    return out;
}

template <typename S>
double runKernel() {
    const srt::PolyphaseFilterBank<S> bank(srt::FilterSpec::balanced(), 48000.0);
    const auto hist = sineBlock<S>(bank.taps(), 997.0, 0.5);
    double sink = 0.0;
    double mu = 0.0;
    for (int i = 0; i < 200000; ++i) {
        mu += 0.6180339887498949;
        if (mu >= 1.0)
            mu -= 1.0;
        sink += static_cast<double>(srt::interpolate(bank, hist.data(), mu));
    }
    return sink;
}

template <typename S>
double runPipeline() {
    constexpr std::size_t kCh = 2;
    constexpr std::size_t kBlock = 32;
    srt::Config cfg;
    cfg.channels = kCh;
    srt::BasicAsyncSampleRateConverter<S> asrc(cfg);

    const auto input = sineBlock<S>(12000 * kCh, 997.0, 0.5); // 0.25 s, cycled
    std::vector<S> out(kBlock * kCh);

    double sink = 0.0;
    std::size_t off = 0;
    const std::size_t blocks = 2 * 48000 / kBlock; // 2 s of virtual audio
    for (std::size_t b = 0; b < blocks; ++b) {
        asrc.push(input.data() + off, kBlock);
        asrc.pull(out.data(), kBlock);
        off += kBlock * kCh;
        if (off + kBlock * kCh > input.size())
            off = 0;
        sink += static_cast<double>(out[0]);
    }
    if (asrc.status().underruns != 0)
        return std::numeric_limits<double>::quiet_NaN(); // poisons the checksum
    return sink;
}

template <typename S>
double run() {
#if SRT_SC_KIND == 0
    return runKernel<S>();
#else
    return runPipeline<S>();
#endif
}

} // namespace

int main() {
#if SRT_SC_TYPE == 0
    const double checksum = run<float>();
#elif SRT_SC_TYPE == 1
    const double checksum = run<std::int16_t>();
#else
    const double checksum = run<std::int32_t>();
#endif
    const bool ok = checksum == checksum; // NaN check
    std::printf("SRT_ICOUNT_DONE ok=%d checksum=%.17g\n", ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
