#include <cstdint>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "srt/spsc_ring.hpp"

namespace {

TEST(SpscRing, CapacityRoundsUpToPowerOfTwo) {
    srt::SpscRing<float> r(100);
    EXPECT_EQ(r.capacity(), 128u);
}

TEST(SpscRing, FillDrainExactness) {
    srt::SpscRing<int> r(8);
    std::vector<int> src(8);
    std::iota(src.begin(), src.end(), 0);
    EXPECT_EQ(r.write(src.data(), 8), 8u);
    EXPECT_EQ(r.write(src.data(), 1), 0u); // full
    EXPECT_EQ(r.readAvailable(), 8u);
    std::vector<int> dst(8, -1);
    EXPECT_EQ(r.read(dst.data(), 8), 8u);
    EXPECT_EQ(dst, src);
    EXPECT_EQ(r.read(dst.data(), 1), 0u); // empty
}

TEST(SpscRing, WrapAroundPreservesData) {
    srt::SpscRing<std::uint32_t> r(16);
    std::uint32_t seq = 0;
    std::uint32_t expect = 0;
    // Repeatedly write 5, read 5 so the indices wrap many times.
    for (int round = 0; round < 100; ++round) {
        std::uint32_t buf[5];
        for (auto& v : buf)
            v = seq++;
        ASSERT_EQ(r.write(buf, 5), 5u);
        std::uint32_t out[5];
        ASSERT_EQ(r.read(out, 5), 5u);
        for (auto v : out)
            ASSERT_EQ(v, expect++);
    }
}

TEST(SpscRing, DiscardAdvancesConsumer) {
    srt::SpscRing<int> r(16);
    int buf[10];
    std::iota(buf, buf + 10, 0);
    ASSERT_EQ(r.write(buf, 10), 10u);
    EXPECT_EQ(r.discard(4), 4u);
    int out[6];
    ASSERT_EQ(r.read(out, 6), 6u);
    EXPECT_EQ(out[0], 4);
    EXPECT_EQ(out[5], 9);
    EXPECT_EQ(r.discard(100), 0u); // nothing left
}

TEST(SpscRing, PartialWriteWhenNearlyFull) {
    srt::SpscRing<int> r(8);
    int buf[6] = {0, 1, 2, 3, 4, 5};
    ASSERT_EQ(r.write(buf, 6), 6u);
    EXPECT_EQ(r.write(buf, 6), 2u); // only 2 slots free
    EXPECT_EQ(r.readAvailable(), 8u);
}

TEST(SpscRing, TwoThreadStressPreservesSequence) {
    constexpr std::uint64_t kTotal = 10'000'000;
    srt::SpscRing<std::uint32_t> ring(1024);

    std::thread producer([&] {
        std::mt19937 rng(12345);
        std::uniform_int_distribution<std::size_t> chunk(1, 64);
        std::vector<std::uint32_t> buf(64);
        std::uint64_t sent = 0;
        while (sent < kTotal) {
            const std::size_t want = std::min<std::uint64_t>(chunk(rng), kTotal - sent);
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

    std::mt19937 rng(54321);
    std::uniform_int_distribution<std::size_t> chunk(1, 64);
    std::vector<std::uint32_t> buf(64);
    std::uint64_t received = 0;
    bool ordered = true;
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
