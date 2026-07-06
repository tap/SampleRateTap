// Two real threads, two slightly different clocks.
//
// A producer thread pushes a 997 Hz sine at a virtual 48000.0 Hz; a consumer
// thread pulls at 48 kHz + 500 ppm. Both are paced with absolute sleep_until
// deadlines, so the long-term rates are exact even though each wakeup jitters
// by OS-scheduler amounts (typically far rougher than a real audio callback).
// The servo locks onto the -500 ppm consumption deviation and the status line
// shows it converge.
//
// Demo-vs-reality notes:
//  - Sleep jitter here is on the order of milliseconds (vs. nanoseconds for a
//    hardware interface), so this demo uses a deeper FIFO setpoint (20 ms)
//    and a looser lock window than the library defaults.
//  - With block transfer the converter only observes whole chunks, so the
//    rate estimate needs several chunk-beat periods (1/(ppm * chunkRate)) to
//    resolve; at 500 ppm and 96-frame chunks that is ~4 s per cycle. The
//    instantaneous estimate also wobbles at that beat, so the display shows a
//    3 s moving average.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <thread>
#include <vector>

#include "srt/srt.hpp"

namespace {

    constexpr double      kFs          = 48000.0;
    constexpr double      kConsumerPpm = 500.0; // consumer clock runs 500 ppm fast
    constexpr std::size_t kChunk       = 96;
    constexpr double      kRunSeconds  = 20.0;

    const char* stateName(srt::State s) {
        switch (s) {
        case srt::State::Filling:
            return "Filling";
        case srt::State::Acquiring:
            return "Acquiring";
        case srt::State::Locked:
            return "Locked";
        }
        return "?";
    }

} // namespace

int main() {
    srt::Config cfg;
    cfg.channels                    = 1;
    cfg.targetLatencyFrames         = 960; // 20 ms: room for OS scheduling jitter
    cfg.servo.lockThresholdFrames   = 4.0;
    cfg.servo.unlockThresholdFrames = 96.0;
    srt::AsyncSampleRateConverter asrc(cfg);

    std::printf("drifting_clocks: producer 48000.0 Hz, consumer %+.0f ppm, %g s\n", kConsumerPpm, kRunSeconds);
    std::printf("designed latency: %.2f ms\n", asrc.designedLatencySeconds() * 1e3);

    std::atomic<bool> stop{false};
    using clock   = std::chrono::steady_clock;
    const auto t0 = clock::now();

    std::thread producer([&] {
        std::vector<float> buf(kChunk);
        const double       nu     = 997.0 / kFs;
        std::uint64_t      idx    = 0;
        auto               next   = t0;
        const auto         period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(static_cast<double>(kChunk) / kFs));
        while (!stop.load(std::memory_order_relaxed)) {
            for (auto& v : buf)
                v = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(idx++)));
            asrc.push(buf.data(), kChunk);
            next += period;
            std::this_thread::sleep_until(next);
        }
    });

    std::thread consumer([&] {
        std::vector<float> buf(kChunk);
        const double       fsOut  = kFs * (1.0 + kConsumerPpm * 1e-6);
        auto               next   = t0;
        const auto         period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(static_cast<double>(kChunk) / fsOut));
        while (!stop.load(std::memory_order_relaxed)) {
            asrc.pull(buf.data(), kChunk);
            next += period;
            std::this_thread::sleep_until(next);
        }
    });

    double       ppmAvg   = 0.0;
    const double avgAlpha = 0.5 / 3.0; // 0.5 s prints, ~3 s averaging
    for (double t = 0.5; t <= kRunSeconds; t += 0.5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto st = asrc.status();
        ppmAvg += avgAlpha * (st.ppm - ppmAvg);
        std::printf("t=%5.1fs  state=%-9s  ppm=%+8.2f (avg %+8.2f)  fill=%7.1f  "
                    "under=%llu over=%llu resync=%llu\n",
                    t, stateName(st.state), st.ppm, ppmAvg, st.fifoFillFrames,
                    static_cast<unsigned long long>(st.underruns), static_cast<unsigned long long>(st.overruns),
                    static_cast<unsigned long long>(st.resyncs));
    }

    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();

    const auto st = asrc.status();
    // The producer pushes at exactly 48000 Hz and the consumer pulls 500 ppm
    // fast, so the converter must consume input slower than unity: -500 ppm.
    std::printf("\nfinal: state=%s ppm(avg)=%+.2f (expected ~%+.0f)\n", stateName(st.state), ppmAvg, -kConsumerPpm);
    const bool ok = st.state == srt::State::Locked && std::abs(ppmAvg + kConsumerPpm) < 150.0;
    std::printf("%s\n", ok ? "OK" : "NOT CONVERGED (heavily loaded machine?)");
    return ok ? 0 : 1;
}
