// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Contract battery for the fixed-point profiles (milestone M5). The stage is
// format-generic (stage.h through tap::dsp::sample_traits); what this file
// proves is each format's numeric contract, as numbers, for every ratio of
// the vocabulary at every named profile:
//
//   - every row of every table sums to exact unity in the format (a phase
//     row of an interpolator or mixed ratio to 2^14 / 2^30 — a Q15 mixed
//     ratio going down's to G 2^14, G its power-of-two gain; a Q15
//     decimator's every branch to 2^14, its finalize dividing by M; a Q31
//     decimator's whole filter, quantized as one row, to 2^30), so DC gain is
//     exactly 1 and full-scale DC comes out at exactly full scale from every
//     phase;
//   - the tables are bit-pinned (FNV-1a-64 per ratio, profile and format);
//   - the structural zeros never enter the dot of an interpolator, a
//     decimator or a mixed ratio going up: every stored row is trimmed to its
//     quantized nonzero span and no design zero lies inside it, so the MACs
//     counted are the design's nonzero count in Q31 (and float) and the
//     quantized span in Q15, pinned per stage; a mixed ratio going down
//     multiplies exactly its band's structural zeros that fall inside its
//     L-phase rows, and the count is printed;
//   - Q31 tracks the double golden model within a tenth of float's floor;
//   - Q15 is format-limited, and its limits are stated per stage: the RMS
//     deviation from double on the reference noise, and the stopband the
//     quantized Q1.14 table attains (a decimator's table is its band's
//     interpolator table, each branch at unity with the 1 / M in the
//     finalize, so it attains what the interpolator does — by 8 at economy
//     -72.9 dB; quantized as h / M in one row, as before the lever, it lost
//     about 20 log10 M dB: -62.9 dB at M5's design; a mixed ratio going down
//     holds its rows at a power-of-two gain for the same reason, so every
//     stage attains the 70 dB tier; the 120 dB promise is float's and Q31's);
//   - full-scale drive saturates, never wraps, in both formats.
//
// Numbers measured 2026-10-02 (clang 18, x86-64; the formats are exact
// integer arithmetic, so every host and target computes the same bits), and
// re-measured 2026-11-20 (gcc 13, x86-64) with the designer's beta for
// A + 4 dB (X17, PLAN.md section 6), which moved every table. The 4th-band
// stage's rows (up4, down4) joined 2026-11-21 with X-a3 (decision 6 as
// amended, docs/X4_X8_PLAN.md X3), with its tables at every design divisor
// the coverage matrix runs it at, bit-pinned below.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "reference/reference_vectors.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)

    template <typename S>
    constexpr double full_scale() {
        if constexpr (std::is_floating_point_v<S>) {
            return 1.0;
        }
        else {
            return static_cast<double>(std::numeric_limits<S>::max());
        }
    }

    template <typename S>
    S to_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * full_scale<S>());
        }
    }

    /// One stage over a stream of doubles in full-scale units, through format
    /// S (input quantized to S, output back to full-scale units).
    template <typename S, rational_ratio R>
    std::vector<double> run_stage(const profile& p, const std::vector<double>& x) {
        basic_stage<S, 1, R> c(p);
        std::vector<S>       xs(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            xs[i] = to_sample<S>(x[i]);
        }
        std::vector<S> y(c.outputs_for(xs.size()));
        c.process(xs.data(), xs.size(), y.data());
        std::vector<double> out(y.size());
        for (std::size_t i = 0; i < y.size(); ++i) {
            out[i] = static_cast<double>(y[i]) / full_scale<S>();
        }
        return out;
    }

    std::vector<double> reference_noise() {
        return {rational_ref::k_input.begin(), rational_ref::k_input.end()};
    }

    /// Rows of a stage's table: L phases, or M decimating branches.
    template <rational_ratio R>
    constexpr std::size_t k_rows = R::k_up == 1 ? R::k_down : R::k_up;

    // FNV-1a-64 over the coefficient bytes, rows in order, each row in
    // storage order (little-endian on every CI host and target; a one-LSB
    // move in any tap changes it). The pattern of DspTap's
    // Decimate.FixedPointTablesAreBitPinned.
    template <typename S, rational_ratio R>
    std::uint64_t table_fnv1a64(const basic_stage<S, 1, R>& c) {
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (std::size_t r = 0; r < k_rows<R>; ++r) {
            for (std::size_t t = 0; t < c.row_length(); ++t) {
                const auto    v = c.coefficient(r, t);
                unsigned char bytes[sizeof v];
                std::memcpy(bytes, &v, sizeof bytes);
                for (const unsigned char b : bytes) {
                    h ^= b;
                    h *= 0x100000001b3ULL;
                }
            }
        }
        return h;
    }

    /// The design index of row r's storage tap t (tap-reversed rows): an
    /// L-phase row holds taps p + i L, a decimating branch taps j + s M.
    template <rational_ratio R>
    std::size_t design_index(std::size_t r, std::size_t t, std::size_t row_len) {
        const std::size_t step = R::k_up == 1 ? R::k_down : R::k_up;
        return r + (row_len - 1 - t) * step;
    }

    // ------------------------------------------------------------------
    // The pins, per (ratio, profile): float's MACs per superblock (the
    // design's nonzero count, which Q31's equals), Q15's (the quantized
    // span: the 120 dB designs' outer taps fall below half a Q1.14 LSB and
    // are trimmed with the structural zeros), the FNV-1a-64 of the Q15 and
    // Q31 tables, the worst stopband the quantized Q15 table attains on the
    // 16384-point design grid (dB), and Q15's RMS deviation from double on
    // the reference noise (dBFS). Generated from the measurement the file
    // header describes; a change that moves one is a numeric change to the
    // fixed-point datapath.
    // clang-format off
