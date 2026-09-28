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

#include "srt/srt.h"

namespace {

    constexpr double      k_fs           = 48000.0;
    constexpr double      k_consumer_ppm = 500.0; // consumer clock runs 500 ppm fast
    constexpr std::size_t k_chunk        = 96;
    constexpr double      k_run_seconds  = 20.0;

    const char* state_name(tap::samplerate::converter_state s) {
        switch (s) {
        case tap::samplerate::converter_state::filling:
            return "Filling";
        case tap::samplerate::converter_state::acquiring:
            return "Acquiring";
        case tap::samplerate::converter_state::locked:
            return "Locked";
        }
        return "?";
    }

} // namespace

int main() {
    tap::samplerate::config cfg;
    cfg.channels                      = 1;
    cfg.target_latency_frames         = 960; // 20 ms: room for OS scheduling jitter
    cfg.servo.lock_threshold_frames   = 4.0;
    cfg.servo.unlock_threshold_frames = 96.0;
    tap::samplerate::async_sample_rate_converter asrc(cfg);

    std::printf("drifting_clocks: producer 48000.0 Hz, consumer %+.0f ppm, %g s\n", k_consumer_ppm, k_run_seconds);
    std::printf("designed latency: %.2f ms\n", asrc.designed_latency_seconds() * 1e3);

    std::atomic<bool> stop{false};
    using clock   = std::chrono::steady_clock;
    const auto t0 = clock::now();

    std::thread producer([&] {
        std::vector<float> buf(k_chunk);
        const double       nu     = 997.0 / k_fs;
        std::uint64_t      idx    = 0;
        auto               next   = t0;
        const auto         period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(static_cast<double>(k_chunk) / k_fs));
        while (!stop.load(std::memory_order_relaxed)) {
            for (auto& v : buf) {
                v = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(idx++)));
            }
            asrc.push(buf.data(), k_chunk);
            next += period;
            std::this_thread::sleep_until(next);
        }
    });

    std::thread consumer([&] {
        std::vector<float> buf(k_chunk);
        const double       fs_out = k_fs * (1.0 + k_consumer_ppm * 1e-6);
        auto               next   = t0;
        const auto         period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(static_cast<double>(k_chunk) / fs_out));
        while (!stop.load(std::memory_order_relaxed)) {
            asrc.pull(buf.data(), k_chunk);
            next += period;
            std::this_thread::sleep_until(next);
        }
    });

    double       ppm_avg   = 0.0;
    const double avg_alpha = 0.5 / 3.0; // 0.5 s prints, ~3 s averaging
    for (double t = 0.5; t <= k_run_seconds; t += 0.5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto st = asrc.status();
        ppm_avg += avg_alpha * (st.ppm - ppm_avg);
        std::printf("t=%5.1fs  state=%-9s  ppm=%+8.2f (avg %+8.2f)  fill=%7.1f  "
                    "under=%llu over=%llu resync=%llu\n",
                    t, state_name(st.state), st.ppm, ppm_avg, st.fifo_fill_frames,
                    static_cast<unsigned long long>(st.underruns), static_cast<unsigned long long>(st.overruns),
                    static_cast<unsigned long long>(st.resyncs));
    }

    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();

    const auto st = asrc.status();
    // The producer pushes at exactly 48000 Hz and the consumer pulls 500 ppm
    // fast, so the converter must consume input slower than unity: -500 ppm.
    std::printf("\nfinal: state=%s ppm(avg)=%+.2f (expected ~%+.0f)\n", state_name(st.state), ppm_avg, -k_consumer_ppm);
    const bool ok = st.state == tap::samplerate::converter_state::locked && std::abs(ppm_avg + k_consumer_ppm) < 150.0;
    std::printf("%s\n", ok ? "OK" : "NOT CONVERGED (heavily loaded machine?)");
    return ok ? 0 : 1;
}
