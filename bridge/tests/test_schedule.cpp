// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// Contract battery for the compile-time schedule. Coverage is exhaustive —
// all 160 and all 147 entries, plus every superblock position for
// frames_needed — because the period-L structure makes exhaustive cheap,
// and that is precisely the property this library exists to exploit.

#include <cstdint>
#include <numeric>

#include <gtest/gtest.h>

#include "tap/ratio/schedule.h"

namespace {

    using tap::ratio::direction;
    using tap::ratio::frames_needed;
    using tap::ratio::k_schedule;
    using tap::ratio::ratio_traits;

    // The schedule is a compile-time constant; pin a few facts statically.
    static_assert(k_schedule<direction::up_to_48k>.size() == 160);
    static_assert(k_schedule<direction::down_to_44k1>.size() == 147);
    static_assert(k_schedule<direction::up_to_48k>[0].phase == 0);
    static_assert(k_schedule<direction::down_to_44k1>[0].phase == 0);
    static_assert(frames_needed<direction::up_to_48k>(0, 160) == 147);
    static_assert(frames_needed<direction::down_to_44k1>(0, 147) == 160);

    template <direction D>
    void check_schedule_exhaustively(unsigned lo_advance, unsigned hi_advance) {
        constexpr auto        s = k_schedule<D>;
        constexpr std::size_t l = ratio_traits<D>::k_phases;
        constexpr std::size_t m = ratio_traits<D>::k_decimation;

        std::uint64_t advance_sum = 0;
        std::uint64_t phase_seen  = 0; // bitset via sum of distinct check below
        std::vector<bool> seen(l, false);
        for (std::size_t n = 0; n < l; ++n) {
            // The defining formulas, entry by entry.
            EXPECT_EQ(s[n].phase, (n * m) % l) << "n=" << n;
            EXPECT_EQ(s[n].advance, ((n + 1) * m) / l - (n * m) / l) << "n=" << n;
            // Advance alphabet is exactly the two adjacent integers around M/L.
            EXPECT_GE(s[n].advance, lo_advance) << "n=" << n;
            EXPECT_LE(s[n].advance, hi_advance) << "n=" << n;
            advance_sum += s[n].advance;
            EXPECT_FALSE(seen[s[n].phase]) << "phase revisited within a superblock, n=" << n;
            seen[s[n].phase] = true;
            ++phase_seen;
        }
        // One superblock: L outputs consume exactly M inputs, every phase
        // visited exactly once (gcd(L, M) = 1).
        EXPECT_EQ(advance_sum, m);
        EXPECT_EQ(phase_seen, l);

        // frames_needed agrees with walking the schedule, from EVERY start
        // position, for spans up to two superblocks (covers the wrap).
        for (std::size_t pos = 0; pos < l; ++pos) {
            std::uint64_t walked = 0;
            for (std::uint64_t out = 1; out <= 2 * l; ++out) {
                walked += s[(pos + out - 1) % l].advance;
                ASSERT_EQ(frames_needed<D>(pos, out), walked) << "pos=" << pos << " out=" << out;
            }
        }
    }

    TEST(Schedule, UpExhaustive) {
        // Going up (L > M) some outputs re-use the window: advances are {0, 1}.
        check_schedule_exhaustively<direction::up_to_48k>(0, 1);
    }

    TEST(Schedule, DownExhaustive) {
        // Going down (M > L) some outputs skip a frame: advances are {1, 2}.
        check_schedule_exhaustively<direction::down_to_44k1>(1, 2);
    }

    TEST(Schedule, FramesNeededIsPositionInvariantOverSuperblocks) {
        // Whole superblocks cost exactly M from anywhere.
        for (std::size_t pos = 0; pos < 160; ++pos) {
            EXPECT_EQ(frames_needed<direction::up_to_48k>(pos, 160), 147u);
            EXPECT_EQ(frames_needed<direction::up_to_48k>(pos, 320), 294u);
        }
        for (std::size_t pos = 0; pos < 147; ++pos) {
            EXPECT_EQ(frames_needed<direction::down_to_44k1>(pos, 147), 160u);
        }
    }

} // namespace