#define TAP_SR_RATIONAL_FIXED_POINT_ROWS(ROW) \
        ROW(up2, super_economy, 17, 17, 0x14cf6470efe6927dULL, 0xbdd9174a705c3a59ULL, -71.4, -94.6) \
        ROW(up2, economy, 21, 21, 0x3dc156bf7e96af1dULL, 0x9350e8239cdf5039ULL, -70.6, -95.3) \
        ROW(up2, balanced, 25, 25, 0x45055d8fd99e161dULL, 0x087cfb78d59476bdULL, -70.7, -94.2) \
        ROW(up2, transparent, 51, 43, 0x35d1dec0a1c88445ULL, 0x30f73228445b4861ULL, -77.1, -92.0) \
        ROW(down2, super_economy, 17, 17, 0x14cf6470efe6927dULL, 0xdb18923fc41e8605ULL, -71.4, -96.0) \
        ROW(down2, economy, 21, 21, 0x3dc156bf7e96af1dULL, 0x1aa8374d9b3a1639ULL, -70.6, -97.2) \
        ROW(down2, balanced, 25, 25, 0x45055d8fd99e161dULL, 0x369a63548f81b825ULL, -70.7, -95.5) \
        ROW(down2, transparent, 51, 43, 0x35d1dec0a1c88445ULL, 0x4405737eb3784409ULL, -77.1, -94.4) \
        ROW(up3, super_economy, 33, 33, 0x8af024814b664ea1ULL, 0xaa26e7f192c7149dULL, -72.7, -94.5) \
        ROW(up3, economy, 41, 41, 0x51b3728312bde5adULL, 0xd4efcaec483db611ULL, -71.8, -95.4) \
        ROW(up3, balanced, 49, 49, 0x5b6cd18a34d851e5ULL, 0x46294c37c3a6eef1ULL, -71.8, -92.7) \
        ROW(up3, transparent, 101, 87, 0x31f6906f83337a35ULL, 0xc785bb76f8242ebdULL, -74.2, -90.5) \
        ROW(down3, super_economy, 33, 33, 0x8af024814b664ea1ULL, 0x6552145f3c369760ULL, -72.7, -97.4) \
        ROW(down3, economy, 41, 41, 0x51b3728312bde5adULL, 0x4d713712298a7d18ULL, -71.8, -97.7) \
        ROW(down3, balanced, 49, 49, 0x5b6cd18a34d851e5ULL, 0x5ea52a8b4fb88fc6ULL, -71.8, -96.2) \
        ROW(down3, transparent, 101, 87, 0x31f6906f83337a35ULL, 0x938fd7c8b56b8338ULL, -74.2, -95.6) \
        ROW(up4, super_economy, 49, 49, 0x3e680ecc6a91c511ULL, 0xd54f53e0cc56c941ULL, -72.8, -94.9) \
        ROW(up4, economy, 61, 61, 0xee3330ca24c8153dULL, 0x9316baef3b052ae9ULL, -73.9, -93.5) \
        ROW(up4, balanced, 73, 73, 0x692ee42856a2b2d5ULL, 0x352b34c04a6ea715ULL, -70.4, -92.5) \
        ROW(up4, transparent, 151, 131, 0x71a5a915c80aa3b1ULL, 0x0af398a526d71281ULL, -78.1, -90.7) \
        ROW(down4, super_economy, 49, 49, 0x3e680ecc6a91c511ULL, 0x52816d59a984196dULL, -72.8, -98.7) \
        ROW(down4, economy, 61, 61, 0xee3330ca24c8153dULL, 0x1a977d79bdbc5721ULL, -73.9, -97.0) \
        ROW(down4, balanced, 73, 73, 0x692ee42856a2b2d5ULL, 0x297184edb89378fdULL, -70.4, -96.7) \
        ROW(down4, transparent, 151, 131, 0x71a5a915c80aa3b1ULL, 0x72ab9c1f69c1b6f9ULL, -78.1, -96.3) \
        ROW(up6, super_economy, 71, 71, 0x76181d0d2bf49ea1ULL, 0x244bd5922fdd5e3dULL, -71.9, -93.6) \
        ROW(up6, economy, 101, 101, 0x7b8414715922f849ULL, 0xc3bd21df38e7a6e9ULL, -71.8, -93.5) \
        ROW(up6, balanced, 121, 121, 0xacc8d2a918ea7d61ULL, 0x83f620d66492377dULL, -71.0, -92.7) \
        ROW(up6, transparent, 251, 215, 0x7cc51fd5656f9811ULL, 0x2e9f14df933d16c5ULL, -77.8, -90.8) \
        ROW(down6, super_economy, 71, 71, 0x76181d0d2bf49ea1ULL, 0xdd3095cd1f248163ULL, -71.9, -98.6) \
        ROW(down6, economy, 101, 101, 0x7b8414715922f849ULL, 0xc002ee0060a63fc3ULL, -71.8, -98.6) \
        ROW(down6, balanced, 121, 121, 0xacc8d2a918ea7d61ULL, 0xd909bfce2e7ab5adULL, -71.0, -97.7) \
        ROW(down6, transparent, 251, 215, 0x7cc51fd5656f9811ULL, 0x71479c0b7557534dULL, -77.8, -97.5) \
        ROW(up8, super_economy, 99, 99, 0x6015b0be6cf257d5ULL, 0xb2aad1879cae7601ULL, -72.4, -93.8) \
        ROW(up8, economy, 141, 141, 0xf865893036d83da1ULL, 0x219a3cb8da2ee9e5ULL, -72.9, -92.6) \
        ROW(up8, balanced, 169, 169, 0x218616408337704dULL, 0xd0efdc7c70a13e19ULL, -73.6, -91.9) \
        ROW(up8, transparent, 351, 299, 0x5b3db0ec1995eb7dULL, 0x5c62fe64d140c1c5ULL, -77.9, -90.0) \
        ROW(down8, super_economy, 99, 99, 0x6015b0be6cf257d5ULL, 0x482bcbddd641c469ULL, -72.4, -99.4) \
        ROW(down8, economy, 141, 141, 0xf865893036d83da1ULL, 0xcb658ae559e722a1ULL, -72.9, -99.4) \
        ROW(down8, balanced, 169, 169, 0x218616408337704dULL, 0x6ce642d021ad403dULL, -73.6, -98.8) \
        ROW(down8, transparent, 351, 299, 0x5b3db0ec1995eb7dULL, 0xa7f6d6068f46c4d1ULL, -77.9, -100.5) \
        ROW(ratio3over2, super_economy, 33, 33, 0x8af024814b664ea1ULL, 0xaa26e7f192c7149dULL, -72.7, -94.4) \
        ROW(ratio3over2, economy, 41, 41, 0x51b3728312bde5adULL, 0xd4efcaec483db611ULL, -71.8, -95.5) \
        ROW(ratio3over2, balanced, 49, 49, 0x5b6cd18a34d851e5ULL, 0x46294c37c3a6eef1ULL, -71.8, -92.9) \
        ROW(ratio3over2, transparent, 101, 87, 0x31f6906f83337a35ULL, 0xc785bb76f8242ebdULL, -74.2, -90.5) \
        ROW(ratio2over3, super_economy, 47, 47, 0xc95dd6bbc840c69aULL, 0x2ea18619b9b54a6bULL, -73.5, -96.8) \
        ROW(ratio2over3, economy, 59, 59, 0x169e01c4c79fd690ULL, 0x551c9ba359adea14ULL, -73.6, -96.3) \
        ROW(ratio2over3, balanced, 71, 71, 0x0cc8d2eaaf5ea8d8ULL, 0xad5723eeda25161cULL, -71.9, -95.1) \
        ROW(ratio2over3, transparent, 149, 131, 0x15298da333e2194aULL, 0xdb897dfa8d011a9bULL, -79.1, -94.4) \
        ROW(ratio4over3, super_economy, 49, 49, 0x3e680ecc6a91c511ULL, 0xd54f53e0cc56c941ULL, -72.8, -95.2) \
        ROW(ratio4over3, economy, 61, 61, 0xee3330ca24c8153dULL, 0x9316baef3b052ae9ULL, -73.9, -93.3) \
        ROW(ratio4over3, balanced, 73, 73, 0x692ee42856a2b2d5ULL, 0x352b34c04a6ea715ULL, -70.4, -92.6) \
        ROW(ratio4over3, transparent, 151, 131, 0x71a5a915c80aa3b1ULL, 0x0af398a526d71281ULL, -78.1, -91.0) \
        ROW(ratio3over4, super_economy, 63, 63, 0x1f7a0b635cbb4d81ULL, 0xbfe07900221be1f6ULL, -73.8, -97.2) \
        ROW(ratio3over4, economy, 79, 79, 0xbb2ed727a9d86dddULL, 0x0e344b588a2ef8d0ULL, -73.3, -96.5) \
        ROW(ratio3over4, balanced, 95, 95, 0xda66cda1682f0e01ULL, 0x82c84ac819b52543ULL, -73.7, -95.4) \
        ROW(ratio3over4, transparent, 199, 171, 0x3f7a4c963702157dULL, 0x9e00676910dca877ULL, -80.7, -94.6) \
        ROW(ratio8over3, super_economy, 99, 99, 0x6015b0be6cf257d5ULL, 0xb2aad1879cae7601ULL, -72.4, -93.7) \
        ROW(ratio8over3, economy, 141, 141, 0xf865893036d83da1ULL, 0x219a3cb8da2ee9e5ULL, -72.9, -92.3) \
        ROW(ratio8over3, balanced, 169, 169, 0x218616408337704dULL, 0xd0efdc7c70a13e19ULL, -73.6, -91.8) \
        ROW(ratio8over3, transparent, 351, 299, 0x5b3db0ec1995eb7dULL, 0x5c62fe64d140c1c5ULL, -77.9, -90.1) \
        ROW(ratio3over8, super_economy, 111, 111, 0xfa2686f95ac2bf81ULL, 0x210b1b414c8856c4ULL, -71.9, -98.8) \
        ROW(ratio3over8, economy, 159, 159, 0x82f7df5192bee815ULL, 0x576a97a9ba208208ULL, -73.4, -97.4) \
        ROW(ratio3over8, balanced, 191, 191, 0xb555b75f633910c5ULL, 0x1a82d3af686eea2fULL, -71.8, -97.9) \
        ROW(ratio3over8, transparent, 399, 349, 0x27417c3f4d4af3b5ULL, 0x98d65e3dded273c0ULL, -81.5, -95.5)
    // clang-format on

    struct pin_row {
        const char*   ratio;
        const char*   profile;
        std::size_t   float_macs;
        std::size_t   q15_macs;
        std::uint64_t fnv_q15;
        std::uint64_t fnv_q31;
        double        q15_stop_db;
        double        q15_rms_dbfs;
    };

    // Runs F<R>(row, profile) for every row of the table.
    template <template <typename> class F>
    void for_each_row() {
#define ROW(RATIO, PROF, FM, QM, F15, F31, STOP, RMS)                                                                  \
    F<RATIO>::run(pin_row { #RATIO, #PROF, FM, QM, F15, F31, STOP, RMS }, profile::PROF());
        TAP_SR_RATIONAL_FIXED_POINT_ROWS(ROW)
#undef ROW
    }

    // ------------------------------------------------------------------
    // Exact unity: every phase row (interpolators, mixed ratios) sums to the
    // format's 1.0, and so does every Q15 decimator branch and every Q31
    // decimator's whole filter.
    template <typename S, rational_ratio R>
    void expect_rows_sum_to_unity(const pin_row& row, const profile& p) {
        using tr                   = tap::dsp::sample_traits<S>;
        const auto           unity = static_cast<std::int64_t>(tr::k_coeff_scale);
        basic_stage<S, 1, R> c(p);
        std::int64_t         whole = 0;
        for (std::size_t r = 0; r < k_rows<R>; ++r) {
            std::int64_t sum = 0;
            for (std::size_t t = 0; t < c.row_length(); ++t) {
                sum += c.coefficient(r, t);
            }
            whole += sum;
            if constexpr (R::k_up != 1) {
                // Unity, or a Q15 mixed ratio going down's power-of-two gain.
                EXPECT_EQ(sum, unity * static_cast<std::int64_t>(basic_stage<S, 1, R>::k_table_gain))
                    << row.ratio << " " << row.profile << " q" << sizeof(S) * 8 << " row " << r;
            }
        }
        if constexpr (R::k_up == 1) {
            // A Q15 decimator holds each of its M branches at unity (its
            // finalize divides by M); Q31 holds the whole filter at unity.
            constexpr auto gain = static_cast<std::int64_t>(basic_stage<S, 1, R>::k_table_gain);
            EXPECT_EQ(whole, unity * gain)
                << row.ratio << " " << row.profile << " q" << sizeof(S) * 8 << " whole filter";
            if constexpr (basic_stage<S, 1, R>::k_branch_quantized) {
                for (std::size_t r = 0; r < k_rows<R>; ++r) {
                    std::int64_t sum = 0;
                    for (std::size_t t = 0; t < c.row_length(); ++t) {
                        sum += c.coefficient(r, t);
                    }
                    EXPECT_EQ(sum, unity) << row.ratio << " " << row.profile << " q16 branch " << r;
                }
            }
        }
    }

    template <typename R>
    struct rows_sum {
        static void run(const pin_row& row, const profile& p) {
            expect_rows_sum_to_unity<std::int16_t, R>(row, p);
            expect_rows_sum_to_unity<std::int32_t, R>(row, p);
        }
    };

    TEST(FixedPoint, EveryRowSumsToExactUnityInBothFormats) {
        for_each_row<rows_sum>();
    }

    // ------------------------------------------------------------------
    template <typename R>
    struct bit_pins {
        static void run(const pin_row& row, const profile& p) {
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int16_t, 1, R>(p)), row.fnv_q15)
                << row.ratio << " " << row.profile;
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int32_t, 1, R>(p)), row.fnv_q31)
                << row.ratio << " " << row.profile;
        }
    };

    TEST(FixedPoint, TablesAreBitPinned) {
        for_each_row<bit_pins>();
    }

    // A Q15 decimator's table IS its band's Q15 interpolator table: each of
    // its M branches is the unscaled design's branch quantized at unity, as
    // each interpolator phase is (the 1 / M rides the finalize), so the two
    // hash alike — and attain the same stopband — at every profile.
    template <rational_ratio Up, rational_ratio Down>
    void expect_shared_q15_table() {
        for (const profile& p :
             {profile::super_economy(), profile::economy(), profile::balanced(), profile::transparent()}) {
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int16_t, 1, Down>(p)),
                      table_fnv1a64(basic_stage<std::int16_t, 1, Up>(p)))
                << ratio_traits<Down>::k_band << " " << p.stopband_atten_db;
        }
    }

    TEST(FixedPoint, Q15DecimatorTableIsTheInterpolatorsTable) {
        expect_shared_q15_table<up2, down2>();
        expect_shared_q15_table<up3, down3>();
        expect_shared_q15_table<up4, down4>();
        expect_shared_q15_table<up6, down6>();
        expect_shared_q15_table<up8, down8>();
        static_assert(basic_stage<std::int16_t, 1, down6>::k_table_gain == 6);
        static_assert(basic_stage<std::int32_t, 1, down6>::k_table_gain == 1);
        static_assert(basic_stage<std::int16_t, 1, up6>::k_table_gain == 1);
        static_assert(basic_stage<std::int16_t, 1, down4>::k_table_gain == 4);
    }

    // ------------------------------------------------------------------
    // The structural zeros never enter the dot: each stored row's quantized
    // nonzero span holds no design zero, and the MACs counted are the spans'
    // sum — the design's nonzero count in float and Q31, the pinned
    // quantized span in Q15.
    template <typename S, rational_ratio R>
    std::size_t expect_no_structural_zero_in_the_dot(const pin_row& row, const profile& p, std::size_t& zeros_in_dot) {
        basic_stage<S, 1, R>      c(p);
        const std::vector<double> h      = design_stage<R>(p);
        constexpr std::size_t     band   = ratio_traits<R>::k_band;
        const std::size_t         center = (h.size() - 1) / 2;
        std::size_t               spans  = 0;
        zeros_in_dot                     = 0;
        for (std::size_t r = 0; r < k_rows<R>; ++r) {
            std::size_t first = c.row_length();
            std::size_t last  = 0;
            for (std::size_t t = 0; t < c.row_length(); ++t) {
                if (c.coefficient(r, t) != 0) {
                    first = std::min(first, t);
                    last  = t;
                }
            }
            if (first == c.row_length()) {
                continue;
            }
            spans += last - first + 1;
            for (std::size_t t = first; t <= last; ++t) {
                const std::size_t i = design_index<R>(r, t, c.row_length());
                if (i < h.size() && h[i] != 0.0) {
                    continue;
                }
                // A zero inside the dot: only a mixed ratio going down has
                // one, and only the band's structural zeros (its rows stride
                // by L while the zeros stride by its band M, so each L-phase
                // row crosses every M-th of them).
                EXPECT_TRUE(R::k_down > R::k_up && R::k_up != 1 && i < h.size()
                            && (i + band - center % band) % band == 0)
                    << row.ratio << " " << row.profile << " q" << sizeof(S) * 8 << " row " << r << " tap " << t
                    << ": a zero inside the dot that is not the mixed ratio's structural zero";
                ++zeros_in_dot;
            }
        }
        EXPECT_EQ(spans, c.macs_per_superblock()) << row.ratio << " " << row.profile << " q" << sizeof(S) * 8;
        if (R::k_down <= R::k_up || R::k_up == 1) {
            EXPECT_EQ(zeros_in_dot, 0u) << row.ratio << " " << row.profile;
        }
        return c.macs_per_superblock();
    }

    template <typename R>
    struct structural_zeros {
        static void run(const pin_row& row, const profile& p) {
            std::size_t zf = 0, z31 = 0, z15 = 0;
            EXPECT_EQ((expect_no_structural_zero_in_the_dot<float, R>(row, p, zf)), row.float_macs)
                << row.ratio << " " << row.profile;
            EXPECT_EQ((expect_no_structural_zero_in_the_dot<std::int32_t, R>(row, p, z31)), row.float_macs)
                << row.ratio << " " << row.profile << ": Q31 keeps every nonzero tap";
            EXPECT_EQ((expect_no_structural_zero_in_the_dot<std::int16_t, R>(row, p, z15)), row.q15_macs)
                << row.ratio << " " << row.profile;
            EXPECT_LE(row.q15_macs, row.float_macs);
            EXPECT_EQ(z31, zf) << row.ratio << " " << row.profile;
            if (zf != 0) {
                std::printf("[ measured ] %s %s: %zu of %zu MACs per superblock multiply a structural zero\n",
                            row.ratio, row.profile, zf, row.float_macs);
            }
        }
    };

    // The structural zeros never enter the dot of an interpolator, a
    // decimator or a mixed ratio going up (whose band is L, so its zeros all
    // fall in the centre phase, trimmed to the one centre tap). A mixed
    // ratio going down (2/3, 3/4, 3/8) runs the L-phase machine over a
    // band-M design, so each of its L rows crosses every M-th of the band's
    // zeros: those it multiplies, and only those (measured and printed; a
    // sparse-row lever for the codegen phase after M6).
    TEST(FixedPoint, StructuralZerosNeverEnterTheDot) {
        for_each_row<structural_zeros>();
    }

    // ------------------------------------------------------------------
    // Cross-precision on the reference noise (480 frames, peak 0.9), every
    // stage against double. Measured worst |Q31 - double| 2.5e-9 (3/4 at
    // transparent), worst |float - double| 5.7e-8 (by 8 at transparent;
    // bridge's float floor is 5e-8): Q31 sits a decade under float. Q15's RMS
    // deviation is the format's floor, -90.0 to -100.5 dBFS by stage (pinned
    // per row within 1 dB); its worst sample 1.38e-4 (2026-11-20, X17).
    struct floors {
        double worst_float = 0.0, worst_q31 = 0.0, worst_q15 = 0.0, worst_rms_q15 = -400.0;
    };
    floors g_floors; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): the test's running record

    template <typename R>
    struct cross_precision {
        static void run(const pin_row& row, const profile& p) {
            const auto x   = reference_noise();
            const auto d   = run_stage<double, R>(p, x);
            const auto f   = run_stage<float, R>(p, x);
            const auto q31 = run_stage<std::int32_t, R>(p, x);
            const auto q15 = run_stage<std::int16_t, R>(p, x);
            ASSERT_EQ(d.size(), q15.size());
            double wf = 0.0, w31 = 0.0, w15 = 0.0, e15 = 0.0;
            for (std::size_t i = 0; i < d.size(); ++i) {
                wf  = std::max(wf, std::fabs(f[i] - d[i]));
                w31 = std::max(w31, std::fabs(q31[i] - d[i]));
                w15 = std::max(w15, std::fabs(q15[i] - d[i]));
                e15 += (q15[i] - d[i]) * (q15[i] - d[i]);
            }
            const double rms15 = 10.0 * std::log10(e15 / static_cast<double>(d.size()));
            EXPECT_LT(wf, 6e-8) << row.ratio << " " << row.profile << ": float vs double";
            EXPECT_LT(w31, 5e-9) << row.ratio << " " << row.profile << ": Q31 vs double";
            EXPECT_LT(w15, 2.5e-4) << row.ratio << " " << row.profile << ": Q15 vs double";
            EXPECT_NEAR(rms15, row.q15_rms_dbfs, 1.0) << row.ratio << " " << row.profile << ": Q15 RMS floor";
            g_floors.worst_float   = std::max(g_floors.worst_float, wf);
            g_floors.worst_q31     = std::max(g_floors.worst_q31, w31);
            g_floors.worst_q15     = std::max(g_floors.worst_q15, w15);
            g_floors.worst_rms_q15 = std::max(g_floors.worst_rms_q15, rms15);
        }
    };

    TEST(FixedPoint, Q31TracksDoubleAndQ15IsTheFormatFloor) {
        g_floors = {};
        for_each_row<cross_precision>();
        std::printf("[ measured ] worst |float-double| %.2e, |Q31-double| %.2e, |Q15-double| %.2e, Q15 RMS "
                    "<= %.1f dBFS\n",
                    g_floors.worst_float, g_floors.worst_q31, g_floors.worst_q15, g_floors.worst_rms_q15);
        EXPECT_LT(g_floors.worst_q31 * 10.0, 6e-8) << "Q31 a decade under float's floor";
    }

    // ------------------------------------------------------------------
    // The stopband the quantized tables attain: the prototype reassembled
    // from the stored rows (in units of 1.0, a decimator's times M), its
    // worst response from the stopband edge to F_hi / 2 on the 16384-point
    // design grid, relative to its DC gain. Q31 keeps the design's (within
    // 0.1 dB measured); Q15's is the format's, pinned per stage within
    // 0.5 dB. A host suite (excluded on the QEMU legs): 128 tables through
    // a 16384-point DFT.
    template <typename S, rational_ratio R>
    double quantized_stopband_db(const profile& p) {
        basic_stage<S, 1, R>  c(p);
        constexpr std::size_t l     = R::k_up;
        constexpr std::size_t m     = R::k_down;
        constexpr std::size_t band  = l > m ? l : m;
        const std::size_t     t_len = c.row_length();
        const double          scale = tap::dsp::sample_traits<S>::k_coeff_scale;
        std::vector<double>   h(t_len * k_rows<R>, 0.0);
        for (std::size_t r = 0; r < k_rows<R>; ++r) {
            for (std::size_t t = 0; t < t_len; ++t) {
                h[design_index<R>(r, t, t_len)] =
                    static_cast<double>(c.coefficient(r, t)) / scale * (l == 1 ? static_cast<double>(m) : 1.0);
            }
        }
        double dc = 0.0;
        for (const double v : h) {
            dc += v;
        }
        const double f0    = (1.0 - p.passband_frac) / static_cast<double>(band);
        double       worst = -400.0;
        for (std::size_t g = 0; g < k_design_grid_points; ++g) {
            const double f = f0 + (0.5 - f0) * static_cast<double>(g) / static_cast<double>(k_design_grid_points - 1);
            std::complex<double> acc{0.0, 0.0};
            for (std::size_t i = 0; i < h.size(); ++i) {
                acc += h[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i));
            }
            worst = std::max(worst, 20.0 * std::log10(std::abs(acc) / dc + 1e-300));
        }
        return worst;
    }

    template <typename R>
    struct quantized_stopband {
        static void run(const pin_row& row, const profile& p) {
            const double q31 = quantized_stopband_db<std::int32_t, R>(p);
            const double q15 = quantized_stopband_db<std::int16_t, R>(p);
            EXPECT_LE(q31, -(p.stopband_atten_db + 0.9)) << row.ratio << " " << row.profile << ": Q31 keeps the spec";
            EXPECT_NEAR(q15, row.q15_stop_db, 0.5) << row.ratio << " " << row.profile << ": Q15's attained stopband";
        }
    };

    TEST(FixedPoint, QuantizedTablesAttainTheirStatedStopbands) {
        for_each_row<quantized_stopband>();
    }

    // The stopband the Q15 tables of the RELAXED designs attain: the stages
    // a chain runs are designed at their design divisor (PLAN.md 3.1,
    // profile::relaxed), and those tables are not the rows above. Every
    // (profile, divisor, band) of the relaxation tables, Q15, the band's
    // interpolator (a decimator's table is the same table, a mixed ratio's
    // rows the same coefficients at a power-of-two gain): the worst per
    // profile is printed and pinned within 0.5 dB, and at the 70 dB
    // profiles it attains the tier. With the designer's beta for A (until
    // X17) it did not: the 147/160 rows (the stage at bridge's rate) are the
    // widest transitions in the table, and at economy the half-band there
    // attained -67.6 dB (the 2026-10 audit, F05); with beta for A + 4 dB it
    // attains -72.1 dB and every relaxed Q15 design meets 70 dB (PLAN.md
    // section 6, the X17 record). A host suite (excluded on the QEMU legs).
    template <typename R>
    double relaxed_q15_stopband_db(const profile& base, const relaxed_pins& row) {
        return quantized_stopband_db<std::int16_t, R>(base.relaxed(row.divisor));
    }

    struct relaxed_worst {
        const char* profile;
        double      worst_db;
    };

    TEST(FixedPoint, RelaxedQ15TablesStopbandsAreStated) {
        // Measured 2026-11-20 (gcc 13, x86-64; exact integer tables, the same on
        // every host), the designer's beta for A + 4 dB (X17): the worst
        // relaxed Q15 design per profile (super_economy and economy at the
        // 4/1 and 2/1 divisors, balanced and transparent at 147/160).
        const relaxed_worst stated[] = {
            {"super_economy", -70.5}, {"economy", -70.4}, {"balanced", -70.1}, {"transparent", -72.7}};
        const profile profiles[] = {profile::super_economy(), profile::economy(), profile::balanced(),
                                    profile::transparent()};
        for (std::size_t i = 0; i < 4; ++i) {
            const profile& base  = profiles[i];
            double         worst = -400.0;
            const char*    where = "";
            for (const auto& row : base.relaxations) {
                if (row.divisor == tap::dsp::exact_ratio{1, 1}) {
                    continue; // the unrelaxed rows are the table above
                }
                const double bands[] = {
                    row.taps_per_branch[0] ? relaxed_q15_stopband_db<up2>(base, row) : -400.0,
                    row.taps_per_branch[1] ? relaxed_q15_stopband_db<up3>(base, row) : -400.0,
                    row.taps_per_branch[2] ? relaxed_q15_stopband_db<up4>(base, row) : -400.0,
                    row.taps_per_branch[3] ? relaxed_q15_stopband_db<up6>(base, row) : -400.0,
                    row.taps_per_branch[4] ? relaxed_q15_stopband_db<up8>(base, row) : -400.0,
                };
                static const char* const k_names[] = {"up2", "up3", "up4", "up6", "up8"};
                for (std::size_t b = 0; b < 5; ++b) {
                    std::printf("[ measured ] relaxed Q15 %s divisor %llu/%llu %s: %.1f dB\n", stated[i].profile,
                                static_cast<unsigned long long>(row.divisor.num),
                                static_cast<unsigned long long>(row.divisor.den), k_names[b], bands[b]);
                    if (bands[b] > worst) {
                        worst = bands[b];
                        where = k_names[b];
                    }
                }
            }
            std::printf("[ measured ] relaxed Q15 %s: worst %.1f dB (%s)\n", stated[i].profile, worst, where);
            EXPECT_NEAR(worst, stated[i].worst_db, 0.5) << stated[i].profile;
            EXPECT_LE(worst, -70.0) << stated[i].profile << ": every relaxed Q15 design attains the 70 dB tier";
        }
    }

    // ------------------------------------------------------------------
    // The 4th-band stage at every design divisor the coverage matrix runs it
    // at (X-a3: 2, 3, 4, 6, 8 and 12 above the 1/1 rows of the table at the
    // top, up and down alike; matrix.py rows), every named profile: the
    // Q15 table (one table, the decimator's being its band's interpolator
    // table), the Q31 tables of up 4 and down 4, and the stopband the Q15
    // table attains on the 16384-point grid from the relaxed stage's own
    // stopband edge. Tables at one m are one design whatever the divisor
    // (the divisor moves the edge the search measures from, not the
    // window), so several rows share a hash; their stopbands differ.
    // Measured 2026-11-21 (gcc 13, x86-64), by the code of this file.
    // clang-format off
