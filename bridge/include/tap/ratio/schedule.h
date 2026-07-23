/// @file schedule.h
/// @brief The compile-time (phase, input_advance) schedule of one direction.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "tap/ratio/design.h"

namespace tap::ratio {

    // ANCHOR: rt_schedule
    /// One output frame's step: which polyphase branch to dot, and how many
    /// input frames to consume before the next output. The whole schedule has
    /// period L and is known at compile time — no modulo, no division, and no
    /// drift ever, which is the entire advantage over the async engine's
    /// moving ratio (and, later, the license for straight-line superblock
    /// codegen).
    struct schedule_entry {
        std::uint16_t phase;   ///< polyphase branch index in [0, L)
        std::uint8_t  advance; ///< input frames consumed after this output
    };

    /// The direction's full superblock: entry n (n in [0, L)) serves output
    /// frame k*L + n. phase(n) = (n * M) mod L visits every branch exactly
    /// once per superblock; advance(n) = floor((n+1)M/L) - floor(nM/L), so
    /// advances are {0,1} going up (L > M), {1,2} going down (M > L), and sum
    /// to exactly M over the superblock: L outputs always consume M inputs.
    template <direction D>
    constexpr std::array<schedule_entry, ratio_traits<D>::k_phases> make_schedule() noexcept {
        constexpr std::size_t         l = ratio_traits<D>::k_phases;
        constexpr std::size_t         m = ratio_traits<D>::k_decimation;
        std::array<schedule_entry, l> s{};
        for (std::size_t n = 0; n < l; ++n) {
            s[n].phase   = static_cast<std::uint16_t>((n * m) % l);
            s[n].advance = static_cast<std::uint8_t>(((n + 1) * m) / l - (n * m) / l);
        }
        return s;
    }

    template <direction D>
    inline constexpr std::array<schedule_entry, ratio_traits<D>::k_phases> k_schedule = make_schedule<D>();

    /// Exact input need for the next `out_frames` outputs when the engine
    /// stands at superblock position `pos` (in [0, L)): a capability the
    /// async engine can never offer, and what makes pull-style composition
    /// (produce exactly N, draw input as needed) deterministic. Pure
    /// arithmetic — floor((pos + out)M/L) - floor(pos*M/L) — so it never
    /// walks the table.
    template <direction D>
    constexpr std::uint64_t frames_needed(std::size_t pos, std::uint64_t out_frames) noexcept {
        constexpr std::uint64_t l = ratio_traits<D>::k_phases;
        constexpr std::uint64_t m = ratio_traits<D>::k_decimation;
        const std::uint64_t     p = pos % l;
        return (p + out_frames) * m / l - p * m / l;
    }
    // ANCHOR_END: rt_schedule

} // namespace tap::ratio
