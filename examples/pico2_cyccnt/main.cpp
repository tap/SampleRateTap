// Real-silicon cycle measurement of the ASRC hot path on the RP2350's
// Cortex-M33 (docs/HARDWARE_TESTING.md, Setup 2). The steady-state workload
// is the same duplex push(32)/pull(32) loop as runPipeline() in
// bench/icount/icount_main.cpp, timed per block with DWT.CYCCNT.
//
// Calibration purpose: docs/PERFORMANCE.md gates regressions on QEMU
// *instruction* counts because they are deterministic; real cost is in
// *cycles*, which only hardware counters give. Dividing the mean cycles/frame
// printed here by the committed M33 QEMU baselines (bench/baselines.json,
// 2 s of 48 kHz audio = 96,000 frames per workload):
//
//   pipeline_q15   (2ch, balanced)  484,146,844 insns = 5,043 insns/frame
//   pipeline12_q15 (12ch, balanced) 962,613,655 insns = 10,027 insns/frame
//
// yields the "1 QEMU instruction ~= N RP2350 cycles" ratio that converts
// every M33 instruction baseline into a real cycle budget.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <numbers>
#include <type_traits>
#include <vector>

#include "RP2350.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "srt/asrc.h"

namespace {

    constexpr std::size_t kBlockFrames  = 32;
    constexpr std::size_t kWarmupIters  = 1000; // past Filling/priming + servo settled
    constexpr std::size_t kMeasureIters = 2000;

    // 997 Hz at 0.5 FS, cycled, as in icount_main.cpp — but 4800 frames (0.1 s)
    // instead of 12000 so the 12-channel Q15 input block fits RP2350 SRAM next
    // to the converter. The wrap seam is not periodic in the sine; irrelevant
    // here, the cycle cost per block does not depend on sample values.
    constexpr std::size_t kInputFrames = 4800;

    static_assert(kInputFrames % kBlockFrames == 0);

    std::uint32_t gCycles[kMeasureIters];

    // TRCENA gates the whole DWT block; CYCCNTENA starts the free-running 32-bit
    // cycle counter. CMSIS names from the SDK's core_cm33.h; the firmware runs in
    // the secure state (rp2350-arm-s) so the registers are directly writable.
    // 32-bit wrap is ~28.6 s at 150 MHz — per-block unsigned deltas are safe.
    bool enableCycleCounter() {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        if (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk)
            return false; // implementation without a cycle counter
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        return true;
    }

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
        const double   w = 2.0 * std::numbers::pi * freqHz / 48000.0;
        for (std::size_t i = 0; i < samples; ++i)
            out[i] = makeSample<S>(amp * std::sin(w * static_cast<double>(i)));
        return out;
    }

    template <typename S>
    void runCase(const char* typeName, const char* presetName, const srt::filter_spec& spec, std::size_t channels) {
        srt::Config cfg;
        cfg.channels = channels;
        cfg.filter   = spec;

        // Heap-constructed so allocation failure (e.g. 12ch + float on a tighter
        // build) degrades to a printed SKIP row instead of a hard fault.
        std::unique_ptr<srt::basic_async_sample_rate_converter<S>> asrc;
        std::vector<S>                                             input;
        std::vector<S>                                             out;
        try {
            asrc  = std::make_unique<srt::basic_async_sample_rate_converter<S>>(cfg);
            input = sineBlock<S>(kInputFrames * channels, 997.0, 0.5);
            out.resize(kBlockFrames * channels);
        }
        catch (const std::exception& e) {
            std::printf("%-6s %-9s %3u  SKIP (%s)\n", typeName, presetName, static_cast<unsigned>(channels), e.what());
            return;
        }

        // The sink defeats dead-code elimination, exactly as in the icount
        // workload; its soft-double add is inside the timed region there too.
        double      sink = 0.0;
        std::size_t off  = 0;
        const auto  step = [&]() {
            asrc->push(input.data() + off, kBlockFrames);
            asrc->pull(out.data(), kBlockFrames);
            off += kBlockFrames * channels;
            if (off + kBlockFrames * channels > input.size())
                off = 0;
            sink += static_cast<double>(out[0]);
        };

        for (std::size_t i = 0; i < kWarmupIters; ++i)
            step();
        for (std::size_t i = 0; i < kMeasureIters; ++i) {
            const std::uint32_t t0 = DWT->CYCCNT;
            step();
            gCycles[i] = DWT->CYCCNT - t0;
        }

        std::uint64_t sum = 0;
        for (const std::uint32_t c : gCycles)
            sum += c;
        std::sort(gCycles, gCycles + kMeasureIters);
        const double        mean           = static_cast<double>(sum) / static_cast<double>(kMeasureIters);
        const std::uint32_t p99            = gCycles[kMeasureIters * 99 / 100 - 1];
        const std::uint32_t mx             = gCycles[kMeasureIters - 1];
        const double        cyclesPerFrame = mean / static_cast<double>(kBlockFrames);
        // One 48 kHz stream's share of this core at the configured sys clock.
        const double pctCore = cyclesPerFrame * 48000.0 / static_cast<double>(clock_get_hz(clk_sys)) * 100.0;

        const auto st = asrc->status();
        std::printf("%-6s %-9s %3u  %10.0f %10lu %10lu %10.1f %8.2f%%%s\n", typeName, presetName,
                    static_cast<unsigned>(channels), mean, static_cast<unsigned long>(p99),
                    static_cast<unsigned long>(mx), cyclesPerFrame, pctCore,
                    (st.underruns != 0 || st.overruns != 0 || sink != sink) ? "  WARN: not steady-state" : "");
    }

} // namespace

int main() {
    stdio_init_all();
    // USB CDC drops everything printed before a host terminal attaches.
    while (!stdio_usb_connected())
        sleep_ms(100);
    sleep_ms(250);

    std::printf("SampleRateTap RP2350 DWT.CYCCNT measurement\n");
    std::printf("sys clock: %lu Hz, block: %u frames, warmup: %u, measured: %u iters\n",
                static_cast<unsigned long>(clock_get_hz(clk_sys)), static_cast<unsigned>(kBlockFrames),
                static_cast<unsigned>(kWarmupIters), static_cast<unsigned>(kMeasureIters));

    if (!enableCycleCounter()) {
        std::printf("ERROR: DWT cycle counter not implemented\nSRT_PICO2_DONE\n");
        while (true)
            sleep_ms(1000);
    }

    std::printf("%-6s %-9s %3s  %10s %10s %10s %10s %9s\n", "type", "preset", "ch", "mean/blk", "p99/blk", "max/blk",
                "cyc/frame", "%core@48k");

    for (const std::size_t ch : {std::size_t{1}, std::size_t{2}, std::size_t{12}}) {
        runCase<std::int16_t>("q15", "fast", srt::filter_spec::fast(), ch);
        runCase<std::int16_t>("q15", "balanced", srt::filter_spec::balanced(), ch);
    }
#if PICO2_MEASURE_FLOAT
    // Soft FP64 accumulation: expected brutally slow on the M33 (the QEMU
    // baselines put pipeline_float at ~3.8x pipeline_q15 instructions).
    runCase<float>("float", "fast", srt::filter_spec::fast(), 1);
    runCase<float>("float", "balanced", srt::filter_spec::balanced(), 1);
#endif

    std::printf("SRT_PICO2_DONE\n");
    while (true)
        sleep_ms(1000);
}
