/// @file tap_sr_rational_capi.cpp
/// @brief C ABI implementation: one interface over the named chains and the single stages, in float, Q15 and Q31.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors

#include "tap_sr_rational_capi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

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

    rat::profile profile_for(int p) {
        return p == 0   ? rat::profile::economy()
               : p == 1 ? rat::profile::transparent()
               : p == 2 ? rat::profile::balanced()
                        : rat::profile::super_economy();
    }

    template <typename S, rat::rational_ratio... Rs>
    std::unique_ptr<engine> make_chain(const rat::profile& p, unsigned channels) {
        using chain = rat::basic_chain<S, Rs...>;
        return std::make_unique<chain_engine<chain>>(chain(channels, p));
    }

    template <typename S>
    std::unique_ptr<engine> make_named(int chain, const rat::profile& p, unsigned ch) {
        using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
        switch (chain) {
        case TAP_SR_RATIONAL_UP_2:
            return make_chain<S, up_2>(p, ch);
        case TAP_SR_RATIONAL_DOWN_2:
            return make_chain<S, down_2>(p, ch);
        case TAP_SR_RATIONAL_UP_3:
            return make_chain<S, up_3>(p, ch);
        case TAP_SR_RATIONAL_DOWN_3:
            return make_chain<S, down_3>(p, ch);
        case TAP_SR_RATIONAL_RATIO_3_2:
            return make_chain<S, ratio_3_2>(p, ch);
        case TAP_SR_RATIONAL_RATIO_2_3:
            return make_chain<S, ratio_2_3>(p, ch);
        case TAP_SR_RATIONAL_RATIO_4_3:
            return make_chain<S, ratio_4_3>(p, ch);
        case TAP_SR_RATIONAL_RATIO_3_4:
            return make_chain<S, ratio_3_4>(p, ch);
        case TAP_SR_RATIONAL_UP_2_UP_2:
            return make_chain<S, up_2, up_2>(p, ch);
        case TAP_SR_RATIONAL_UP_2_UP_3:
            return make_chain<S, up_2, up_3>(p, ch);
        case TAP_SR_RATIONAL_UP_2_RATIO_4_3_UP_3:
            return make_chain<S, up_2, ratio_4_3, up_3>(p, ch);
        case TAP_SR_RATIONAL_UP_3_RATIO_8_3:
            return make_chain<S, up_3, ratio_8_3>(p, ch);
        case TAP_SR_RATIONAL_UP_2_UP_6:
            return make_chain<S, up_2, up_6>(p, ch);
        case TAP_SR_RATIONAL_UP_2_UP_8:
            return make_chain<S, up_2, up_8>(p, ch);
        case TAP_SR_RATIONAL_UP_2_UP_6_UP_2:
            return make_chain<S, up_2, up_6, up_2>(p, ch);
        case TAP_SR_RATIONAL_UP_2_UP_8_UP_2:
            return make_chain<S, up_2, up_8, up_2>(p, ch);
        case TAP_SR_RATIONAL_UP_2_UP_8_UP_3:
            return make_chain<S, up_2, up_8, up_3>(p, ch);
        case TAP_SR_RATIONAL_UP_2_RATIO_4_3:
            return make_chain<S, up_2, ratio_4_3>(p, ch);
        case TAP_SR_RATIONAL_RATIO_3_4_DOWN_2:
            return make_chain<S, ratio_3_4, down_2>(p, ch);
        case TAP_SR_RATIONAL_DOWN_2_DOWN_2:
            return make_chain<S, down_2, down_2>(p, ch);
        case TAP_SR_RATIONAL_DOWN_3_DOWN_2:
            return make_chain<S, down_3, down_2>(p, ch);
        case TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2:
            return make_chain<S, down_3, ratio_3_4, down_2>(p, ch);
        case TAP_SR_RATIONAL_RATIO_3_8_DOWN_3:
            return make_chain<S, ratio_3_8, down_3>(p, ch);
        case TAP_SR_RATIONAL_DOWN_6_DOWN_2:
            return make_chain<S, down_6, down_2>(p, ch);
        case TAP_SR_RATIONAL_DOWN_8_DOWN_2:
            return make_chain<S, down_8, down_2>(p, ch);
        case TAP_SR_RATIONAL_DOWN_2_DOWN_6_DOWN_2:
            return make_chain<S, down_2, down_6, down_2>(p, ch);
        case TAP_SR_RATIONAL_DOWN_2_DOWN_8_DOWN_2:
            return make_chain<S, down_2, down_8, down_2>(p, ch);
        case TAP_SR_RATIONAL_DOWN_3_DOWN_8_DOWN_2:
            return make_chain<S, down_3, down_8, down_2>(p, ch);
        default:
            return nullptr;
        }
    }

    template <typename S, rat::rational_ratio R>
    std::unique_ptr<engine> make_stage(const rat::profile& p, exact_ratio divisor, unsigned ch) {
        using chain = tap::dsp::chain<rat::basic_stage<S, R>>;
        return std::make_unique<chain_engine<chain>>(chain(ch, rat::basic_stage<S, R>(ch, p.relaxed(divisor))));
    }

    template <typename S>
    std::unique_ptr<engine> make_single(unsigned l, unsigned m, const rat::profile& p, exact_ratio d, unsigned ch) {
        using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
        const auto key = (static_cast<unsigned>(l) << 8) | m;
        switch (key) {
        case (2u << 8) | 1u:
            return make_stage<S, up_2>(p, d, ch);
        case (1u << 8) | 2u:
            return make_stage<S, down_2>(p, d, ch);
        case (3u << 8) | 1u:
            return make_stage<S, up_3>(p, d, ch);
        case (1u << 8) | 3u:
            return make_stage<S, down_3>(p, d, ch);
        case (6u << 8) | 1u:
            return make_stage<S, up_6>(p, d, ch);
        case (1u << 8) | 6u:
            return make_stage<S, down_6>(p, d, ch);
        case (8u << 8) | 1u:
            return make_stage<S, up_8>(p, d, ch);
        case (1u << 8) | 8u:
            return make_stage<S, down_8>(p, d, ch);
        case (3u << 8) | 2u:
            return make_stage<S, ratio_3_2>(p, d, ch);
        case (2u << 8) | 3u:
            return make_stage<S, ratio_2_3>(p, d, ch);
        case (4u << 8) | 3u:
            return make_stage<S, ratio_4_3>(p, d, ch);
        case (3u << 8) | 4u:
            return make_stage<S, ratio_3_4>(p, d, ch);
        case (8u << 8) | 3u:
            return make_stage<S, ratio_8_3>(p, d, ch);
        case (3u << 8) | 8u:
            return make_stage<S, ratio_3_8>(p, d, ch);
        default:
            return nullptr;
        }
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
    if (chain < 0 || chain >= TAP_SR_RATIONAL_CHAIN_COUNT || profile < 0 || profile > 3 || channels == 0) {
        return nullptr;
    }
    try {
        auto c = std::make_unique<tap_sr_rational_converter>();
        c->e   = by_format(format,
                           [&](auto tag) { return make_named<decltype(tag)>(chain, profile_for(profile), channels); });
        return c->e ? c.release() : nullptr;
    }
    catch (...) {
        return nullptr;
    }
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
    try {
        auto c = std::make_unique<tap_sr_rational_converter>();
        c->e   = by_format(format, [&](auto tag) {
            return make_single<decltype(tag)>(L, M, profile_for(profile), exact_ratio{divisor_num, divisor_den},
                                                channels);
        });
        return c->e ? c.release() : nullptr;
    }
    catch (...) {
        return nullptr;
    }
}

tap_sr_rational_converter* tap_sr_rational_create_stage(unsigned L, unsigned M, int profile, uint32_t divisor_num,
                                                        uint32_t divisor_den, unsigned channels) {
    return tap_sr_rational_create_stage_format(L, M, profile, divisor_num, divisor_den, TAP_SR_RATIONAL_FORMAT_FLOAT,
                                               channels);
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
    put(c->e->latency_output_frames(), num, den);
}

double tap_sr_rational_latency_seconds(const tap_sr_rational_converter* c, double out_rate_hz) {
    return c->e->latency_output_frames().value() / out_rate_hz;
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
