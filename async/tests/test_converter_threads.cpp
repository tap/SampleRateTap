// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The converter under its real call pattern, on real threads (the roadmap's
// A4): one producer thread calling push() at the input clock, one consumer
// thread calling pull() at the output clock, and a third thread polling
// status() the whole time. The thread contract the README states — one
// producer, one consumer, status() from any thread — is what ThreadSanitizer
// checks here; until this file the only two-thread test was the ring's.
//
// The two clocks are virtual, paced by support/two_clock_sim.h's rule: the
// producer pushes while its virtual time has not passed the consumer's, the
// consumer pulls otherwise, so exactly one side is allowed at a time, the
// pair cannot deadlock, and the sequence of pushes and pulls the servo sees
// is the simulator's (a looser window — one block of lead on each side —
// randomizes the order, and the servo reads that as block-beat noise and
// never holds the 1-frame lock threshold: measured, four of four runs). The
// pacing counters are relaxed atomics on purpose: a release/acquire pair on
// them would order every push before the pull that follows it in
// ThreadSanitizer's happens-before model and hide a missing fence inside
// the ring, whose own acquire/release is the synchronization under test.
// Each turn is instead handed over through a fence pair (a release fence
// before a side bumps its counter, an acquire fence once the other side has
// read it). On weakly-ordered silicon the relaxed counter alone can be seen
// before the ring's own index: the consumer then took its turn a block
// early, the servo read the short fill as block-beat noise and never
// locked, in both arm64 runs since it landed (ci-arm64.yml, issue #84). The
// fences make the hand-off ordered in the C++ model, so the sequence is the
// simulator's on every target, and ThreadSanitizer, which does not model
// standalone fences, still sees no edge but the ring's. The poller is
// unpaced and runs concurrently throughout.
//
// The scenario is the lock test's, with threads: ten virtual seconds at
// +500 ppm (acquire -> track -> quiet: locked), a consumer stall while the
// producer pushes 3000 frames unpaced (the ring overruns and the next pull
// finds occupancy past the high watermark: a hard resync; the burst is not
// virtual time, as the lock test's is not), ten more seconds (relock), a
// reset_from_consumer() from the consumer thread, and ten seconds to relock
// from nothing. Blocks are 16 frames on both sides: the simulator shows the
// servo settled at 16 (locked at 0.5 s, 508 ppm over the lock phase's last
// two seconds, 504 after the resync, 486 ten seconds after the reset) where
// at 32/32 it has not settled by 10 s and at 4/4 or 8/8 it never promotes —
// the documented block-beat wobble, which is why the expectations below are
// two-second means, as the lock test's are, not instantaneous readings. The
// pacing rule makes the threaded sequence the simulator's, so those numbers
// are this test's oracle. No underrun is expected while locked. Compiled
// only where the platform has std::thread (CMakeLists.txt, as the ring
// stress); run under TSan by ci.yml's sanitizer job and, on weakly-ordered
// silicon, by ci-arm64.yml.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "tap/sr/async/converter.h"

namespace {

    using tap::sr::async::converter_state;

    constexpr double      k_fs_out      = 48000.0;
    constexpr double      k_fs_in       = k_fs_out * (1.0 + 500e-6);
    constexpr std::size_t k_channels    = 2;
    constexpr std::size_t k_block       = 16;
    constexpr std::size_t k_burst       = 3000; // frames pushed unpaced in the stall
    constexpr std::size_t k_burst_chunk = 100;

    enum class mode : int { gated, burst, stop };

    // Relaxed throughout (the file comment): each hand-off is a fence pair.
    struct shared_state {
        std::atomic<std::uint64_t> pushed{0}; // frames pushed on the clock (the burst is not)
        std::atomic<std::uint64_t> pulled{0}; // frames pulled by the consumer
        std::atomic<int>           run{static_cast<int>(mode::gated)};
        std::atomic<bool>          burst_done{false};
        std::atomic<bool>          poller_stop{false};
    };

    tap::sr::async::config threads_config() {
        tap::sr::async::config cfg;
        cfg.channels = k_channels;
        return cfg;
    }

