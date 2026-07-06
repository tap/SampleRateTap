// Two-thread SpscRing stress test. Compiled only when the platform has
// std::thread (excluded from bare-metal builds by tests/CMakeLists.txt).
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "srt/spsc_ring.hpp"

namespace {

    TEST(SpscRing, TwoThreadStressPreservesSequence) {
        constexpr std::uint64_t      kTotal = 10'000'000;
        srt::SpscRing<std::uint32_t> ring(1024);

        std::thread producer([&] {
            std::mt19937                               rng(12345);
            std::uniform_int_distribution<std::size_t> chunk(1, 64);
            std::vector<std::uint32_t>                 buf(64);
            std::uint64_t                              sent = 0;
            while (sent < kTotal) {
                const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(chunk(rng), kTotal - sent));
                for (std::size_t i = 0; i < want; ++i)
                    buf[i] = static_cast<std::uint32_t>(sent + i);
                std::size_t done = 0;
                while (done < want) {
                    done += ring.write(buf.data() + done, want - done);
                    if (done < want)
                        std::this_thread::yield();
                }
                sent += want;
            }
        });

        std::mt19937                               rng(54321);
        std::uniform_int_distribution<std::size_t> chunk(1, 64);
        std::vector<std::uint32_t>                 buf(64);
        std::uint64_t                              received = 0;
        bool                                       ordered  = true;
        while (received < kTotal) {
            const std::size_t got = ring.read(buf.data(), chunk(rng));
            for (std::size_t i = 0; i < got; ++i)
                ordered = ordered && (buf[i] == static_cast<std::uint32_t>(received + i));
            received += got;
            if (got == 0)
                std::this_thread::yield();
        }
        producer.join();
        EXPECT_TRUE(ordered);
        EXPECT_EQ(received, kTotal);
        EXPECT_EQ(ring.readAvailable(), 0u);
    }

} // namespace