#define TAP_SR_RATIONAL_BAND4_RELAXED_ROWS(ROW) \
        ROW(super_economy, 2, 0xb575b3b7e9ee5ca5ULL, 0x26952a84d6aab9c1ULL, 0x059e8a5aba3763e5ULL, -70.9) \
        ROW(super_economy, 3, 0xb575b3b7e9ee5ca5ULL, 0x26952a84d6aab9c1ULL, 0x059e8a5aba3763e5ULL, -70.9) \
        ROW(super_economy, 4, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.0) \
        ROW(super_economy, 6, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.3) \
        ROW(super_economy, 8, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.4) \
        ROW(super_economy, 12, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.4) \
        ROW(economy, 2, 0xb575b3b7e9ee5ca5ULL, 0x26952a84d6aab9c1ULL, 0x059e8a5aba3763e5ULL, -70.9) \
        ROW(economy, 3, 0xb575b3b7e9ee5ca5ULL, 0x26952a84d6aab9c1ULL, 0x059e8a5aba3763e5ULL, -70.9) \
        ROW(economy, 4, 0xb575b3b7e9ee5ca5ULL, 0x26952a84d6aab9c1ULL, 0x059e8a5aba3763e5ULL, -70.9) \
        ROW(economy, 6, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.3) \
        ROW(economy, 8, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.3) \
        ROW(economy, 12, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.4) \
        ROW(balanced, 2, 0x6fcf943d2eaff42dULL, 0xebcf162091cca619ULL, 0xb5f2496dd4742559ULL, -74.7) \
        ROW(balanced, 3, 0xb575b3b7e9ee5ca5ULL, 0x26952a84d6aab9c1ULL, 0x059e8a5aba3763e5ULL, -70.9) \
        ROW(balanced, 4, 0xb575b3b7e9ee5ca5ULL, 0x26952a84d6aab9c1ULL, 0x059e8a5aba3763e5ULL, -70.9) \
        ROW(balanced, 6, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.3) \
        ROW(balanced, 8, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.3) \
        ROW(balanced, 12, 0xb996279d33656921ULL, 0xf9390d1a5ab0c581ULL, 0x798a49656236da4dULL, -73.4) \
        ROW(transparent, 2, 0xff70fb6f5c0e99adULL, 0xa66f79f6d2180f91ULL, 0xb02cbfe2dc3278f1ULL, -82.8) \
        ROW(transparent, 3, 0x08c1a141c732ec49ULL, 0x126ce4efe70a98f5ULL, 0xf2f8f92784e372f1ULL, -84.0) \
        ROW(transparent, 4, 0x08c1a141c732ec49ULL, 0x126ce4efe70a98f5ULL, 0xf2f8f92784e372f1ULL, -84.0) \
        ROW(transparent, 6, 0x4a52e584fc12ef05ULL, 0x79eb5e51d4712015ULL, 0xee9db367de0da0d9ULL, -84.1) \
        ROW(transparent, 8, 0x4a52e584fc12ef05ULL, 0x79eb5e51d4712015ULL, 0xee9db367de0da0d9ULL, -84.1) \
        ROW(transparent, 12, 0x4a52e584fc12ef05ULL, 0x79eb5e51d4712015ULL, 0xee9db367de0da0d9ULL, -84.1)
    // clang-format on

    struct band4_row {
        const char*   profile;
        std::uint64_t divisor;
        std::uint64_t fnv_q15;
        std::uint64_t fnv_q31_up;
        std::uint64_t fnv_q31_down;
        double        q15_stop_db;
    };

    template <typename F>
    void for_each_band4_row(F&& f) {
#define ROW(PROF, DIV, F15, F31U, F31D, STOP)                                                                          \
    f(band4_row{.profile      = #PROF,                                                                                 \
                .divisor      = DIV,                                                                                   \
                .fnv_q15      = F15,                                                                                   \
                .fnv_q31_up   = F31U,                                                                                  \
                .fnv_q31_down = F31D,                                                                                  \
                .q15_stop_db  = STOP},                                                                                  \
      profile::PROF());
        TAP_SR_RATIONAL_BAND4_RELAXED_ROWS(ROW)
#undef ROW
    }

    TEST(FixedPoint, FourthBandTablesAreBitPinnedAtEveryMatrixDivisor) {
        std::size_t rows = 0;
        for_each_band4_row([&](const band4_row& row, const profile& base) {
            const profile p = base.relaxed(tap::dsp::exact_ratio{row.divisor, 1});
            ASSERT_NE(p.pinned_taps_per_branch(4), 0u) << row.profile << " " << row.divisor << ": a pinned divisor";
            const pin_row up{"up4", row.profile, 0, 0, 0, 0, 0.0, 0.0};
            const pin_row down{"down4", row.profile, 0, 0, 0, 0, 0.0, 0.0};
            expect_rows_sum_to_unity<std::int16_t, up4>(up, p);
            expect_rows_sum_to_unity<std::int32_t, up4>(up, p);
            expect_rows_sum_to_unity<std::int16_t, down4>(down, p);
            expect_rows_sum_to_unity<std::int32_t, down4>(down, p);
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int16_t, 1, up4>(p)), row.fnv_q15)
                << row.profile << " " << row.divisor;
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int16_t, 1, down4>(p)), row.fnv_q15)
                << row.profile << " " << row.divisor << ": the decimator's is the interpolator's table";
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int32_t, 1, up4>(p)), row.fnv_q31_up)
                << row.profile << " " << row.divisor;
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int32_t, 1, down4>(p)), row.fnv_q31_down)
                << row.profile << " " << row.divisor;
            ++rows;
        });
        EXPECT_EQ(rows, 24u); // 4 profiles x 6 divisors
    }

    // Each relaxed 4th-band Q15 table attains its stated stopband, within
    // 0.5 dB, and at the 70 dB profiles the tier (the worst -70.9 dB, at the
    // m = 4 design of divisors 2 to 4); Q31 keeps the spec. A host suite
    // (excluded on the QEMU legs, as the other stopband sweeps).
    TEST(FixedPoint, FourthBandRelaxedQ15TablesStopbandsAreStated) {
        for_each_band4_row([&](const band4_row& row, const profile& base) {
            const profile p   = base.relaxed(tap::dsp::exact_ratio{row.divisor, 1});
            const double  q15 = quantized_stopband_db<std::int16_t, up4>(p);
            EXPECT_NEAR(q15, row.q15_stop_db, 0.5) << row.profile << " " << row.divisor;
            const double q15_down = quantized_stopband_db<std::int16_t, down4>(p);
            const double q31_down = quantized_stopband_db<std::int32_t, down4>(p);
            EXPECT_NEAR(q15_down, q15, 1e-9) << row.profile << " " << row.divisor;
            EXPECT_LE(q31_down, -(p.stopband_atten_db + 0.9))
                << row.profile << " " << row.divisor << ": Q31 keeps the spec";
            if (p.stopband_atten_db < 100.0) {
                EXPECT_LE(q15, -70.0) << row.profile << " " << row.divisor << ": the 70 dB tier";
            }
        });
    }

    // ------------------------------------------------------------------
    // Full scale saturates, never wraps: random +-full-scale noise (the
    // densest drive: every window sum can exceed 1 by the filter's
    // overshoot) through every stage at economy and transparent; each output
    // is the double model's on the same input samples, clamped to full
    // scale, within the format's floor. A wrap would land ~2 away. Measured
    // worst 4.96e-4 (Q15: Q1.14 coefficient error summed at full scale,
    // after the per-branch lever; 4.83e-4 before X17) and 1.5e-8 (Q31).
    template <typename S, rational_ratio R>
    double worst_against_clamped_double(const profile& p) {
        std::vector<double> x(2048);
        std::uint32_t       s = 0x9E3779B9u;
        for (auto& v : x) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            v = (s & 1u) != 0 ? 1.0 : -1.0;
        }
        // The double model on exactly the samples S can hold.
        std::vector<double> xq(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            xq[i] = static_cast<double>(to_sample<S>(x[i])) / full_scale<S>();
        }
        const auto d = run_stage<double, R>(p, xq);
        const auto q = run_stage<S, R>(p, xq);
        double     w = 0.0;
        for (std::size_t i = 0; i < d.size(); ++i) {
            // The format's range in full-scale units: [min / max, 1].
            const double low = static_cast<double>(std::numeric_limits<S>::min()) / full_scale<S>();
            w                = std::max(w, std::fabs(q[i] - std::clamp(d[i], low, 1.0)));
        }
        return w;
    }

    std::array<double, 2> g_saturation{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): the record

    template <typename R>
    struct saturation {
        static void run(const pin_row& row, const profile& p) {
            if (std::strcmp(row.profile, "economy") != 0 && std::strcmp(row.profile, "transparent") != 0) {
                return;
            }
            const double q15 = worst_against_clamped_double<std::int16_t, R>(p);
            const double q31 = worst_against_clamped_double<std::int32_t, R>(p);
            EXPECT_LT(q15, 2e-3) << row.ratio << " " << row.profile;
            EXPECT_LT(q31, 1e-7) << row.ratio << " " << row.profile;
            g_saturation[0] = std::max(g_saturation[0], q15);
            g_saturation[1] = std::max(g_saturation[1], q31);
        }
    };

    TEST(FixedPoint, FullScaleNoiseSaturatesNeverWraps) {
        g_saturation = {0.0, 0.0};
        for_each_row<saturation>();
        std::printf("[ measured ] full-scale noise, worst |q - clamp(double)|: Q15 %.2e, Q31 %.2e (a wrap is ~2)\n",
                    g_saturation[0], g_saturation[1]);
    }

    // ------------------------------------------------------------------
    // Full-scale DC, positive and negative, is exactly full scale at every
    // output once the window has filled: the rows sum to exact unity and
    // the one rounding lands on the input value (Q31's per-product
    // 16-bit pre-shift loses under T LSBs of Q45, under half an output LSB
    // for any row shorter than 8192 taps).
    template <typename S, rational_ratio R>
    void expect_full_scale_dc_exact(const pin_row& row, const profile& p, S level) {
        basic_stage<S, 1, R> c(p);
        const std::size_t    fill = c.window_frames() + R::k_down;
        const std::size_t    n_in = fill + 2 * R::k_up * R::k_down + 4;
        std::vector<S>       x(n_in, level);
        std::vector<S>       y(c.outputs_for(n_in));
        const std::size_t    made = c.process(x.data(), n_in, y.data());
        const std::size_t    skip = c.outputs_for(0) + (fill * R::k_up) / R::k_down + 1;
        ASSERT_GT(made, skip);
        for (std::size_t n = skip; n < made; ++n) {
            ASSERT_EQ(y[n], level) << row.ratio << " " << row.profile << " q" << sizeof(S) * 8 << " n " << n;
        }
    }

    template <typename R>
    struct full_scale_dc {
        static void run(const pin_row& row, const profile& p) {
            expect_full_scale_dc_exact<std::int16_t, R>(row, p, std::numeric_limits<std::int16_t>::max());
            expect_full_scale_dc_exact<std::int16_t, R>(row, p, std::numeric_limits<std::int16_t>::min());
            expect_full_scale_dc_exact<std::int32_t, R>(row, p, std::numeric_limits<std::int32_t>::max());
            expect_full_scale_dc_exact<std::int32_t, R>(row, p, std::numeric_limits<std::int32_t>::min());
        }
    };

    TEST(FixedPoint, FullScaleDcIsExactFromEveryPhase) {
        for_each_row<full_scale_dc>();
    }

    // ------------------------------------------------------------------
    // Chains stack the floors stage by stage: Q15 and Q31 through named
    // chains against the double chain on the reference noise.
    template <typename Chain, typename ChainD>
    std::pair<double, double> chain_floor(const profile& p) {
        const auto x = reference_noise();
        ChainD     cd(p);
        Chain      cq(p);
        using sample = typename Chain::sample;
        std::vector<double> yd(cd.outputs_for(x.size()));
        cd.process(x.data(), x.size(), yd.data());
        std::vector<sample> xq(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            xq[i] = to_sample<sample>(x[i]);
        }
        std::vector<sample> yq(cq.outputs_for(xq.size()));
        cq.process(xq.data(), xq.size(), yq.data());
        double w = 0.0, e = 0.0;
        for (std::size_t i = 0; i < yd.size(); ++i) {
            const double diff = static_cast<double>(yq[i]) / full_scale<sample>() - yd[i];
            w                 = std::max(w, std::fabs(diff));
            e += diff * diff;
        }
        return {w, 10.0 * std::log10(e / static_cast<double>(yd.size()))};
    }

    TEST(FixedPoint, ChainsStackTheFloorStageByStage) {
        const profile eco = profile::economy();
        const auto    a   = chain_floor<down4_down4_down3<std::int16_t, 1>, down4_down4_down3<double, 1>>(eco);
        const auto    b   = chain_floor<up3_up4_up4<std::int16_t, 1>, up3_up4_up4<double, 1>>(eco);
        const auto    c   = chain_floor<down4_down2<std::int16_t, 1>, down4_down2<double, 1>>(eco);
        const auto    d   = chain_floor<down4_down4_down3<std::int32_t, 1>, down4_down4_down3<double, 1>>(eco);
        std::printf("[ measured ] Q15 chains: 1/48 worst %.2e rms %.1f dBFS; 48/1 worst %.2e rms %.1f dBFS; 1/8 "
                    "worst %.2e rms %.1f dBFS; Q31 1/48 worst %.2e\n",
                    a.first, a.second, b.first, b.second, c.first, c.second, d.first);
        EXPECT_LT(a.first, 4e-4);
        EXPECT_LT(b.first, 4e-4);
        EXPECT_LT(c.first, 4e-4);
        EXPECT_LT(a.second, -80.0);
        EXPECT_LT(b.second, -80.0);
        EXPECT_LT(c.second, -80.0);
        EXPECT_LT(d.first, 1e-8);
    }

} // namespace
