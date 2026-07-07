// Single-threaded spsc_ring tests; the two-thread stress lives in
// test_spsc_ring_threads.cpp so thread-less (bare-metal) builds can still
// compile this file.
#include <cstdint>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "srt/spsc_ring.h"

namespace {

    TEST(spsc_ring, CapacityRoundsUpToPowerOfTwo) {
        srt::spsc_ring<float> r(100);
        EXPECT_EQ(r.capacity(), 128u);
    }

    TEST(spsc_ring, FillDrainExactness) {
        srt::spsc_ring<int> r(8);
        std::vector<int>    src(8);
        std::iota(src.begin(), src.end(), 0);
        EXPECT_EQ(r.write(src.data(), 8), 8u);
        EXPECT_EQ(r.write(src.data(), 1), 0u); // full
        EXPECT_EQ(r.read_available(), 8u);
        std::vector<int> dst(8, -1);
        EXPECT_EQ(r.read(dst.data(), 8), 8u);
        EXPECT_EQ(dst, src);
        EXPECT_EQ(r.read(dst.data(), 1), 0u); // empty
    }

    TEST(spsc_ring, WrapAroundPreservesData) {
        srt::spsc_ring<std::uint32_t> r(16);
        std::uint32_t                 seq    = 0;
        std::uint32_t                 expect = 0;
        // Repeatedly write 5, read 5 so the indices wrap many times.
        for (int round = 0; round < 100; ++round) {
            std::uint32_t buf[5];
            for (auto& v : buf) {
                v = seq++;
            }
            ASSERT_EQ(r.write(buf, 5), 5u);
            std::uint32_t out[5];
            ASSERT_EQ(r.read(out, 5), 5u);
            for (auto v : out) {
                ASSERT_EQ(v, expect++);
            }
        }
    }

    TEST(spsc_ring, DiscardAdvancesConsumer) {
        srt::spsc_ring<int> r(16);
        int                 buf[10];
        std::iota(buf, buf + 10, 0);
        ASSERT_EQ(r.write(buf, 10), 10u);
        EXPECT_EQ(r.discard(4), 4u);
        int out[6];
        ASSERT_EQ(r.read(out, 6), 6u);
        EXPECT_EQ(out[0], 4);
        EXPECT_EQ(out[5], 9);
        EXPECT_EQ(r.discard(100), 0u); // nothing left
    }

    TEST(spsc_ring, PartialWriteWhenNearlyFull) {
        srt::spsc_ring<int> r(8);
        int                 buf[6] = {0, 1, 2, 3, 4, 5};
        ASSERT_EQ(r.write(buf, 6), 6u);
        EXPECT_EQ(r.write(buf, 6), 2u); // only 2 slots free
        EXPECT_EQ(r.read_available(), 8u);
    }
} // namespace
