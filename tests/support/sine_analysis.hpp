// Least-squares sine fitting for THD+N-style quality measurements.
#ifndef SRT_TESTS_SINE_ANALYSIS_HPP
#define SRT_TESTS_SINE_ANALYSIS_HPP

#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

namespace srt_test {

    struct sine_fit {
        double amplitude    = 0.0;
        double phase        = 0.0;
        double dc           = 0.0;
        double residual_rms = 0.0;
        double freq_norm    = 0.0;
    };

    /// Fits x[i] ~ a*sin(w i) + b*cos(w i) + c by least squares (3x3 normal
    /// equations) at the known normalized frequency, then measures the residual.
    inline sine_fit fit_sine(std::span<const float> x, double freq_norm) {
        const double w       = 2.0 * std::numbers::pi * freq_norm;
        double       m[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        double       rhs[3]  = {0, 0, 0};
        for (std::size_t i = 0; i < x.size(); ++i) {
            const double s        = std::sin(w * static_cast<double>(i));
            const double c        = std::cos(w * static_cast<double>(i));
            const double basis[3] = {s, c, 1.0};
            for (int r = 0; r < 3; ++r) {
                for (int q = 0; q < 3; ++q)
                    m[r][q] += basis[r] * basis[q];
                rhs[r] += basis[r] * static_cast<double>(x[i]);
            }
        }
        // Gaussian elimination with partial pivoting.
        int order[3] = {0, 1, 2};
        for (int col = 0; col < 3; ++col) {
            int piv = col;
            for (int r = col + 1; r < 3; ++r)
                if (std::abs(m[order[r]][col]) > std::abs(m[order[piv]][col]))
                    piv = r;
            std::swap(order[col], order[piv]);
            const int p = order[col];
            for (int r = col + 1; r < 3; ++r) {
                const int    rr = order[r];
                const double f  = m[rr][col] / m[p][col];
                for (int q = col; q < 3; ++q)
                    m[rr][q] -= f * m[p][q];
                rhs[rr] -= f * rhs[p];
            }
        }
        double sol[3];
        for (int col = 2; col >= 0; --col) {
            const int p = order[col];
            double    v = rhs[p];
            for (int q = col + 1; q < 3; ++q)
                v -= m[p][q] * sol[q];
            sol[col] = v / m[p][col];
        }
        sine_fit fit;
        fit.amplitude = std::hypot(sol[0], sol[1]);
        fit.phase     = std::atan2(sol[1], sol[0]);
        fit.dc        = sol[2];
        double sq     = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) {
            const double s = std::sin(w * static_cast<double>(i));
            const double c = std::cos(w * static_cast<double>(i));
            const double r = static_cast<double>(x[i]) - (sol[0] * s + sol[1] * c + sol[2]);
            sq += r * r;
        }
        fit.residual_rms = std::sqrt(sq / static_cast<double>(x.size()));
        fit.freq_norm    = freq_norm;
        return fit;
    }

    /// Like fitSine, but refines the frequency first (a few iterations comparing
    /// the fitted phase of the two window halves). An ASRC's rate estimate
    /// converges asymptotically, so the tail of a run can sit a fraction of a ppm
    /// off the nominal ratio; a rigid fixed-frequency fit would book that
    /// (inaudible) offset as residual. Tracking the fundamental is standard
    /// THD-analyzer practice.
    inline sine_fit fit_sine_tracked(std::span<const float> x, double freq_norm_guess) {
        double            f    = freq_norm_guess;
        const std::size_t half = x.size() / 2;
        for (int iter = 0; iter < 4; ++iter) {
            const sine_fit a = fit_sine(x.first(half), f);
            const sine_fit b = fit_sine(x.subspan(half), f);
            // b.phase is relative to the second half's start; predict it from a.
            const double two_pi    = 2.0 * std::numbers::pi;
            const double predicted = a.phase + two_pi * f * static_cast<double>(half);
            const double dphi      = std::remainder(b.phase - predicted, two_pi);
            f += dphi / (two_pi * static_cast<double>(half));
        }
        return fit_sine(x, f);
    }

    /// Signal-to-(residual) ratio in dB for a fitted sine.
    inline double snr_db(const sine_fit& f) {
        return 10.0 * std::log10((f.amplitude * f.amplitude * 0.5) / (f.residual_rms * f.residual_rms));
    }

} // namespace srt_test

#endif // SRT_TESTS_SINE_ANALYSIS_HPP
