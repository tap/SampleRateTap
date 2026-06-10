/// \file kaiser.hpp
/// \brief Kaiser-window FIR prototype design for the polyphase interpolation bank.
///
/// Design note — runtime vs constexpr: the prototype tables run 12K-33K taps and
/// each tap needs sin/sqrt plus a ~50-term Bessel I0 series. Constexpr evaluation
/// is interpreted (roughly 1e3-1e4x slower than native), would need hand-rolled
/// constexpr transcendentals before C++26, and would cost tens of seconds to
/// minutes of compile time in every including translation unit. Runtime design
/// takes well under 10 ms, runs once in a constructor, and is off the audio path,
/// so all design math here is plain runtime double precision.
#ifndef SRT_DETAIL_KAISER_HPP
#define SRT_DETAIL_KAISER_HPP

#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

namespace srt::detail {

/// Modified Bessel function of the first kind, order zero, by power series.
/// Converges for all practical Kaiser betas (|x| < ~40); terms are added until
/// they no longer contribute at double precision.
inline double besselI0(double x) noexcept {
    const double halfX = 0.5 * x;
    double term = 1.0;
    double sum = 1.0;
    for (int k = 1; k < 1000; ++k) {
        const double r = halfX / static_cast<double>(k);
        term *= r * r;
        sum += term;
        if (term < 1e-21 * sum)
            break;
    }
    return sum;
}

/// Kaiser window shape parameter for a given stopband attenuation in dB
/// (Kaiser's published empirical fit).
inline double kaiserBeta(double attenDb) noexcept {
    if (attenDb > 50.0)
        return 0.1102 * (attenDb - 8.7);
    if (attenDb > 21.0)
        return 0.5842 * std::pow(attenDb - 21.0, 0.4) + 0.07886 * (attenDb - 21.0);
    return 0.0;
}

/// Kaiser/harris FIR length estimate, expressed per polyphase branch.
///
/// \param attenDb        target stopband attenuation in dB
/// \param transWidthNorm transition width normalized to the *input* sample rate
///                       (e.g. 8 kHz transition at 48 kHz -> 8000/48000)
/// \return estimated taps per polyphase phase: N = (A - 8) / (2.285 * 2*pi * df)
inline std::size_t estimateTaps(double attenDb, double transWidthNorm) noexcept {
    const double n = (attenDb - 8.0) / (2.285 * 2.0 * std::numbers::pi * transWidthNorm);
    return static_cast<std::size_t>(std::ceil(n));
}

/// sin(pi x)/(pi x) with the removable singularity handled.
inline double sinc(double x) noexcept {
    if (std::abs(x) < 1e-12)
        return 1.0;
    const double px = std::numbers::pi * x;
    return std::sin(px) / px;
}

/// Designs the Kaiser-windowed sinc prototype lowpass for an L-phase
/// interpolation bank.
///
/// \param h          output, length L*T (the full oversampled prototype)
/// \param numPhases  L; the prototype is sampled on a grid of 1/L input samples
/// \param cutoffNorm cutoff normalized to the input Nyquist, i.e. 2*fc/fs in
///                   (0, 1]; for a near-unity interpolator centered between a
///                   20 kHz passband and 28 kHz stopband at 48 kHz this is
///                   (20000+28000)/48000 = 1.0 (cutoff at input Nyquist)
/// \param beta       Kaiser shape parameter (see kaiserBeta)
///
/// The result is normalized so that sum(h) == L, giving each polyphase branch a
/// DC gain of ~1 (deviation bounded by the stopband leakage).
inline void designPrototype(std::span<double> h, std::size_t numPhases, double cutoffNorm,
                            double beta) noexcept {
    const std::size_t n = h.size();
    const double center = 0.5 * static_cast<double>(n - 1);
    const double i0Beta = besselI0(beta);
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = (static_cast<double>(i) - center) / static_cast<double>(numPhases);
        const double u = (static_cast<double>(i) - center) / center; // window argument, [-1, 1]
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0Beta;
        h[i] = cutoffNorm * sinc(cutoffNorm * t) * w;
        sum += h[i];
    }
    const double gain = static_cast<double>(numPhases) / sum;
    for (auto& v : h)
        v *= gain;
}

} // namespace srt::detail

#endif // SRT_DETAIL_KAISER_HPP
