/// @file tap_sr_rational_capi.cpp
/// @brief C ABI implementation: one interface over the named chains, the single stages, the recursive stages and
/// the guard band filter, in float, Q15 and Q31.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors

#include "tap_sr_rational_capi.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "tap/dsp/detail/expects.h"
#include "tap/sr/rational/rational.h"

namespace {

    using tap::dsp::exact_ratio;
    namespace rat = tap::sr::rational;

    // The chain or stage is a compile-time type in C++; the C ABI makes the
    // choice a runtime tag over the instantiations, behind one interface.
    // The sample format is a runtime tag too: each engine runs one format,
    // and a process / flush call in another format is refused (returns 0,
    // consumes and writes nothing).
    struct engine {
        virtual ~engine()                                                                     = default;
        virtual int         format() const                                                    = 0;
        virtual std::size_t process(const float* in, std::size_t n, float* out)               = 0;
        virtual std::size_t process(const std::int16_t* in, std::size_t n, std::int16_t* out) = 0;
        virtual std::size_t process(const std::int32_t* in, std::size_t n, std::int32_t* out) = 0;
        virtual std::size_t flush(std::int16_t* out)                                          = 0;
        virtual std::size_t flush(std::int32_t* out)                                          = 0;
        virtual std::size_t outputs_for(std::size_t n) const                                  = 0;
        virtual std::size_t frames_needed(std::size_t k) const                                = 0;
        virtual std::size_t flush(float* out)                                                 = 0;
        virtual std::size_t flush_output_frames() const                                       = 0;
        virtual void        reset()                                                           = 0;
        virtual exact_ratio latency_output_frames() const                                     = 0;
        virtual bool        exact_latency() const                                             = 0;
        virtual double      nominal_latency_output_frames() const                             = 0;
        virtual double      group_delay_output_frames(double f) const                         = 0;
        virtual exact_ratio macs_per_output() const                                           = 0;
        virtual exact_ratio ratio() const                                                     = 0;
        virtual std::size_t stages() const                                                    = 0;
        virtual std::size_t stage_taps(std::size_t i) const                                   = 0;
    };

    template <typename S>
    constexpr int format_of() {
        return std::is_same_v<S, float>          ? TAP_SR_RATIONAL_FORMAT_FLOAT
               : std::is_same_v<S, std::int16_t> ? TAP_SR_RATIONAL_FORMAT_Q15
                                                 : TAP_SR_RATIONAL_FORMAT_Q31;
    }

    /// Over a tap::dsp::chain of basic_stage<S, R>: a named chain
    /// (basic_chain) or a one-stage chain built at a stated divisor.
    template <typename Chain>
    class chain_engine final : public engine {
      public:
        using sample = typename Chain::sample;

        explicit chain_engine(Chain c)
            : m_c(std::move(c)) {}

        int         format() const override { return format_of<sample>(); }
        std::size_t process(const float* in, std::size_t n, float* out) override { return run(in, n, out); }
        std::size_t process(const std::int16_t* in, std::size_t n, std::int16_t* out) override {
            return run(in, n, out);
        }
        std::size_t process(const std::int32_t* in, std::size_t n, std::int32_t* out) override {
            return run(in, n, out);
        }
        std::size_t flush(float* out) override { return drain(out); }
        std::size_t flush(std::int16_t* out) override { return drain(out); }
        std::size_t flush(std::int32_t* out) override { return drain(out); }
        std::size_t outputs_for(std::size_t n) const override { return m_c.outputs_for(n); }
        std::size_t frames_needed(std::size_t k) const override { return m_c.frames_needed(k); }
        std::size_t flush_output_frames() const override { return m_c.flush_output_frames(); }
        void        reset() override { m_c.reset(); }
        exact_ratio latency_output_frames() const override { return m_c.latency_output_frames(); }
        bool        exact_latency() const override { return true; }
        double      nominal_latency_output_frames() const override { return m_c.nominal_latency_output_frames(); }
        double      group_delay_output_frames(double f) const override { return m_c.group_delay_output_frames(f); }
        exact_ratio ratio() const override { return exact_ratio{Chain::k_up, Chain::k_down}; }
        std::size_t stages() const override { return Chain::k_stages; }

        /// Each stage's MACs per superblock over its outputs per superblock,
        /// scaled by its output rate over the chain's (basic_chain's rule).
        exact_ratio macs_per_output() const override {
            exact_ratio total{0, 1};
            exact_ratio after{1, 1}; // the rate ratio from stage I's output to the chain's
            stage_fold<Chain::k_stages - 1>(total, after);
            return total;
        }

        std::size_t stage_taps(std::size_t i) const override {
            std::size_t taps = 0;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((i == I ? (taps = m_c.template stage<I>().taps()) : 0), ...);
            }(std::make_index_sequence<Chain::k_stages>{});
            return taps;
        }

