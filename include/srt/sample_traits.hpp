/// \file sample_traits.hpp
/// \brief Sample-type customization point for the resampling datapath.
///
/// The datapath (PolyphaseFilterBank, the interpolation kernel,
/// FractionalResampler, BasicAsyncSampleRateConverter) is templated on the
/// sample type through SampleTraits<T>. v0.1 ships only the float
/// specialization (float I/O, float coefficients, double accumulation).
/// Fixed-point types (Q15/Q31) can be added later by specializing
/// SampleTraits with integer accumulators and a saturating finalize() —
/// no public API changes required. The clock servo and the filter design
/// always run in double regardless of sample type (control path and
/// one-time init, not the audio path).
#ifndef SRT_SAMPLE_TRAITS_HPP
#define SRT_SAMPLE_TRAITS_HPP

#include <concepts>

namespace srt {

/// Primary template intentionally undefined; specialize per sample type.
template <typename T>
struct SampleTraits;

/// Float datapath: float samples and coefficients, double accumulation.
/// The double accumulator keeps the dot-product noise floor far below the
/// 120 dB transparency target; float coefficient storage quantizes the
/// filter at roughly -150 dB, negligible against the same target.
template <>
struct SampleTraits<float> {
    using Coeff = float;  ///< stored filter coefficient type
    using Accum = double; ///< dot-product accumulator type

    /// Convert a double-precision designed coefficient to storage form.
    static Coeff makeCoeff(double c) noexcept { return static_cast<Coeff>(c); }

    /// acc + x * c, in the accumulator domain.
    static Accum mac(Accum acc, float x, Coeff c) noexcept {
        return acc + static_cast<double>(x) * static_cast<double>(c);
    }

    /// Linear blend between two adjacent-phase coefficients, fr in [0, 1).
    static Coeff blend(Coeff a, Coeff b, double fr) noexcept {
        return a + static_cast<float>(fr) * (b - a);
    }

    /// Convert the accumulator to an output sample (saturates for fixed point).
    static float finalize(Accum acc) noexcept { return static_cast<float>(acc); }

    /// The zero/silence sample value.
    static float silence() noexcept { return 0.0f; }
};

/// Satisfied by any type with a complete, well-formed SampleTraits
/// specialization.
template <typename T>
concept SampleType =
    requires(T x, double d, typename SampleTraits<T>::Accum a, typename SampleTraits<T>::Coeff c) {
        { SampleTraits<T>::makeCoeff(d) } -> std::same_as<typename SampleTraits<T>::Coeff>;
        { SampleTraits<T>::mac(a, x, c) } -> std::same_as<typename SampleTraits<T>::Accum>;
        { SampleTraits<T>::blend(c, c, d) } -> std::same_as<typename SampleTraits<T>::Coeff>;
        { SampleTraits<T>::finalize(a) } -> std::same_as<T>;
        { SampleTraits<T>::silence() } -> std::same_as<T>;
    };

static_assert(SampleType<float>);

} // namespace srt

#endif // SRT_SAMPLE_TRAITS_HPP
