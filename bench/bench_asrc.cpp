// Host benchmarks for the SampleRateTap hot path. Two layers:
//  - Kernel: srt::interpolate() in isolation (one output sample, one
//    channel) — the datapath's arithmetic floor.
//  - Pipeline: steady-state push()+pull() through the full converter in
//    128-frame blocks, the realistic duplex cost per frame.
// Items processed are output samples (kernel) or frames (pipeline), so
// benchmark's items/s divided by 48000 gives the ×realtime figure.
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include <benchmark/benchmark.h>

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
void kernelBench(benchmark::State& state, const srt::FilterSpec& spec) {
    const srt::PolyphaseFilterBank<S> bank(spec, 48000.0);
    const auto hist = sineBlock<S>(bank.taps(), 997.0, 0.5);
    double mu = 0.0;
    for (auto _ : state) {
        mu += 0.6180339887498949; // golden-ratio stride visits phases evenly
        if (mu >= 1.0)
            mu -= 1.0;
        benchmark::DoNotOptimize(srt::interpolate(bank, hist.data(), mu));
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}

template <typename S>
void pipelineBench(benchmark::State& state, const srt::FilterSpec& spec, std::size_t channels) {
    constexpr std::size_t kBlock = 128;
    srt::Config cfg;
    cfg.channels = channels;
    cfg.filter = spec;
    // The FIFO setpoint must exceed the pull block size (see README latency
    // notes); 2 blocks gives headroom without distorting per-frame cost.
    cfg.targetLatencyFrames = 2 * kBlock;
    srt::BasicAsyncSampleRateConverter<S> asrc(cfg);

    // One second of pregenerated input so signal synthesis stays out of the
    // measured region; consumed cyclically.
    const auto input = sineBlock<S>(48000 * channels, 997.0, 0.5);
    std::vector<S> out(kBlock * channels);

    // Warm up into the Locked steady state before measuring.
    std::size_t off = 0;
    const auto step = [&] {
        asrc.push(input.data() + off, kBlock);
        asrc.pull(out.data(), kBlock);
        off += kBlock * channels;
        if (off + kBlock * channels > input.size())
            off = 0;
    };
    for (int i = 0; i < 1000; ++i)
        step();

    for (auto _ : state) {
        step();
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kBlock);
    if (asrc.status().underruns != 0)
        state.SkipWithError("underrun during steady-state benchmark");
}

// --- Kernel: type x preset ------------------------------------------------
void BM_Kernel_Float_Fast(benchmark::State& s) {
    kernelBench<float>(s, srt::FilterSpec::fast());
}
void BM_Kernel_Float_Balanced(benchmark::State& s) {
    kernelBench<float>(s, srt::FilterSpec::balanced());
}
void BM_Kernel_Float_Transparent(benchmark::State& s) {
    kernelBench<float>(s, srt::FilterSpec::transparent());
}
void BM_Kernel_Q15_Balanced(benchmark::State& s) {
    kernelBench<std::int16_t>(s, srt::FilterSpec::balanced());
}
void BM_Kernel_Q31_Balanced(benchmark::State& s) {
    kernelBench<std::int32_t>(s, srt::FilterSpec::balanced());
}
BENCHMARK(BM_Kernel_Float_Fast);
BENCHMARK(BM_Kernel_Float_Balanced);
BENCHMARK(BM_Kernel_Float_Transparent);
BENCHMARK(BM_Kernel_Q15_Balanced);
BENCHMARK(BM_Kernel_Q31_Balanced);

// --- Pipeline: type x channels (balanced), plus the transparent ceiling ---
void BM_Pipeline_Float_Balanced_1ch(benchmark::State& s) {
    pipelineBench<float>(s, srt::FilterSpec::balanced(), 1);
}
void BM_Pipeline_Float_Balanced_2ch(benchmark::State& s) {
    pipelineBench<float>(s, srt::FilterSpec::balanced(), 2);
}
void BM_Pipeline_Float_Balanced_8ch(benchmark::State& s) {
    pipelineBench<float>(s, srt::FilterSpec::balanced(), 8);
}
void BM_Pipeline_Q15_Balanced_2ch(benchmark::State& s) {
    pipelineBench<std::int16_t>(s, srt::FilterSpec::balanced(), 2);
}
void BM_Pipeline_Q31_Balanced_2ch(benchmark::State& s) {
    pipelineBench<std::int32_t>(s, srt::FilterSpec::balanced(), 2);
}
void BM_Pipeline_Float_Transparent_2ch(benchmark::State& s) {
    pipelineBench<float>(s, srt::FilterSpec::transparent(), 2);
}
BENCHMARK(BM_Pipeline_Float_Balanced_1ch);
BENCHMARK(BM_Pipeline_Float_Balanced_2ch);
BENCHMARK(BM_Pipeline_Float_Balanced_8ch);
BENCHMARK(BM_Pipeline_Q15_Balanced_2ch);
BENCHMARK(BM_Pipeline_Q31_Balanced_2ch);
BENCHMARK(BM_Pipeline_Float_Transparent_2ch);

} // namespace
