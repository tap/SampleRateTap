// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The coverage-matrix test (milestone M4, PLAN.md section 5 leg 3): every
// one of the 182 ordered pairs of the family's fourteen rates, the chains
// of section 3 built in double from the generated row table
// (tests/coverage/matrix_rows.h, tools/coverage/matrix.py). A row holds a
// chain per attenuation tier (X4, X-a4): the 70 dB tier's, which it runs
// at super_economy, economy and balanced, and the 120 dB tier's, which it
// runs at transparent, the same chain where the two selections agree. Per
// row and profile it pins MACs per output and latency of the tier's chain
// exactly against the table, checks a within-family chain is the named
// chain (its divisors are basic_chain's), measures the chain against its
// promise (f_pass, A, delta; 2.1) with a tone battery (images and aliases
// at the bins arithmetic predicts, the passband gain), checks accounting,
// flush and the impulse against the latency. Cross-family rows build
// bridge through support/bridge_stage.h (a test may name a sibling, 4.2);
// every row is measured, none skipped (bridge's rate scale K = 1, 2 landed
// as 2.2).

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "coverage/matrix_rows.h"
#include "support/bridge_stage.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
    using tap::dsp::exact_ratio;

    constexpr exact_ratio d(std::uint64_t num, std::uint64_t den) {
        return exact_ratio{num, den};
    }

    // The stage descriptors of the row table: a rational ratio type, or
    // bridge at a rate scale.
    template <unsigned K>
    struct bridge_down {};
    template <unsigned K>
    struct bridge_up {};

    template <typename T>
    struct is_bridge : std::false_type {};
    template <unsigned K>
    struct is_bridge<bridge_down<K>> : std::true_type {};
    template <unsigned K>
    struct is_bridge<bridge_up<K>> : std::true_type {};

    template <typename S, typename D>
    struct stage_for;
    template <typename S, rational_ratio R>
    struct stage_for<S, R> {
        using type = basic_stage<S, 1, R>;
    };
    template <typename S, unsigned K>
    struct stage_for<S, bridge_down<K>> {
        using type = rational_test::bridge_stage<S, tap::sr::bridge::direction::down_to_44k1, K>;
    };
    template <typename S, unsigned K>
    struct stage_for<S, bridge_up<K>> {
        using type = rational_test::bridge_stage<S, tap::sr::bridge::direction::up_to_48k, K>;
    };

    // The four profiles by index, rational's and bridge's by the same name.
    constexpr std::array<const char*, 4> k_profile_names = {"super_economy", "economy", "balanced", "transparent"};
    profile                              rational_profile(std::size_t i) {
        switch (i) {
        case 0:
            return profile::super_economy();
        case 1:
            return profile::economy();
        case 2:
            return profile::balanced();
        default:
            return profile::transparent();
        }
    }
    tap::sr::bridge::profile bridge_profile(std::size_t i) {
        switch (i) {
        case 0:
            return tap::sr::bridge::profile::super_economy();
        case 1:
            return tap::sr::bridge::profile::economy();
        case 2:
            return tap::sr::bridge::profile::balanced();
        default:
            return tap::sr::bridge::profile::transparent();
        }
    }

    template <typename S, typename D>
    typename stage_for<S, D>::type make_stage(std::size_t prof, exact_ratio divisor) {
        if constexpr (is_bridge<D>::value) {
            return typename stage_for<S, D>::type(bridge_profile(prof));
        }
        else {
            return typename stage_for<S, D>::type(rational_profile(prof).relaxed(divisor));
        }
    }

    /// The attenuation tiers (X4): a row runs the 70 dB tier's chain (tier
    /// 0, selected at economy) at super_economy, economy and balanced, and
    /// the 120 dB tier's (tier 1, selected at transparent) at transparent.
    constexpr std::size_t                k_tiers                  = 2;
    constexpr std::array<std::size_t, 2> k_tier_selecting_profile = {1, 3};
    constexpr std::size_t                tier_of(std::size_t prof) noexcept {
        return prof == 3 ? 1 : 0;
    }

    struct row_spec {
        std::size_t                   src;
        std::size_t                   dst;
        bool                          flagged;
        std::array<std::uint64_t, 16> pins; ///< (macs num, den, latency num, den) per profile, its tier's chain

        exact_ratio macs(std::size_t prof) const { return exact_ratio{pins[4 * prof], pins[4 * prof + 1]}; }
        exact_ratio latency(std::size_t prof) const { return exact_ratio{pins[4 * prof + 2], pins[4 * prof + 3]}; }
    };

    // ------------------------------------------------------------------
    // The chains behind one interface: every row is a distinct chain type,
    // so the measurement and streaming code is written once against this
    // handle and instantiated once, and a row's template code is only the
    // factory (182 rows otherwise cost minutes of compile per test
    // function).
    struct chain_handle {
        virtual ~chain_handle()                                                                     = default;
        virtual std::size_t                   process(const double* in, std::size_t n, double* out) = 0;
        virtual std::size_t                   outputs_for(std::size_t n) const                      = 0;
        virtual std::size_t                   frames_needed(std::size_t k) const                    = 0;
        virtual std::size_t                   flush(double* out)                                    = 0;
        virtual std::size_t                   flush_output_frames() const                           = 0;
        virtual void                          reset()                                               = 0;
        virtual exact_ratio                   latency_output_frames() const                         = 0;
        virtual exact_ratio                   macs_per_output() const                               = 0;
        virtual std::size_t                   ratio_up() const                                      = 0;
        virtual std::unique_ptr<chain_handle> clone() const                                         = 0;
    };

    template <typename... Descs>
    using row_chain = tap::dsp::chain<typename stage_for<double, Descs>::type...>;

    template <typename... Descs>
    class row_handle final : public chain_handle {
      public:
        row_handle(row_chain<Descs...> c, exact_ratio macs)
            : m_c(std::move(c))
            , m_macs(macs) {}
        std::size_t process(const double* in, std::size_t n, double* out) override { return m_c.process(in, n, out); }
        std::size_t outputs_for(std::size_t n) const override { return m_c.outputs_for(n); }
        std::size_t frames_needed(std::size_t k) const override { return m_c.frames_needed(k); }
        std::size_t flush(double* out) override { return m_c.flush(out); }
        std::size_t flush_output_frames() const override { return m_c.flush_output_frames(); }
        void        reset() override { m_c.reset(); }
        exact_ratio latency_output_frames() const override { return m_c.latency_output_frames(); }
        exact_ratio macs_per_output() const override { return m_macs; }
        std::size_t ratio_up() const override { return row_chain<Descs...>::k_up; }
        std::unique_ptr<chain_handle> clone() const override { return std::make_unique<row_handle>(*this); }

      private:
        row_chain<Descs...> m_c;
        exact_ratio         m_macs;
    };

    /// The rates along a chain: before each stage, and after the last.
    template <typename... Descs>
    std::vector<std::size_t> chain_rates(std::size_t src) {
        std::vector<std::size_t> rates{src};
        (
            [&] {
                using st = typename stage_for<double, Descs>::type;
                rates.push_back(rates.back() * st::k_up / st::k_down);
            }(),
            ...);
        return rates;
    }

    /// MACs per output, exact: each stage's superblock count over its
    /// outputs per superblock, scaled by its output rate over the chain's.
    template <typename... Descs>
    exact_ratio chain_macs(const row_chain<Descs...>& c, const std::vector<std::size_t>& rates) {
        exact_ratio total{0, 1};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((total =
                  total
                  + exact_ratio{c.template stage<I>().macs_per_superblock(),
                                std::tuple_element_t<I, std::tuple<typename stage_for<double, Descs>::type...>>::k_up}
                        * exact_ratio{rates[I + 1], rates.back()}),
             ...);
        }(std::index_sequence_for<Descs...>{});
        return total;
    }

    template <typename... Descs, std::size_t... I>
    row_chain<Descs...> build_chain(const std::vector<exact_ratio>& divisors, std::size_t prof,
                                    std::index_sequence<I...>) {
        return row_chain<Descs...>(make_stage<double, Descs>(prof, divisors[I])...);
    }

    /// One tier's chain of a row with its type erased: the rates, the
    /// table's divisors, a factory per profile, and for a within-family
    /// chain the named chain's compile-time divisors and its own exact
    /// numbers.
    struct tier_chain {
        std::vector<std::size_t>                                             rates;
        bool                                                                 has_bridge = false;
        std::vector<exact_ratio>                                             divisors;
        std::vector<exact_ratio>                                             named_divisors;
        std::function<std::unique_ptr<chain_handle>(std::size_t prof)>       build;
        std::function<std::pair<exact_ratio, exact_ratio>(std::size_t prof)> named_numbers;
    };

    template <typename... Descs>
    tier_chain make_tier(std::size_t src, std::vector<exact_ratio> divisors) {
        tier_chain t;
        t.rates      = chain_rates<Descs...>(src);
        t.has_bridge = (is_bridge<Descs>::value || ...);
        t.divisors   = std::move(divisors);
        t.build      = [divs = t.divisors, rates = t.rates](std::size_t prof) -> std::unique_ptr<chain_handle> {
            auto c    = build_chain<Descs...>(divs, prof, std::index_sequence_for<Descs...>{});
            auto macs = chain_macs<Descs...>(c, rates);
            return std::make_unique<row_handle<Descs...>>(std::move(c), macs);
        };
        if constexpr (!(is_bridge<Descs>::value || ...)) {
            constexpr auto named = basic_chain<double, 1, Descs...>::k_divisors;
            t.named_divisors.assign(named.begin(), named.end());
            t.named_numbers = [](std::size_t prof) {
                basic_chain<double, 1, Descs...> c(rational_profile(prof));
                return std::pair{c.macs_per_output_exact(), c.latency_output_frames()};
            };
        }
        return t;
    }

    /// A row of the matrix: the spec and its two tiers' chains.
    struct row {
        row_spec                        spec;
        std::array<tier_chain, k_tiers> tiers;
        std::string                     name;

        /// The chain the row runs at a profile: its tier's.
        const tier_chain&             at(std::size_t prof) const { return tiers[tier_of(prof)]; }
        std::unique_ptr<chain_handle> build(std::size_t prof) const { return at(prof).build(prof); }
        /// Whether the 120 dB tier runs a chain of its own (X4).
        bool split() const { return tiers[0].rates != tiers[1].rates; }
    };

    row make_row(row_spec spec, tier_chain seventy, tier_chain one_twenty) {
        row r;
        r.name  = std::to_string(spec.src) + " -> " + std::to_string(spec.dst);
        r.spec  = spec;
        r.tiers = {std::move(seventy), std::move(one_twenty)};
        return r;
    }