      private:
        template <typename T>
        std::size_t run(const T* in, std::size_t n, T* out) {
            if constexpr (std::is_same_v<T, sample>) {
                return m_c.process(in, n, out);
            }
            else {
                return 0; // another format's converter
            }
        }

        template <typename T>
        std::size_t drain(T* out) {
            if constexpr (std::is_same_v<T, sample>) {
                return m_c.flush(out);
            }
            else {
                return 0;
            }
        }

        template <std::size_t I>
        void stage_fold(exact_ratio& total, exact_ratio& after) const {
            const auto& s = m_c.template stage<I>();
            using st      = std::remove_cvref_t<decltype(s)>;
            total         = total + exact_ratio{s.macs_per_superblock(), st::k_up == 1 ? 1 : st::k_up} * after;
            after         = after * exact_ratio{st::k_down, st::k_up};
            if constexpr (I > 0) {
                stage_fold<I - 1>(total, after);
            }
        }

        Chain m_c;
    };

    /// A stage that reports an exact latency (the guard band filter's linear
    /// form); a recursive stage, a cascade and the Chebyshev II do not.
    template <typename T>
    concept reports_exact_latency = requires(const T& t) {
        { t.latency_output_frames() } -> std::same_as<exact_ratio>;
    };
    /// A stage that reports a nominal latency and a group delay (I5).
    template <typename T>
    concept reports_nominal = requires(const T& t, double f) {
        { t.nominal_latency_output_frames() } -> std::convertible_to<double>;
        { t.group_delay_output_frames(f) } -> std::convertible_to<double>;
    };

    /// A recursive half-band stage's multiplies per output, exact: its
    /// design's per decimated output, over its factor going up.
    template <typename Stage>
    exact_ratio iir_stage_macs(const Stage& s) {
        return exact_ratio{s.multiplies(), Stage::k_is_decimator ? 1 : Stage::k_factor};
    }

    /// Over the recursive stages and the guard band filter (R21 to R28): a
    /// basic_iir_stage, a cascade (iir_up_by / iir_down_by, a
    /// tap::dsp::chain of them) or a guard_band_filter, each a sync stage of
    /// its own, its latency members those of its C++ type.
    template <typename T>
    class recursive_engine final : public engine {
      public:
        using sample = typename T::sample;

        explicit recursive_engine(T t)
            : m_t(std::move(t)) {}

        int         format() const override { return format_of<sample>(); }
        std::size_t process(const float* in, std::size_t n, float* out) override { return run(in, n, out); }
        std::size_t process(const std::int16_t* in, std::size_t n, std::int16_t* out) override {
            return run(in, n, out);
        }
        std::size_t process(const std::int32_t* in, std::size_t n, std::int32_t* out) override {
            return run(in, n, out);
        }
        std::size_t flush(float* out) override { return drain(out); }
        std::size_t flush(std::int16_t* out) override { return drain(out); }
        std::size_t flush(std::int32_t* out) override { return drain(out); }
        std::size_t outputs_for(std::size_t n) const override { return m_t.outputs_for(n); }
        std::size_t frames_needed(std::size_t k) const override { return m_t.frames_needed(k); }
        std::size_t flush_output_frames() const override { return m_t.flush_output_frames(); }
        void        reset() override { m_t.reset(); }
        exact_ratio ratio() const override { return exact_ratio{T::k_up, T::k_down}; }

        /// Exact only where the C++ type reports it (the linear form); the
        /// C entry point refuses the others (0 / 0).
        exact_ratio latency_output_frames() const override {
            if constexpr (reports_exact_latency<T>) {
                return m_t.latency_output_frames();
            }
            else {
                return exact_ratio{0, 1};
            }
        }
        bool   exact_latency() const override { return reports_exact_latency<T>; }
        double nominal_latency_output_frames() const override {
            if constexpr (reports_nominal<T>) {
                return m_t.nominal_latency_output_frames();
            }
            else {
                return latency_output_frames().value();
            }
        }
        double group_delay_output_frames(double f) const override {
            if constexpr (reports_nominal<T>) {
                return m_t.group_delay_output_frames(f);
            }
            else {
                return latency_output_frames().value(); // linear phase
            }
        }

        exact_ratio macs_per_output() const override {
            if constexpr (requires { T::k_stages; }) {
                // A cascade: each stage's at its output rate over the
                // cascade's, as chain_engine folds an FIR chain's.
                exact_ratio total{0, 1};
                exact_ratio after{1, 1};
                stage_fold<T::k_stages - 1>(total, after);
                return total;
            }
            else if constexpr (requires { m_t.multiplies(); }) {
                return iir_stage_macs(m_t); // one recursive stage
            }
            else {
                // The guard band filter: five per section or its N taps.
                return exact_ratio{static_cast<std::uint64_t>(m_t.macs_per_output()), 1};
            }
        }

        std::size_t stages() const override {
            if constexpr (requires { T::k_stages; }) {
                return T::k_stages;
            }
            else {
                return 1;
            }
        }
        /// A recursive stage has no taps (0); the linear form has its N.
        std::size_t stage_taps(std::size_t i) const override {
            if constexpr (requires { m_t.taps(); }) {
                return i == 0 ? m_t.taps() : 0;
            }
            else {
                return 0;
            }
        }

      private:
        template <typename U>
        std::size_t run(const U* in, std::size_t n, U* out) {
            if constexpr (std::is_same_v<U, sample>) {
                return m_t.process(in, n, out);
            }
            else {
                return 0; // another format's converter
            }
        }

        template <typename U>
        std::size_t drain(U* out) {
            if constexpr (std::is_same_v<U, sample>) {
                return m_t.flush(out);
            }
            else {
                return 0;
            }
        }

        template <std::size_t I>
        void stage_fold(exact_ratio& total, exact_ratio& after) const {
            const auto& s = m_t.template stage<I>();
            using st      = std::remove_cvref_t<decltype(s)>;
            total         = total + iir_stage_macs(s) * after;
            after         = after * exact_ratio{st::k_down, st::k_up};
            if constexpr (I > 0) {
                stage_fold<I - 1>(total, after);
            }
        }

        T m_t;
    };

    /// Any channel count but the native ones (one and two, CHANNELS_PLAN.md
    /// K8, option (b)): that many mono engines behind one handle, the
    /// interleaved frames split to them and their outputs interleaved back
    /// through staging buffers allocated here. Bit-identical to the native
    /// converter of that count, because the channels share nothing but the
    /// schedule, which every mono engine runs identically (pinned by
    /// CApi.FanOutIsTheNativeConverterBitForBit at 3, 6 and 12 channels).
    template <typename S>
    class fan_out_engine final : public engine {
      public:
        fan_out_engine(std::vector<std::unique_ptr<engine>> monos)
            : m_monos(std::move(monos))
            , m_in(k_chunk)
            , m_out(chunk_output_capacity())
            , m_flush(2 * m_monos.front()->flush_output_frames() + 64) {}

        int         format() const override { return m_monos.front()->format(); }
        std::size_t process(const float* in, std::size_t n, float* out) override { return run(in, n, out); }
        std::size_t process(const std::int16_t* in, std::size_t n, std::int16_t* out) override {
            return run(in, n, out);
        }
        std::size_t process(const std::int32_t* in, std::size_t n, std::int32_t* out) override {
            return run(in, n, out);
        }
        std::size_t flush(float* out) override { return drain(out); }
        std::size_t flush(std::int16_t* out) override { return drain(out); }
        std::size_t flush(std::int32_t* out) override { return drain(out); }
        std::size_t outputs_for(std::size_t n) const override { return m_monos.front()->outputs_for(n); }
        std::size_t frames_needed(std::size_t k) const override { return m_monos.front()->frames_needed(k); }
        std::size_t flush_output_frames() const override { return m_monos.front()->flush_output_frames(); }
        void        reset() override {
            for (auto& m : m_monos) {
                m->reset();
            }
        }
        exact_ratio latency_output_frames() const override { return m_monos.front()->latency_output_frames(); }
        bool        exact_latency() const override { return m_monos.front()->exact_latency(); }
        double      nominal_latency_output_frames() const override {
            return m_monos.front()->nominal_latency_output_frames();
        }
        double group_delay_output_frames(double f) const override {
            return m_monos.front()->group_delay_output_frames(f);
        }
        exact_ratio macs_per_output() const override { return m_monos.front()->macs_per_output(); }
        exact_ratio ratio() const override { return m_monos.front()->ratio(); }
        std::size_t stages() const override { return m_monos.front()->stages(); }
        std::size_t stage_taps(std::size_t i) const override { return m_monos.front()->stage_taps(i); }

      private:
        static constexpr std::size_t k_chunk = 256; ///< input frames staged per pass

        /// Outputs one chunk can reach: its frames times L / M, rounded up,
        /// plus a superblock's L for the phase it starts in.
        std::size_t chunk_output_capacity() const {
            const exact_ratio r = m_monos.front()->ratio();
            return static_cast<std::size_t>((k_chunk * r.num + r.den - 1) / r.den + r.num) + 8;
        }

        template <typename T>
        std::size_t run(const T* in, std::size_t n, T* out) {
            if constexpr (!std::is_same_v<T, S>) {
                return 0; // another format's converter
            }
            else {
                const std::size_t ch   = m_monos.size();
                std::size_t       made = 0;
                while (n > 0) {
                    std::size_t take = std::min(n, k_chunk);
                    while (outputs_for(take) > m_out.size()) {
                        take /= 2; // one frame never exceeds a superblock's L
                    }
                    std::size_t produced = 0;
                    for (std::size_t c = 0; c < ch; ++c) {
                        for (std::size_t i = 0; i < take; ++i) {
                            m_in[i] = in[i * ch + c];
                        }
                        produced = m_monos[c]->process(m_in.data(), take, m_out.data());
                        for (std::size_t k = 0; k < produced; ++k) {
                            out[(made + k) * ch + c] = m_out[k];
                        }
                    }
                    made += produced;
                    in += take * ch;
                    n -= take;
                }
                return made;
            }
        }

        template <typename T>
        std::size_t drain(T* out) {
            if constexpr (!std::is_same_v<T, S>) {
                return 0;
            }
            else {
                const std::size_t ch   = m_monos.size();
                const std::size_t need = flush_output_frames();
                if (need > m_flush.size()) {
                    m_flush.resize(need); // end of stream, not the streaming path
                }
                std::size_t made = 0;
                for (std::size_t c = 0; c < ch; ++c) {
                    made = m_monos[c]->flush(m_flush.data());
                    for (std::size_t k = 0; k < made; ++k) {
                        out[k * ch + c] = m_flush[k];
                    }
                }
                return made;
            }
        }

        std::vector<std::unique_ptr<engine>> m_monos;
        std::vector<S>                       m_in;
        std::vector<S>                       m_out;
        std::vector<S>                       m_flush;
    };

    /// The native one- and two-channel engines, and the fan-out for any
    /// other count (K8, option (b)); NULL when a mono engine is refused.
    template <typename S, typename Make>
    std::unique_ptr<engine> by_channels(unsigned channels, Make&& make) {
        if (channels == 1) {
            return make(std::integral_constant<std::size_t, 1>{});
        }
        if (channels == 2) {
            return make(std::integral_constant<std::size_t, 2>{});
        }
        std::vector<std::unique_ptr<engine>> monos;
        monos.reserve(channels);
        for (unsigned c = 0; c < channels; ++c) {
            monos.push_back(make(std::integral_constant<std::size_t, 1>{}));
            if (!monos.back()) {
                return nullptr;
            }
        }
        return std::make_unique<fan_out_engine<S>>(std::move(monos));
    }

    rat::profile profile_for(int p) {
        return p == 0   ? rat::profile::economy()
               : p == 1 ? rat::profile::transparent()
               : p == 2 ? rat::profile::balanced()
                        : rat::profile::super_economy();
    }

    // Both factories run the chain's own precheck first: a rejection is a
    // null engine (NULL at the C boundary), never a throw — and, built without
    // exceptions, never a terminate.
    template <typename S, rat::rational_ratio... Rs>
    std::unique_ptr<engine> make_chain(const rat::profile& p, unsigned channels) {
        return by_channels<S>(channels, [&](auto ch) -> std::unique_ptr<engine> {
            using chain = rat::basic_chain<S, decltype(ch)::value, Rs...>;
            if (chain::validate(p) != nullptr) {
                return nullptr;
            }
            return std::make_unique<chain_engine<chain>>(chain(p));
        });
    }

    // The chain values retired in 0.8.0 (the header lists them; X7 of
    // docs/X4_X8_PLAN.md): never reused, and rejected by create before any
    // factory runs.
    constexpr std::array<int, 10> k_retired_chains = {10, 11, 14, 15, 16, 21, 22, 25, 26, 27};

    constexpr bool is_retired(int chain) noexcept {
        return std::find(k_retired_chains.begin(), k_retired_chains.end(), chain) != k_retired_chains.end();
    }

    template <typename S>
    std::unique_ptr<engine> make_named(int chain, const rat::profile& p, unsigned ch) {
        using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
        switch (chain) {
        case TAP_SR_RATIONAL_UP2:
            return make_chain<S, up2>(p, ch);
        case TAP_SR_RATIONAL_DOWN2:
            return make_chain<S, down2>(p, ch);
        case TAP_SR_RATIONAL_UP3:
            return make_chain<S, up3>(p, ch);
        case TAP_SR_RATIONAL_DOWN3:
            return make_chain<S, down3>(p, ch);
        case TAP_SR_RATIONAL_RATIO3OVER2:
            return make_chain<S, ratio3over2>(p, ch);
        case TAP_SR_RATIONAL_RATIO2OVER3:
            return make_chain<S, ratio2over3>(p, ch);
        case TAP_SR_RATIONAL_RATIO4OVER3:
            return make_chain<S, ratio4over3>(p, ch);
        case TAP_SR_RATIONAL_RATIO3OVER4:
            return make_chain<S, ratio3over4>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP2:
            return make_chain<S, up2, up2>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP3:
            return make_chain<S, up2, up3>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP6:
            return make_chain<S, up2, up6>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP8:
            return make_chain<S, up2, up8>(p, ch);
        case TAP_SR_RATIONAL_UP2_RATIO4OVER3:
            return make_chain<S, up2, ratio4over3>(p, ch);
        case TAP_SR_RATIONAL_RATIO3OVER4_DOWN2:
            return make_chain<S, ratio3over4, down2>(p, ch);
        case TAP_SR_RATIONAL_DOWN2_DOWN2:
            return make_chain<S, down2, down2>(p, ch);
        case TAP_SR_RATIONAL_DOWN3_DOWN2:
            return make_chain<S, down3, down2>(p, ch);
        case TAP_SR_RATIONAL_DOWN6_DOWN2:
            return make_chain<S, down6, down2>(p, ch);
        case TAP_SR_RATIONAL_DOWN8_DOWN2:
            return make_chain<S, down8, down2>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP4:
            return make_chain<S, up2, up4>(p, ch);
        case TAP_SR_RATIONAL_UP3_UP4:
            return make_chain<S, up3, up4>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP4_UP2:
            return make_chain<S, up2, up4, up2>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP3_UP4:
            return make_chain<S, up2, up3, up4>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP4_UP4:
            return make_chain<S, up2, up4, up4>(p, ch);
        case TAP_SR_RATIONAL_UP3_UP4_UP4:
            return make_chain<S, up3, up4, up4>(p, ch);
        case TAP_SR_RATIONAL_DOWN4_DOWN2:
            return make_chain<S, down4, down2>(p, ch);
        case TAP_SR_RATIONAL_DOWN4_DOWN3:
            return make_chain<S, down4, down3>(p, ch);
        case TAP_SR_RATIONAL_DOWN2_DOWN4_DOWN2:
            return make_chain<S, down2, down4, down2>(p, ch);
        case TAP_SR_RATIONAL_DOWN4_DOWN3_DOWN2:
            return make_chain<S, down4, down3, down2>(p, ch);
        case TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN2:
            return make_chain<S, down4, down4, down2>(p, ch);
        case TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN3:
            return make_chain<S, down4, down4, down3>(p, ch);
        case TAP_SR_RATIONAL_UP2_RATIO3OVER2:
            return make_chain<S, up2, ratio3over2>(p, ch);
        case TAP_SR_RATIONAL_UP2_UP6_UP4:
            return make_chain<S, up2, up6, up4>(p, ch);
        case TAP_SR_RATIONAL_DOWN4_DOWN6_DOWN2:
            return make_chain<S, down4, down6, down2>(p, ch);
        default:
            return nullptr;
        }
    }

    template <typename S, rat::rational_ratio R>
    std::unique_ptr<engine> make_stage(const rat::profile& p, exact_ratio divisor, unsigned channels) {
        return by_channels<S>(channels, [&](auto ch) -> std::unique_ptr<engine> {
            using stage = rat::basic_stage<S, decltype(ch)::value, R>;
            using chain = tap::dsp::chain<stage>;
            if (stage::validate(p.relaxed(divisor)) != nullptr) {
                return nullptr;
            }
            return std::make_unique<chain_engine<chain>>(chain(stage(p.relaxed(divisor))));
        });
    }

    template <typename S>
    std::unique_ptr<engine> make_single(unsigned l, unsigned m, const rat::profile& p, exact_ratio d, unsigned ch) {
        using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
        const auto key = (static_cast<unsigned>(l) << 8) | m;
        switch (key) {
        case (2u << 8) | 1u:
            return make_stage<S, up2>(p, d, ch);
        case (1u << 8) | 2u:
            return make_stage<S, down2>(p, d, ch);
        case (3u << 8) | 1u:
            return make_stage<S, up3>(p, d, ch);
        case (1u << 8) | 3u:
            return make_stage<S, down3>(p, d, ch);
        case (4u << 8) | 1u:
            return make_stage<S, up4>(p, d, ch);
        case (1u << 8) | 4u:
            return make_stage<S, down4>(p, d, ch);
        case (6u << 8) | 1u:
            return make_stage<S, up6>(p, d, ch);
        case (1u << 8) | 6u:
            return make_stage<S, down6>(p, d, ch);
        case (8u << 8) | 1u:
            return make_stage<S, up8>(p, d, ch);
        case (1u << 8) | 8u:
            return make_stage<S, down8>(p, d, ch);
        case (3u << 8) | 2u:
            return make_stage<S, ratio3over2>(p, d, ch);
        case (2u << 8) | 3u:
            return make_stage<S, ratio2over3>(p, d, ch);
        case (4u << 8) | 3u:
            return make_stage<S, ratio4over3>(p, d, ch);
        case (3u << 8) | 4u:
            return make_stage<S, ratio3over4>(p, d, ch);
        case (8u << 8) | 3u:
            return make_stage<S, ratio8over3>(p, d, ch);
        case (3u << 8) | 8u:
            return make_stage<S, ratio3over8>(p, d, ch);
        default:
            return nullptr;
        }
    }

    /// A recursive stage (factor 2) or cascade (4, 8, 16) in variant P going
    /// up or down, at the profile; NULL where the type's validate() refuses
    /// it (D17: never for a named profile, which every variant reaches at
    /// every divisor a cascade uses).
    template <typename S, rat::iir_phase P, bool Up, std::size_t D>
    std::unique_ptr<engine> make_iir_typed(const rat::profile& p, unsigned channels) {
        return by_channels<S>(channels, [&](auto ch) -> std::unique_ptr<engine> {
            constexpr std::size_t C = decltype(ch)::value;
            using stage =
                std::conditional_t<D == 2, rat::basic_iir_stage<S, C, std::conditional_t<Up, rat::up2, rat::down2>, P>,
                                   rat::basic_iir_cascade<S, C, P, D, Up>>;
            if (stage::validate(p) != nullptr) {
                return nullptr;
            }
            return std::make_unique<recursive_engine<stage>>(stage(p));
        });
    }

    template <typename S, rat::iir_phase P, bool Up>
    std::unique_ptr<engine> make_iir_factor(unsigned factor, const rat::profile& p, unsigned channels) {
        switch (factor) {
        case 2:
            return make_iir_typed<S, P, Up, 2>(p, channels);
        case 4:
            return make_iir_typed<S, P, Up, 4>(p, channels);
        case 8:
            return make_iir_typed<S, P, Up, 8>(p, channels);
        case 16:
            return make_iir_typed<S, P, Up, 16>(p, channels);
        default:
            return nullptr;
        }
    }

    template <typename S>
    std::unique_ptr<engine> make_iir(int direction, unsigned factor, int phase, const rat::profile& p,
                                     unsigned channels) {
        constexpr auto nonlinear   = rat::iir_phase::nonlinear;
        constexpr auto near_linear = rat::iir_phase::near_linear;
        const bool     up          = direction == TAP_SR_RATIONAL_DIRECTION_UP;
        if (phase == TAP_SR_RATIONAL_IIR_NONLINEAR) {
            return up ? make_iir_factor<S, nonlinear, true>(factor, p, channels)
                      : make_iir_factor<S, nonlinear, false>(factor, p, channels);
        }
        return up ? make_iir_factor<S, near_linear, true>(factor, p, channels)
                  : make_iir_factor<S, near_linear, false>(factor, p, channels);
    }

    /// The guard band filter's named setting for a profile tag behind a kind.
    rat::guard_band_settings guard_band_settings_for(int profile, rat::half_band_kind kind) {
        return profile == 0   ? rat::guard_band_settings::economy(kind)
               : profile == 1 ? rat::guard_band_settings::transparent(kind)
               : profile == 2 ? rat::guard_band_settings::balanced(kind)
                              : rat::guard_band_settings::super_economy(kind);
    }

    template <typename S, rat::guard_phase G>
    std::unique_ptr<engine> make_guard_band_typed(const rat::guard_band_settings& s, unsigned channels) {
        return by_channels<S>(channels, [&](auto ch) -> std::unique_ptr<engine> {
            using filter = rat::guard_band_filter<S, decltype(ch)::value, G>;
            if (filter::validate(s) != nullptr) {
                return nullptr;
            }
            return std::make_unique<recursive_engine<filter>>(filter(s));
        });
    }

    template <typename S>
    std::unique_ptr<engine> make_guard_band(int form, const rat::guard_band_settings& s, unsigned channels) {
        return form == TAP_SR_RATIONAL_GUARD_NONLINEAR
                   ? make_guard_band_typed<S, rat::guard_phase::nonlinear>(s, channels)
                   : make_guard_band_typed<S, rat::guard_phase::linear>(s, channels);
    }

    /// The format tag's sample type, as a factory call.
    template <typename Make>
    std::unique_ptr<engine> by_format(int format, Make&& make) {
        switch (format) {
        case TAP_SR_RATIONAL_FORMAT_FLOAT:
            return make(float{});
        case TAP_SR_RATIONAL_FORMAT_Q15:
            return make(std::int16_t{});
        case TAP_SR_RATIONAL_FORMAT_Q31:
            return make(std::int32_t{});
        default:
            return nullptr;
        }
    }

    void put(exact_ratio r, uint64_t* num, uint64_t* den) {
        if (num != nullptr) {
            *num = r.num;
        }
        if (den != nullptr) {
            *den = r.den;
        }
    }

} // namespace