    // Producer thread: push blocks of a 997 Hz tone while allowed, burst when
    // told, stop when told.
    void produce(tap::sr::async::converter& asrc, shared_state& st) {
        std::vector<float> block(k_block * k_channels);
        std::vector<float> burst(k_burst_chunk * k_channels, 0.0f);
        std::uint64_t      idx     = 0;
        const double       w       = 2.0 * std::numbers::pi * 997.0 / k_fs_in;
        bool               bursted = false;
        for (;;) {
            const auto m = static_cast<mode>(st.run.load(std::memory_order_relaxed));
            if (m == mode::stop) {
                return;
            }
            if (m == mode::burst) {
                if (!bursted) {
                    for (std::size_t off = 0; off < k_burst; off += k_burst_chunk) {
                        asrc.push(burst.data(), k_burst_chunk);
                    }
                    bursted = true; // not virtual time: the stall is the consumer's, not the clock's
                    std::atomic_thread_fence(std::memory_order_release); // the hand-off (file comment)
                    st.burst_done.store(true, std::memory_order_relaxed);
                }
                std::this_thread::yield();
                continue;
            }
            bursted            = false;
            const double t_in  = static_cast<double>(st.pushed.load(std::memory_order_relaxed)) / k_fs_in;
            const double t_out = static_cast<double>(st.pulled.load(std::memory_order_relaxed)) / k_fs_out;
            if (t_in > t_out) { // the simulator's rule: push while not ahead
                std::this_thread::yield();
                continue;
            }
            std::atomic_thread_fence(std::memory_order_acquire); // the consumer's last pull, seen whole
            for (std::size_t f = 0; f < k_block; ++f) {
                const auto v = static_cast<float>(0.5 * std::sin(w * static_cast<double>(idx++)));
                for (std::size_t c = 0; c < k_channels; ++c) {
                    block[f * k_channels + c] = v;
                }
            }
            asrc.push(block.data(), k_block);
            std::atomic_thread_fence(std::memory_order_release); // this push, before the turn passes
            st.pushed.fetch_add(k_block, std::memory_order_relaxed);
        }
    }

    struct poll_result {
        std::uint64_t snapshots     = 0;
        bool          coherent      = true; // every field in range, every counter monotonic
        bool          saw_locked    = false;
        bool          saw_filling   = false;
        std::uint64_t max_underruns = 0;
        std::uint64_t max_resyncs   = 0;
    };

    // Poller thread: status() from a third thread, as a UI or a watchdog would
    // call it, checking each snapshot against the last.
    void poll(const tap::sr::async::converter& asrc, const shared_state& st, poll_result& out,
              std::size_t configured_target) {
        tap::sr::async::converter_status prev = asrc.status();
        while (!st.poller_stop.load(std::memory_order_relaxed)) {
            const auto s = asrc.status();
            ++out.snapshots;
            const int state = static_cast<int>(s.state);
            out.coherent    = out.coherent && state >= static_cast<int>(converter_state::filling)
                           && state <= static_cast<int>(converter_state::locked) && std::isfinite(s.ppm)
                           && std::isfinite(s.fifo_fill_frames) && s.fifo_fill_frames >= 0.0
                           && s.ratio_estimate == 1.0 + s.ppm * 1e-6 && s.underruns >= prev.underruns
                           && s.overruns >= prev.overruns && s.resyncs >= prev.resyncs
                           && s.effective_target_latency_frames >= configured_target;
            out.saw_locked    = out.saw_locked || s.state == converter_state::locked;
            out.saw_filling   = out.saw_filling || s.state == converter_state::filling;
            out.max_underruns = std::max(out.max_underruns, s.underruns);
            out.max_resyncs   = std::max(out.max_resyncs, s.resyncs);
            prev              = s;
            std::this_thread::yield();
        }
    }

    struct consumed {
        std::uint64_t made     = 0;   // frames synthesized from real input
        double        tail_ppm = 0.0; // mean status().ppm over the phase's last two virtual seconds
    };

    // Consumer side (this thread): pull blocks while allowed, for `seconds` of
    // output-clock virtual time.
    consumed consume(tap::sr::async::converter& asrc, shared_state& st, double seconds, std::vector<float>& out) {
        const std::uint64_t from  = st.pulled.load(std::memory_order_relaxed);
        const std::uint64_t until = from + static_cast<std::uint64_t>(seconds * k_fs_out);
        const std::uint64_t tail  = until - static_cast<std::uint64_t>(2.0 * k_fs_out);
        consumed            r;
        double              ppm_sum = 0.0;
        std::uint64_t       ppm_n   = 0;
        while (st.pulled.load(std::memory_order_relaxed) < until) {
            const double t_out = static_cast<double>(st.pulled.load(std::memory_order_relaxed)) / k_fs_out;
            const double t_in  = static_cast<double>(st.pushed.load(std::memory_order_relaxed)) / k_fs_in;
            if (t_in <= t_out) { // the producer's turn
                std::this_thread::yield();
                continue;
            }
            std::atomic_thread_fence(std::memory_order_acquire); // the producer's last push, seen whole
            const std::size_t got = asrc.pull(out.data(), k_block);
            EXPECT_LE(got, k_block);
            r.made += got;
            std::atomic_thread_fence(std::memory_order_release); // this pull, before the turn passes
            const std::uint64_t pulled = st.pulled.fetch_add(k_block, std::memory_order_relaxed) + k_block;
            if (pulled > tail) {
                ppm_sum += asrc.status().ppm;
                ++ppm_n;
            }
        }
        r.tail_ppm = ppm_n != 0 ? ppm_sum / static_cast<double>(ppm_n) : 0.0;
        return r;
    }

