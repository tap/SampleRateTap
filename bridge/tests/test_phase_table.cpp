// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// Contract battery for the phase-major coefficient table, typed over the
// three sample formats and exhaustive over every phase of both directions —
// the beginning of the exhaustive-phase discipline the plan commits to.

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "tap/dsp/fir_kernels.h"
#include "tap/ratio/phase_table.h"

namespace {

    using tap::ratio::basic_phase_table;
    using tap::ratio::direction;
    using tap::ratio::profile;

    template <typename S>
    class phase_table_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(phase_table_test, sample_types, );

    template <typename S, direction D>
    void check_table(const profile& p) {
        using tr = tap::dsp::sample_traits<S>;
        const basic_phase_table<S, D> table(p);
        EXPECT_EQ(table.taps(), p.template taps<D>());
        EXPECT_EQ(table.storage_bytes(), table.k_phases * table.taps() * sizeof(typename tr::coeff));
        EXPECT_NEAR(table.group_delay_input_samples(), static_cast<double>(table.taps()) / 2.0, 0.51);

        // Every phase, exhaustively: fixed-point rows sum to the format's
        // unity exactly (the row-sum guarantee), and a full-scale DC window
        // through the shared kernel lands within one output LSB of full
        // scale for every branch — DC gain is phase-independent.
        std::vector<S> dc(table.taps(), std::is_floating_point_v<S> ? S(1) : std::numeric_limits<S>::max());
        for (std::size_t ph = 0; ph < table.k_phases; ++ph) {
            if constexpr (!std::is_floating_point_v<S>) {
                std::int64_t sum = 0;
                for (std::size_t t = 0; t < table.taps(); ++t) {
                    sum += table.row(ph)[t];
                }
                ASSERT_EQ(sum, static_cast<std::int64_t>(tr::k_coeff_scale)) << "phase " << ph;
            }
            const S y = tap::dsp::dot_row<S>(table.row(ph), dc.data(), table.taps());
            if constexpr (std::is_floating_point_v<S>) {
                ASSERT_NEAR(y, 1.0f, 1e-3f) << "phase " << ph;
            }
            else {
                ASSERT_NEAR(y, std::numeric_limits<S>::max(), 2) << "phase " << ph;
            }
        }
    }

    TYPED_TEST(phase_table_test, DownEconomyEveryPhase) {
        check_table<TypeParam, direction::down_to_44k1>(profile::economy());
    }
    TYPED_TEST(phase_table_test, UpEconomyEveryPhase) {
        check_table<TypeParam, direction::up_to_48k>(profile::economy());
    }
    TYPED_TEST(phase_table_test, DownTransparentEveryPhase) {
        check_table<TypeParam, direction::down_to_44k1>(profile::transparent());
    }
    TYPED_TEST(phase_table_test, UpTransparentEveryPhase) {
        check_table<TypeParam, direction::up_to_48k>(profile::transparent());
    }

    // The storage numbers the plan quotes, pinned: economy is the compact
    // profile the speed-first charter defaults to.
    TEST(PhaseTable, StorageBudgetsArePinned) {
        const basic_phase_table<float, direction::down_to_44k1> de(profile::economy());
        EXPECT_EQ(de.storage_bytes(), 147u * 78u * 4u); // 44.8 KiB
        const basic_phase_table<float, direction::down_to_44k1> dt(profile::transparent());
        EXPECT_EQ(dt.storage_bytes(), 147u * 184u * 4u); // 105.7 KiB
        const basic_phase_table<std::int16_t, direction::up_to_48k> ue(profile::economy());
        EXPECT_EQ(ue.storage_bytes(), 160u * 44u * 2u); // 13.8 KiB — Q15 halves it
    }

} // namespace
