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
        // Symmetry-halved storage (M7 lever 3): only ceil(L/2) rows held.
        EXPECT_EQ(table.storage_bytes(), table.k_stored * table.taps() * sizeof(typename tr::coeff));
        EXPECT_NEAR(table.group_delay_input_samples(), static_cast<double>(table.taps()) / 2.0, 0.51);

        // Every phase, exhaustively — through the same stored_row/is_mirrored
        // pairing the converter's hot path uses, so the reversed-kernel leg
        // is exercised for every mirrored branch: fixed-point rows sum to
        // the format's unity exactly (the row-sum guarantee; reversal
        // preserves the multiset), and a full-scale DC window lands within
        // one output LSB of full scale — DC gain is phase-independent.
        std::vector<S> dc(table.taps(), std::is_floating_point_v<S> ? S(1) : std::numeric_limits<S>::max());
        for (std::size_t ph = 0; ph < table.k_phases; ++ph) {
            if constexpr (!std::is_floating_point_v<S>) {
                std::int64_t sum = 0;
                for (std::size_t t = 0; t < table.taps(); ++t) {
                    sum += table.at(ph, t);
                }
                ASSERT_EQ(sum, static_cast<std::int64_t>(tr::k_coeff_scale)) << "phase " << ph;
            }
            const S y = table.is_mirrored(ph)
                            ? tap::dsp::dot_row_reversed<S>(table.stored_row(ph), dc.data(), table.taps())
                            : tap::dsp::dot_row<S>(table.stored_row(ph), dc.data(), table.taps());
            if constexpr (std::is_floating_point_v<S>) {
                ASSERT_NEAR(y, 1.0f, 1e-3f) << "phase " << ph;
            }
            else {
                ASSERT_NEAR(y, std::numeric_limits<S>::max(), 2) << "phase " << ph;
            }
        }

        // The mirror identity itself, exhaustively: phase ph IS phase
        // L-1-ph tap-reversed, coefficient for coefficient.
        for (std::size_t ph = 0; ph < table.k_phases; ++ph) {
            for (std::size_t t = 0; t < table.taps(); ++t) {
                ASSERT_EQ(table.at(ph, t), table.at(table.k_phases - 1 - ph, table.taps() - 1 - t))
                    << "phase " << ph << " tap " << t;
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
    // profile the speed-first charter defaults to, and the symmetry halving
    // stores ceil(L/2) rows — 74 of 147 down, 80 of 160 up (the odd L keeps
    // its self-symmetric middle branch as a stored row).
    TEST(PhaseTable, StorageBudgetsArePinned) {
        const basic_phase_table<float, direction::down_to_44k1> de(profile::economy());
        EXPECT_EQ(de.storage_bytes(), 74u * 78u * 4u); // 22.5 KiB (was 44.8 full)
        const basic_phase_table<float, direction::down_to_44k1> dt(profile::transparent());
        EXPECT_EQ(dt.storage_bytes(), 74u * 184u * 4u); // 53.2 KiB (was 105.7 full)
        const basic_phase_table<std::int16_t, direction::up_to_48k> ue(profile::economy());
        EXPECT_EQ(ue.storage_bytes(), 80u * 44u * 2u); // 6.9 KiB — Q15 halves it again
    }

} // namespace