    TEST(Threads, ProducerConsumerAndPollerShareOneConverter) {
        const auto                cfg = threads_config();
        tap::sr::async::converter asrc(cfg);
        shared_state              st;
        poll_result               polled;
        std::vector<float>        out(k_block * k_channels);

        std::thread producer([&] { produce(asrc, st); });
        std::thread poller([&] { poll(asrc, st, polled, cfg.target_latency_frames); });

        // 1. Lock at +500 ppm (the simulator: locked at 0.5 s, 508 ppm over
        //    the last two seconds).
        const consumed c1 = consume(asrc, st, 10.0, out);
        auto           s  = asrc.status();
        EXPECT_EQ(s.state, converter_state::locked) << "ppm " << s.ppm << " fill " << s.fifo_fill_frames;
        EXPECT_NEAR(c1.tail_ppm, 500.0, 50.0);
        EXPECT_EQ(s.underruns, 0u);
        EXPECT_EQ(s.resyncs, 0u);
        EXPECT_GT(c1.made, 9 * static_cast<std::uint64_t>(k_fs_out)) << "the fill is the only silence";

        // 2. The consumer stalls; the producer bursts past the ring.
        st.run.store(static_cast<int>(mode::burst), std::memory_order_relaxed);
        while (!st.burst_done.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }
        std::atomic_thread_fence(std::memory_order_acquire); // the whole burst, seen
        s = asrc.status();
        EXPECT_GT(s.overruns, 0u) << "3000 frames into a ring that holds fewer";
        const std::uint64_t underruns_before = s.underruns;

        // 3. Resume: the first pull finds the high watermark crossed (a hard
        //    resync), then the servo relocks (the simulator: one resync, 504 ppm).
        st.run.store(static_cast<int>(mode::gated), std::memory_order_relaxed);
        const consumed c3 = consume(asrc, st, 10.0, out);
        s                 = asrc.status();
        EXPECT_GE(s.resyncs, 1u);
        EXPECT_EQ(s.state, converter_state::locked) << "ppm " << s.ppm;
        EXPECT_NEAR(c3.tail_ppm, 500.0, 50.0);
        EXPECT_EQ(s.underruns, underruns_before) << "a resync is a splice, not a dropout";

        // 4. A full restart from the consumer thread, then relock from nothing
        //    (the simulator: 486 ppm over seconds 8 to 10 after the reset).
        asrc.reset_from_consumer();
        EXPECT_EQ(asrc.status().state, converter_state::filling);
        const consumed c4 = consume(asrc, st, 10.0, out);
        s                 = asrc.status();
        EXPECT_EQ(s.state, converter_state::locked) << "ppm " << s.ppm;
        EXPECT_NEAR(c4.tail_ppm, 500.0, 50.0);

        st.run.store(static_cast<int>(mode::stop), std::memory_order_relaxed);
        producer.join();
        st.poller_stop.store(true, std::memory_order_relaxed);
        poller.join();

        EXPECT_TRUE(polled.coherent) << "a status() snapshot read a field out of range or a counter going backwards";
        EXPECT_GT(polled.snapshots, 1000u) << "the poller ran alongside, not after";
        EXPECT_TRUE(polled.saw_locked);
        EXPECT_TRUE(polled.saw_filling);
        EXPECT_GE(polled.max_resyncs, 1u);
        EXPECT_EQ(polled.max_underruns, s.underruns);
        // Frame accounting across the threads: every clocked frame was counted
        // once, on the side that offered it.
        EXPECT_EQ(st.pushed.load() % k_block, 0u);
        EXPECT_EQ(st.pulled.load() % k_block, 0u);
    }

} // namespace
