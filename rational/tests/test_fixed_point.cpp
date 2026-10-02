// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Contract battery for the fixed-point profiles (milestone M5). The stage is
// format-generic (stage.h through tap::dsp::sample_traits); what this file
// proves is each format's numeric contract, as numbers, for every ratio of
// the vocabulary at every named profile:
//
//   - every row of every table sums to exact unity in the format (a phase
//     row of an interpolator or mixed ratio to 2^14 / 2^30; a Q15
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
//     -71.7 dB; quantized as h / M in one row, as before the lever, it lost
//     about 20 log10 M dB: -62.9 dB; the 70 dB / 120 dB promises are float's
//     and Q31's);
//   - full-scale drive saturates, never wraps, in both formats.
//
// Numbers measured 2026-10-02 (clang 18, x86-64; the formats are exact
// integer arithmetic, so every host and target computes the same bits).

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
        basic_stage<S, R> c(1, p);
        std::vector<S>    xs(x.size());
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
    std::uint64_t table_fnv1a64(const basic_stage<S, R>& c) {
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
        ROW(up_2, super_economy, 19, 19, 0x3cba7ba5bf0200f1ULL, 0x65cde4429b552ad1ULL, -71.1, -94.0) \
        ROW(up_2, economy, 23, 23, 0x62302898b65a370dULL, 0xe57108ee5dcd11a5ULL, -72.3, -95.8) \
        ROW(up_2, balanced, 27, 27, 0x597a9bff98e1d2fdULL, 0x35690983e0275251ULL, -71.1, -92.8) \
        ROW(up_2, transparent, 63, 55, 0xcd3f2561c2e2d955ULL, 0x805ecb7ea3994265ULL, -77.3, -93.4) \
        ROW(down_2, super_economy, 19, 19, 0x3cba7ba5bf0200f1ULL, 0x23868914d48461fdULL, -71.1, -96.7) \
        ROW(down_2, economy, 23, 23, 0x62302898b65a370dULL, 0x8e72f5156b104145ULL, -72.3, -97.5) \
        ROW(down_2, balanced, 27, 27, 0x597a9bff98e1d2fdULL, 0xecc3467d30d76709ULL, -71.1, -94.9) \
        ROW(down_2, transparent, 63, 55, 0xcd3f2561c2e2d955ULL, 0x6ed3d49c1c32ca15ULL, -77.3, -95.7) \
        ROW(up_3, super_economy, 33, 33, 0xfd3b44a261da711dULL, 0xdb52b0963a9827a9ULL, -71.7, -94.3) \
        ROW(up_3, economy, 45, 45, 0xf843d9f1da25e251ULL, 0xffcac366d1724905ULL, -70.2, -93.5) \
        ROW(up_3, balanced, 53, 53, 0x862f2f9c1f13e64dULL, 0xe9b4f733f5f69a49ULL, -71.5, -94.0) \
        ROW(up_3, transparent, 101, 89, 0xd39ee5ef4c648899ULL, 0x658e039e0cf17da1ULL, -76.1, -91.2) \
        ROW(down_3, super_economy, 33, 33, 0xfd3b44a261da711dULL, 0x0e531ee9e040c832ULL, -71.7, -97.2) \
        ROW(down_3, economy, 45, 45, 0xf843d9f1da25e251ULL, 0x81e16a033f326f76ULL, -70.2, -96.8) \
        ROW(down_3, balanced, 53, 53, 0x862f2f9c1f13e64dULL, 0xc11d78c7a4530ef8ULL, -71.5, -97.7) \
        ROW(down_3, transparent, 101, 89, 0xd39ee5ef4c648899ULL, 0x6506baa7c5d51262ULL, -76.1, -95.7) \
        ROW(up_6, super_economy, 81, 81, 0x7376502a75faf5f5ULL, 0x9d6e292d48601b85ULL, -73.2, -93.1) \
        ROW(up_6, economy, 121, 121, 0x36e3c046fa633d21ULL, 0x39d801984353d381ULL, -71.5, -92.4) \
        ROW(up_6, balanced, 141, 141, 0x3237cb76f59e248dULL, 0x7c7d5cef3507c531ULL, -72.2, -92.3) \
        ROW(up_6, transparent, 251, 219, 0xe46f033362196a39ULL, 0xb9fec6bbf1288489ULL, -78.5, -90.7) \
        ROW(down_6, super_economy, 81, 81, 0x7376502a75faf5f5ULL, 0x782ec334d8cf342bULL, -73.2, -99.8) \
        ROW(down_6, economy, 121, 121, 0x36e3c046fa633d21ULL, 0x92a24b5cea1909cfULL, -71.5, -98.6) \
        ROW(down_6, balanced, 141, 141, 0x3237cb76f59e248dULL, 0x82885ec398c57779ULL, -72.2, -99.6) \
        ROW(down_6, transparent, 251, 219, 0xe46f033362196a39ULL, 0x6a3f54f3e1587481ULL, -78.5, -98.3) \
        ROW(up_8, super_economy, 113, 113, 0xeb745d04141f1bb1ULL, 0x878a639d0252fde5ULL, -72.0, -92.9) \
        ROW(up_8, economy, 169, 169, 0xdc280c16130175d1ULL, 0x192bcfb9abf918edULL, -71.7, -92.3) \
        ROW(up_8, balanced, 197, 197, 0x8cd458eb725f53ddULL, 0xbcda6fae5723088dULL, -72.6, -91.4) \
        ROW(up_8, transparent, 351, 303, 0x4b96078b2928931dULL, 0x11b5febcf65e1475ULL, -78.2, -90.2) \
        ROW(down_8, super_economy, 113, 113, 0xeb745d04141f1bb1ULL, 0xc6ed1a337eadf041ULL, -72.0, -99.5) \
        ROW(down_8, economy, 169, 169, 0xdc280c16130175d1ULL, 0x43d2f7ee82afa8c5ULL, -71.7, -97.9) \
        ROW(down_8, balanced, 197, 197, 0x8cd458eb725f53ddULL, 0xdc5baf7a000445a9ULL, -72.6, -96.9) \
        ROW(down_8, transparent, 351, 303, 0x4b96078b2928931dULL, 0x5a6e47c4d822ebb1ULL, -78.2, -99.5) \
        ROW(ratio_3_2, super_economy, 33, 33, 0xfd3b44a261da711dULL, 0xdb52b0963a9827a9ULL, -71.7, -94.4) \
        ROW(ratio_3_2, economy, 45, 45, 0xf843d9f1da25e251ULL, 0xffcac366d1724905ULL, -70.2, -93.4) \
        ROW(ratio_3_2, balanced, 53, 53, 0x862f2f9c1f13e64dULL, 0xe9b4f733f5f69a49ULL, -71.5, -94.0) \
        ROW(ratio_3_2, transparent, 101, 89, 0xd39ee5ef4c648899ULL, 0x658e039e0cf17da1ULL, -76.1, -91.1) \
        ROW(ratio_2_3, super_economy, 47, 47, 0xdb8cfd8e21dd8209ULL, 0x73dd9f039a900588ULL, -69.1, -92.1) \
        ROW(ratio_2_3, economy, 65, 65, 0xbe9edd5a10f87fedULL, 0xd990f930d9d2e66fULL, -69.7, -91.0) \
        ROW(ratio_2_3, balanced, 77, 77, 0x626ef4bf4a08d9b5ULL, 0xe2019e0deca448ddULL, -71.1, -91.5) \
        ROW(ratio_2_3, transparent, 149, 125, 0xdde5bec80bdf050dULL, 0x174688556c046e2bULL, -73.9, -90.6) \
        ROW(ratio_4_3, super_economy, 49, 49, 0xaba05cd262d404a9ULL, 0xf789914bb74deab5ULL, -70.6, -94.0) \
        ROW(ratio_4_3, economy, 67, 67, 0x54b85d414b471985ULL, 0xf803f1ea55624179ULL, -71.7, -93.6) \
        ROW(ratio_4_3, balanced, 85, 85, 0x441dab53737b273dULL, 0x088ccad6c8e4a9b9ULL, -70.4, -92.9) \
        ROW(ratio_4_3, transparent, 151, 131, 0x2766dd02d84bd799ULL, 0x5491f247aa9ad425ULL, -77.2, -90.6) \
        ROW(ratio_3_4, super_economy, 63, 63, 0x0ec9089ed99ced7dULL, 0xcacc9866f85a1482ULL, -70.7, -92.3) \
        ROW(ratio_3_4, economy, 87, 87, 0x6c0c99983b8387b2ULL, 0x5b0bcd2edd57e081ULL, -72.8, -91.0) \
        ROW(ratio_3_4, balanced, 111, 111, 0xf55da00d5439796fULL, 0xddc050765dd8bd23ULL, -68.2, -91.0) \
        ROW(ratio_3_4, transparent, 199, 167, 0xe50acc0baa4ec77dULL, 0x78b0ec09e7310bffULL, -75.8, -89.2) \
        ROW(ratio_8_3, super_economy, 113, 113, 0xeb745d04141f1bb1ULL, 0x878a639d0252fde5ULL, -72.0, -93.0) \
        ROW(ratio_8_3, economy, 169, 169, 0xdc280c16130175d1ULL, 0x192bcfb9abf918edULL, -71.7, -92.2) \
        ROW(ratio_8_3, balanced, 197, 197, 0x8cd458eb725f53ddULL, 0xbcda6fae5723088dULL, -72.6, -91.4) \
        ROW(ratio_8_3, transparent, 351, 303, 0x4b96078b2928931dULL, 0x11b5febcf65e1475ULL, -78.2, -90.4) \
        ROW(ratio_3_8, super_economy, 127, 127, 0xaebfc38e6ba5a325ULL, 0xca47a7f8e9a6c523ULL, -69.2, -90.6) \
        ROW(ratio_3_8, economy, 191, 189, 0x58767e8f10428845ULL, 0x3cc9b1b3c17f0e3bULL, -71.2, -87.9) \
        ROW(ratio_3_8, balanced, 223, 221, 0x48312e1a4a99cc49ULL, 0x3c098ec5243f25ecULL, -69.4, -89.3) \
        ROW(ratio_3_8, transparent, 399, 325, 0x4f12dd31639b2c9dULL, 0x7e280258af2f97a4ULL, -69.6, -87.1)
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
        using tr                = tap::dsp::sample_traits<S>;
        const auto        unity = static_cast<std::int64_t>(tr::k_coeff_scale);
        basic_stage<S, R> c(1, p);
        std::int64_t      whole = 0;
        for (std::size_t r = 0; r < k_rows<R>; ++r) {
            std::int64_t sum = 0;
            for (std::size_t t = 0; t < c.row_length(); ++t) {
                sum += c.coefficient(r, t);
            }
            whole += sum;
            if constexpr (R::k_up != 1) {
                EXPECT_EQ(sum, unity) << row.ratio << " " << row.profile << " q" << sizeof(S) * 8 << " row " << r;
            }
        }
        if constexpr (R::k_up == 1) {
            // A Q15 decimator holds each of its M branches at unity (its
            // finalize divides by M); Q31 holds the whole filter at unity.
            constexpr auto gain = static_cast<std::int64_t>(basic_stage<S, R>::k_table_gain);
            EXPECT_EQ(whole, unity * gain)
                << row.ratio << " " << row.profile << " q" << sizeof(S) * 8 << " whole filter";
            if constexpr (basic_stage<S, R>::k_branch_quantized) {
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
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int16_t, R>(1, p)), row.fnv_q15)
                << row.ratio << " " << row.profile;
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int32_t, R>(1, p)), row.fnv_q31)
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
            EXPECT_EQ(table_fnv1a64(basic_stage<std::int16_t, Down>(1, p)),
                      table_fnv1a64(basic_stage<std::int16_t, Up>(1, p)))
                << ratio_traits<Down>::k_band << " " << p.stopband_atten_db;
        }
    }

    TEST(FixedPoint, Q15DecimatorTableIsTheInterpolatorsTable) {
        expect_shared_q15_table<up_2, down_2>();
        expect_shared_q15_table<up_3, down_3>();
        expect_shared_q15_table<up_6, down_6>();
        expect_shared_q15_table<up_8, down_8>();
        static_assert(basic_stage<std::int16_t, down_6>::k_table_gain == 6);
        static_assert(basic_stage<std::int32_t, down_6>::k_table_gain == 1);
        static_assert(basic_stage<std::int16_t, up_6>::k_table_gain == 1);
    }

    // ------------------------------------------------------------------
    // The structural zeros never enter the dot: each stored row's quantized
    // nonzero span holds no design zero, and the MACs counted are the spans'
    // sum — the design's nonzero count in float and Q31, the pinned
    // quantized span in Q15.
    template <typename S, rational_ratio R>
    std::size_t expect_no_structural_zero_in_the_dot(const pin_row& row, const profile& p, std::size_t& zeros_in_dot) {
        basic_stage<S, R>         c(1, p);
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
    // stage against double. Measured worst |Q31 - double| 3.4e-9 (by 8 at
    // transparent), worst |float - double| 4.6e-8 (bridge's float floor is
    // 5e-8): Q31 sits a decade under float. Q15's RMS deviation is the
    // format's floor, -85.6 to -95.8 dBFS by stage (pinned per row within
    // 1 dB); its worst sample 1.64e-4.
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
    // 0.5 dB. A host suite (excluded on the QEMU legs): 112 tables through
    // a 16384-point DFT.
    template <typename S, rational_ratio R>
    double quantized_stopband_db(const profile& p) {
        basic_stage<S, R>     c(1, p);
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

    // ------------------------------------------------------------------
    // Full scale saturates, never wraps: random +-full-scale noise (the
    // densest drive: every window sum can exceed 1 by the filter's
    // overshoot) through every stage at economy and transparent; each output
    // is the double model's on the same input samples, clamped to full
    // scale, within the format's floor. A wrap would land ~2 away. Measured
    // worst 8.8e-4 (Q15: Q1.14 coefficient error summed at full scale) and
    // 1.6e-8 (Q31).
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
        basic_stage<S, R> c(1, p);
        const std::size_t fill = c.window_frames() + R::k_down;
        const std::size_t n_in = fill + 2 * R::k_up * R::k_down + 4;
        std::vector<S>    x(n_in, level);
        std::vector<S>    y(c.outputs_for(n_in));
        const std::size_t made = c.process(x.data(), n_in, y.data());
        const std::size_t skip = c.outputs_for(0) + (fill * R::k_up) / R::k_down + 1;
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
        ChainD     cd(1, p);
        Chain      cq(1, p);
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
        const auto    a   = chain_floor<down_3_down_8_down_2<std::int16_t>, down_3_down_8_down_2<double>>(eco);
        const auto    b   = chain_floor<up_2_up_8_up_3<std::int16_t>, up_2_up_8_up_3<double>>(eco);
        const auto    c   = chain_floor<ratio_3_8_down_3<std::int16_t>, ratio_3_8_down_3<double>>(eco);
        const auto    d   = chain_floor<down_3_down_8_down_2<std::int32_t>, down_3_down_8_down_2<double>>(eco);
        std::printf("[ measured ] Q15 chains: 1/48 worst %.2e rms %.1f dBFS; 48/1 worst %.2e rms %.1f dBFS; 1/8 (44.1) "
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