#define TAP_SR_UNPAREN(...) __VA_ARGS__

    /// Every row of the table, built once.
    const std::vector<row>& matrix_rows() {
        static const std::vector<row> k_rows = [] {
            std::vector<row> out;
#define ROW(SRC, DST, FLAG, DESCS, DIVS, TDESCS, TDIVS, ...)                                                           \
    out.push_back(make_row(row_spec{SRC, DST, FLAG, {{__VA_ARGS__}}},                                                  \
                           make_tier<TAP_SR_UNPAREN DESCS>(SRC, {TAP_SR_UNPAREN DIVS}),                                \
                           make_tier<TAP_SR_UNPAREN TDESCS>(SRC, {TAP_SR_UNPAREN TDIVS})));
            TAP_SR_RATIONAL_MATRIX_ROWS(ROW)
#undef ROW
            return out;
        }();
        return k_rows;
    }

    // ------------------------------------------------------------------
    // The measurement: a tone at input frequency f (an exact bin of the
    // analysis window) through the chain in double; the amplitude at every
    // bin arithmetic says an image or alias can land on (|k R_j +- f|
    // folded to the output's Nyquist, over every rate R_j of the chain)
    // that lies in the chain's passband, at or below f_pass — the promise
    // (2.1) is the passband; between f_pass and the output's Nyquist the
    // output is unspecified — and at the tone's own bin. The analysis
    // window is n output frames after the transient, n a multiple of every
    // R_j / f_out's denominator so every such frequency is a bin: a
    // rectangular window then measures each component exactly, without
    // leakage, which is what resolves -120 dB beside a 0 dB tone.
    struct tone_result {
        double worst_spur_db;
        double gain_db;
    };

    tone_result measure_tone(chain_handle& c, const std::vector<std::size_t>& rates, std::size_t n, std::size_t bin,
                             std::size_t skip, std::size_t pass_bin) {
        const std::size_t f_in  = rates.front();
        const std::size_t f_out = rates.back();
        c.reset();
        const std::size_t n_in = c.frames_needed(skip + n);
        const double      w    = 2.0 * std::numbers::pi * static_cast<double>(bin) * static_cast<double>(f_out)
                         / static_cast<double>(n) / static_cast<double>(f_in);
        std::vector<double> x(n_in);
        for (std::size_t i = 0; i < n_in; ++i) {
            x[i] = 0.5 * std::sin(w * static_cast<double>(i));
        }
        std::vector<double> y(c.outputs_for(n_in));
        const std::size_t   made = c.process(x.data(), n_in, y.data());
        EXPECT_GE(made, skip + n);
        std::set<std::size_t> bins;
        const auto            fold = [n](std::size_t b) {
            b %= n;
            return b > n / 2 ? n - b : b;
        };
        const std::size_t own = fold(bin);
        for (const std::size_t r : rates) {
            const std::size_t ru = r * n / f_out; // exact by n's construction
            for (std::size_t k = 0; k * ru <= 2 * n + bin; ++k) {
                bins.insert(fold(k * ru + bin));
                bins.insert(fold(k * ru >= bin ? k * ru - bin : bin - k * ru));
            }
        }
        const auto amplitude = [&](std::size_t b) {
            std::complex<double> acc{0.0, 0.0};
            const double         step = -2.0 * std::numbers::pi * static_cast<double>(b) / static_cast<double>(n);
            for (std::size_t k = 0; k < n; ++k) {
                acc += y[skip + k] * std::polar(1.0, step * static_cast<double>(k));
            }
            return 2.0 * std::abs(acc) / static_cast<double>(n);
        };
        double worst = 0.0;
        for (const std::size_t b : bins) {
            if (b != own && b != 0 && b <= pass_bin) {
                worst = std::max(worst, amplitude(b));
            }
        }
        const double gain = amplitude(own) / 0.5;
        return {20.0 * std::log10(std::max(worst / 0.5, 1e-300)), 20.0 * std::log10(gain)};
    }

    /// The analysis length: a multiple of every denominator of R_j / f_out,
    /// at least `target` frames.
    std::size_t analysis_length(const std::vector<std::size_t>& rates, std::size_t target) {
        const std::size_t f_out = rates.back();
        std::size_t       n0    = 1;
        for (const std::size_t r : rates) {
            n0 = std::lcm(n0, f_out / std::gcd(r, f_out));
        }
        return n0 * ((target + n0 - 1) / n0);
    }

    struct row_measurement {
        double      worst_spur_db   = -400.0; ///< the worst image or alias over the battery
        double      worst_ripple_db = 0.0;    ///< the largest |gain| deviation over the passband tones
        std::size_t tones           = 0;
    };

    row_measurement measure_row(const row& r, std::size_t prof) {
        const std::size_t     f_in     = r.spec.src;
        const std::size_t     f_out    = r.spec.dst;
        const double          r_min    = static_cast<double>(std::min(f_in, f_out));
        const profile         p        = rational_profile(prof);
        const double          f_pass   = p.passband_hz(r_min);
        const double          f_stop   = r_min - f_pass;
        const auto&           rates    = r.at(prof).rates;
        auto                  c        = r.build(prof);
        const std::size_t     n        = analysis_length(rates, 8192);
        const std::size_t     skip     = static_cast<std::size_t>(2.0 * c->latency_output_frames().value()) + 64;
        const double          bin_hz   = static_cast<double>(f_out) / static_cast<double>(n);
        const auto            pass_bin = static_cast<std::size_t>(f_pass / bin_hz);
        std::set<std::size_t> bins;
        for (const double frac : {0.1, 0.3, 0.5, 0.7, 0.85, 0.95, 0.99}) {
            const auto b = static_cast<std::size_t>(frac * f_pass / bin_hz);
            if (b >= 1) {
                bins.insert(b);
            }
        }
        row_measurement m;
        for (const std::size_t b : bins) {
            const tone_result t = measure_tone(*c, rates, n, b, skip, pass_bin);
            m.worst_spur_db     = std::max(m.worst_spur_db, t.worst_spur_db);
            m.worst_ripple_db   = std::max(m.worst_ripple_db, std::fabs(t.gain_db));
            ++m.tones;
        }
        // Stopband tones, where the input can hold any: from the chain's
        // stopband edge to the input's Nyquist.
        const double nyq_in = static_cast<double>(f_in) / 2.0;
        if (f_stop < nyq_in) {
            std::set<std::size_t> sbins;
            for (const double f : {f_stop * 1.001, f_stop * 1.05, f_stop + 0.25 * (nyq_in - f_stop),
                                   f_stop + 0.5 * (nyq_in - f_stop), nyq_in * 0.98}) {
                if (f > f_stop && f < nyq_in) {
                    sbins.insert(static_cast<std::size_t>(std::ceil(f / bin_hz)));
                }
            }
            for (const std::size_t b : sbins) {
                const tone_result t = measure_tone(*c, rates, n, b, skip, pass_bin);
                // A stopband tone must not reach the passband: its own
                // folded bin counts as a spur when it lands there.
                m.worst_spur_db = std::max(m.worst_spur_db, t.worst_spur_db);
                if (b % n <= pass_bin || (n - b % n) <= pass_bin) {
                    m.worst_spur_db = std::max(m.worst_spur_db, t.gain_db);
                }
                ++m.tones;
            }
        }
        return m;
    }

    // ------------------------------------------------------------------
    // What every row is checked for.
    struct totals {
        std::size_t                    rows = 0, flagged = 0, within = 0, split = 0, split_within = 0;
        std::array<row_measurement, 4> worst{};
    };

    void check_tier_structure(const row& r, std::size_t tier) {
        const tier_chain& tc = r.tiers[tier];
        ASSERT_EQ(tc.rates.back(), r.spec.dst) << r.name << " tier " << tier << ": the chain's ratio is not the pair's";
        ASSERT_EQ(tc.divisors.size(), tc.rates.size() - 1);
        if (!tc.has_bridge) {
            // A within-family chain: the named chain's divisors are the table's.
            ASSERT_EQ(tc.named_divisors.size(), tc.divisors.size());
            for (std::size_t i = 0; i < tc.named_divisors.size(); ++i) {
                EXPECT_EQ(tc.named_divisors[i], tc.divisors[i]) << r.name << " tier " << tier << " stage " << i;
            }
        }
        else {
            // The flag (R14): bridge's output rate >= 2x the chain's. A
            // bridge stage is the one whose divisor is 0/1 in the table;
            // both tiers place it at the same rates (R13), so one flag holds.
            bool expect_flag = false;
            for (std::size_t i = 0; i < tc.divisors.size(); ++i) {
                if (tc.divisors[i] == exact_ratio{0, 1} && tc.rates[i + 1] >= 2 * r.spec.dst) {
                    expect_flag = true;
                }
            }
            EXPECT_EQ(r.spec.flagged, expect_flag) << r.name << " tier " << tier;
        }
    }

    void check_row_structure(const row& r, totals& t) {
        ++t.rows;
        t.flagged += r.spec.flagged ? 1 : 0;
        for (std::size_t tier = 0; tier < k_tiers; ++tier) {
            check_tier_structure(r, tier);
        }
        ASSERT_EQ(r.tiers[0].has_bridge, r.tiers[1].has_bridge) << r.name << ": a row is within a family or across";
        t.within += r.tiers[0].has_bridge ? 0 : 1;
        if (r.split()) {
            ++t.split;
            t.split_within += r.tiers[0].has_bridge ? 0 : 1;
        }
        for (std::size_t prof = 0; prof < 4; ++prof) {
            const auto c = r.build(prof);
            EXPECT_EQ(c->macs_per_output(), r.spec.macs(prof))
                << r.name << " " << k_profile_names[prof] << " MACs per output";
            EXPECT_EQ(c->latency_output_frames(), r.spec.latency(prof))
                << r.name << " " << k_profile_names[prof] << " latency";
            if (!r.at(prof).has_bridge) {
                const auto [macs, latency] = r.at(prof).named_numbers(prof);
                EXPECT_EQ(macs, r.spec.macs(prof)) << r.name << " named chain";
                EXPECT_EQ(latency, c->latency_output_frames()) << r.name << " named chain";
            }
        }
    }

    /// One tier's chain, at the profile that selected it.
    void check_row_streaming(const row& r, std::size_t tier) {
        auto              c    = r.tiers[tier].build(k_tier_selecting_profile[tier]);
        const std::size_t up   = c->ratio_up();
        const std::string what = r.name + " tier " + std::to_string(tier);
        // Accounting from a few positions, the impulse at the latency, flush.
        const std::size_t   probe = 2 * up + 3;
        std::vector<double> x(c->frames_needed(probe) + 8, 0.25);
        std::vector<double> y(c->outputs_for(x.size()) + 8);
        for (std::size_t pos = 0; pos < 3; ++pos) {
            for (std::size_t n = 0; n <= 5; ++n) {
                const auto probe_chain = c->clone();
                EXPECT_EQ(probe_chain->process(x.data(), n, y.data()), c->outputs_for(n))
                    << what << " pos " << pos << " n " << n;
            }
            for (std::size_t k = 1; k <= probe; k += up) {
                const std::size_t n = c->frames_needed(k);
                EXPECT_GE(c->outputs_for(n), k) << what << " k " << k;
                EXPECT_LT(c->outputs_for(n - 1), k) << what << " k " << k;
            }
            c->process(x.data(), 1, y.data());
        }
        c->reset();
        const double        lat  = c->latency_output_frames().value();
        const std::size_t   n_in = c->frames_needed(static_cast<std::size_t>(2.0 * lat) + 16);
        std::vector<double> imp(n_in, 0.0);
        imp[0] = 1.0;
        std::vector<double> yi(c->outputs_for(n_in));
        c->process(imp.data(), n_in, yi.data());
        std::size_t peak = 0;
        for (std::size_t n = 1; n < yi.size(); ++n) {
            if (std::fabs(yi[n]) > std::fabs(yi[peak])) {
                peak = n;
            }
        }
        EXPECT_LE(std::fabs(static_cast<double>(peak) - lat), 0.5 + 1e-9) << what << " peak " << peak;
        // flush: the stated count, then silence.
        c->reset();
        c->process(x.data(), x.size(), y.data());
        const std::size_t   expect = c->flush_output_frames();
        std::vector<double> tail(expect + 1, 1.0);
        EXPECT_EQ(c->flush(tail.data()), expect) << what;
        std::vector<double> z(c->frames_needed(8) + 8, 0.0);
        std::vector<double> yz(c->outputs_for(z.size()));
        c->process(z.data(), z.size(), yz.data());
        for (const double v : yz) {
            EXPECT_EQ(v, 0.0) << what << " after flush";
        }
    }

    void check_row_spec(const row& r, totals& t, std::size_t prof) {
        const row_measurement m = measure_row(r, prof);
        const profile         p = rational_profile(prof);
        // The promise (2.1): every image and alias at or below -A; the
        // passband within the sum of the stages' ripple candidates (0.01
        // dB per 70 dB stage, 0.0001 per transparent stage; bridge's own
        // are 0.003 / 0.00001, held to the same).
        const double delta =
            static_cast<double>(r.at(prof).rates.size() - 1) * (p.stopband_atten_db >= 100.0 ? 0.0001 : 0.01);
        EXPECT_LE(m.worst_spur_db, -p.stopband_atten_db) << r.name << " " << k_profile_names[prof] << " worst spur";
        EXPECT_LE(m.worst_ripple_db, delta) << r.name << " " << k_profile_names[prof] << " ripple";
        t.worst[prof].worst_spur_db   = std::max(t.worst[prof].worst_spur_db, m.worst_spur_db);
        t.worst[prof].worst_ripple_db = std::max(t.worst[prof].worst_ripple_db, m.worst_ripple_db);
        t.worst[prof].tones += m.tones;
    }

    TEST(Matrix, EveryRowPinsItsMacsAndLatencyAndTheNamedChainsDivisors) {
        totals t;
        for (const row& r : matrix_rows()) {
            check_row_structure(r, t);
        }
        EXPECT_EQ(t.rows, 182u);
        EXPECT_EQ(t.rows, k_matrix_rows);
        EXPECT_EQ(t.flagged, 38u);
        EXPECT_EQ(t.within, 92u);
        // X4 (X-a4): the 120 dB tier runs a chain of its own in 25 rows, 17
        // of them within a family.
        EXPECT_EQ(t.split, 25u);
        EXPECT_EQ(t.split_within, 17u);
    }

    TEST(Matrix, EveryRowStreamsExactlyAndPeaksAtItsLatency) {
        for (const row& r : matrix_rows()) {
            for (std::size_t tier = 0; tier < k_tiers; ++tier) {
                check_row_streaming(r, tier);
            }
        }
    }

    void run_tone_battery(std::size_t prof) {
        totals t;
        for (const row& r : matrix_rows()) {
            check_row_spec(r, t, prof);
        }
        std::printf("matrix %s: %zu tones over 182 rows, worst spur %.2f dB, worst passband deviation %.5f dB\n",
                    k_profile_names[prof], t.worst[prof].tones, t.worst[prof].worst_spur_db,
                    t.worst[prof].worst_ripple_db);
    }

    TEST(Matrix, EveryRowMeetsEconomy) {
        run_tone_battery(1);
    }
    TEST(Matrix, EveryRowMeetsTransparent) {
        run_tone_battery(3);
    }
    TEST(Matrix, EveryRowMeetsSuperEconomy) {
        run_tone_battery(0);
    }
    TEST(Matrix, EveryRowMeetsBalanced) {
        run_tone_battery(2);
    }

} // namespace