struct tap_sr_rational_converter {
    std::unique_ptr<engine> e;
};

// The library builds with hidden visibility (CMakeLists.txt); only the C entry
// points below are exported, through TAP_SR_RATIONAL_API on their declarations
// in the header (default visibility here, dllexport on Windows).
extern "C" {

tap_sr_rational_converter* tap_sr_rational_create_format(int chain, int profile, int format, unsigned channels) {
    if (chain < 0 || chain >= TAP_SR_RATIONAL_CHAIN_END || is_retired(chain) || profile < 0 || profile > 3
        || channels == 0) {
        return nullptr;
    }
    const auto construct = [&]() -> tap_sr_rational_converter* {
        auto c = std::make_unique<tap_sr_rational_converter>();
        c->e   = by_format(format,
                           [&](auto tag) { return make_named<decltype(tag)>(chain, profile_for(profile), channels); });
        return c->e ? c.release() : nullptr;
    };
#if TAP_DSP_HAS_EXCEPTIONS
    try {
        return construct();
    }
    catch (...) {
        return nullptr; // a refused allocation; the arguments were validated by the factory
    }
#else
    return construct();
#endif
}

tap_sr_rational_converter* tap_sr_rational_create(int chain, int profile, unsigned channels) {
    return tap_sr_rational_create_format(chain, profile, TAP_SR_RATIONAL_FORMAT_FLOAT, channels);
}

tap_sr_rational_converter* tap_sr_rational_create_stage_format(unsigned L, unsigned M, int profile,
                                                               uint32_t divisor_num, uint32_t divisor_den, int format,
                                                               unsigned channels) {
    if (L > 255 || M > 255 || profile < 0 || profile > 3 || channels == 0 || divisor_num == 0 || divisor_den == 0) {
        return nullptr;
    }
    const auto construct = [&]() -> tap_sr_rational_converter* {
        auto c = std::make_unique<tap_sr_rational_converter>();
        c->e   = by_format(format, [&](auto tag) {
            return make_single<decltype(tag)>(L, M, profile_for(profile), exact_ratio{divisor_num, divisor_den},
                                                channels);
        });
        return c->e ? c.release() : nullptr;
    };
#if TAP_DSP_HAS_EXCEPTIONS
    try {
        return construct();
    }
    catch (...) {
        return nullptr; // a refused allocation; the arguments were validated by the factory
    }
#else
    return construct();
#endif
}

tap_sr_rational_converter* tap_sr_rational_create_stage(unsigned L, unsigned M, int profile, uint32_t divisor_num,
                                                        uint32_t divisor_den, unsigned channels) {
    return tap_sr_rational_create_stage_format(L, M, profile, divisor_num, divisor_den, TAP_SR_RATIONAL_FORMAT_FLOAT,
                                               channels);
}

tap_sr_rational_converter* tap_sr_rational_create_iir_format(int direction, unsigned factor, int phase, int profile,
                                                             int format, unsigned channels) {
    if ((direction != TAP_SR_RATIONAL_DIRECTION_UP && direction != TAP_SR_RATIONAL_DIRECTION_DOWN)
        || (factor != 2 && factor != 4 && factor != 8 && factor != 16)
        || (phase != TAP_SR_RATIONAL_IIR_NONLINEAR && phase != TAP_SR_RATIONAL_IIR_NEAR_LINEAR) || profile < 0
        || profile > 3 || channels == 0) {
        return nullptr;
    }
    const auto construct = [&]() -> tap_sr_rational_converter* {
        auto c = std::make_unique<tap_sr_rational_converter>();
        c->e   = by_format(format, [&](auto tag) {
            return make_iir<decltype(tag)>(direction, factor, phase, profile_for(profile), channels);
        });
        return c->e ? c.release() : nullptr;
    };
#if TAP_DSP_HAS_EXCEPTIONS
    try {
        return construct();
    }
    catch (...) {
        return nullptr; // a refused allocation; the arguments were validated by the factory
    }
#else
    return construct();
#endif
}

tap_sr_rational_converter* tap_sr_rational_create_guard_band_format(int form, int half_band_kind, int profile,
                                                                    int format, unsigned channels) {
    if ((form != TAP_SR_RATIONAL_GUARD_NONLINEAR && form != TAP_SR_RATIONAL_GUARD_LINEAR)
        || half_band_kind < TAP_SR_RATIONAL_HALF_BAND_FIR || half_band_kind > TAP_SR_RATIONAL_HALF_BAND_NEAR_LINEAR
        || profile < 0 || profile > 3 || channels == 0) {
        return nullptr;
    }
    const rat::half_band_kind      kind = half_band_kind == TAP_SR_RATIONAL_HALF_BAND_FIR ? rat::half_band_kind::fir
                                          : half_band_kind == TAP_SR_RATIONAL_HALF_BAND_NONLINEAR
                                              ? rat::half_band_kind::nonlinear
                                              : rat::half_band_kind::near_linear;
    const rat::guard_band_settings s    = guard_band_settings_for(profile, kind);
    const auto                     construct = [&]() -> tap_sr_rational_converter* {
        auto c = std::make_unique<tap_sr_rational_converter>();
        c->e = by_format(format, [&](auto tag) { return make_guard_band<decltype(tag)>(form, s, channels); });
        return c->e ? c.release() : nullptr;
    };
#if TAP_DSP_HAS_EXCEPTIONS
    try {
        return construct();
    }
    catch (...) {
        return nullptr; // a refused allocation; the arguments were validated by the factory
    }
#else
    return construct();
#endif
}

void tap_sr_rational_destroy(tap_sr_rational_converter* c) {
    delete c;
}

void tap_sr_rational_ratio(const tap_sr_rational_converter* c, unsigned* L, unsigned* M) {
    const exact_ratio r = c->e->ratio();
    if (L != nullptr) {
        *L = static_cast<unsigned>(r.num);
    }
    if (M != nullptr) {
        *M = static_cast<unsigned>(r.den);
    }
}

uint64_t tap_sr_rational_outputs_for(const tap_sr_rational_converter* c, uint64_t in_frames) {
    return c->e->outputs_for(static_cast<std::size_t>(in_frames));
}

uint64_t tap_sr_rational_frames_needed(const tap_sr_rational_converter* c, uint64_t out_frames) {
    return c->e->frames_needed(static_cast<std::size_t>(out_frames));
}

size_t tap_sr_rational_process(tap_sr_rational_converter* c, const float* in, size_t in_frames, float* out) {
    return c->e->process(in, in_frames, out);
}

size_t tap_sr_rational_flush(tap_sr_rational_converter* c, float* out) {
    return c->e->flush(out);
}

int tap_sr_rational_format(const tap_sr_rational_converter* c) {
    return c->e->format();
}

size_t tap_sr_rational_process_q15(tap_sr_rational_converter* c, const int16_t* in, size_t in_frames, int16_t* out) {
    return c->e->process(in, in_frames, out);
}

size_t tap_sr_rational_process_q31(tap_sr_rational_converter* c, const int32_t* in, size_t in_frames, int32_t* out) {
    return c->e->process(in, in_frames, out);
}

size_t tap_sr_rational_flush_q15(tap_sr_rational_converter* c, int16_t* out) {
    return c->e->flush(out);
}

size_t tap_sr_rational_flush_q31(tap_sr_rational_converter* c, int32_t* out) {
    return c->e->flush(out);
}

uint64_t tap_sr_rational_flush_output_frames(const tap_sr_rational_converter* c) {
    return c->e->flush_output_frames();
}

void tap_sr_rational_reset(tap_sr_rational_converter* c) {
    c->e->reset();
}

void tap_sr_rational_latency_output_frames(const tap_sr_rational_converter* c, uint64_t* num, uint64_t* den) {
    if (!c->e->exact_latency()) {
        // No exact latency (a recursive stage or cascade, the Chebyshev II):
        // refused as 0 / 0, a zero denominator.
        if (num != nullptr) {
            *num = 0;
        }
        if (den != nullptr) {
            *den = 0;
        }
        return;
    }
    put(c->e->latency_output_frames(), num, den);
}

double tap_sr_rational_latency_seconds(const tap_sr_rational_converter* c, double out_rate_hz) {
    if (!c->e->exact_latency()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return c->e->latency_output_frames().value() / out_rate_hz;
}

double tap_sr_rational_nominal_latency_output_frames(const tap_sr_rational_converter* c) {
    return c->e->nominal_latency_output_frames();
}

double tap_sr_rational_group_delay_output_frames(const tap_sr_rational_converter* c, double f) {
    return c->e->group_delay_output_frames(f);
}

void tap_sr_rational_macs_per_output(const tap_sr_rational_converter* c, uint64_t* num, uint64_t* den) {
    put(c->e->macs_per_output(), num, den);
}

size_t tap_sr_rational_stages(const tap_sr_rational_converter* c) {
    return c->e->stages();
}

size_t tap_sr_rational_stage_taps(const tap_sr_rational_converter* c, size_t i) {
    return c->e->stage_taps(i);
}

unsigned tap_sr_rational_version(void) {
    return (TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH;
}

} // extern "C"
