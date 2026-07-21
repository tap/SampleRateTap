// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
// ANCHOR: kai_design_note
/// \file kaiser.h
/// \brief Kaiser-window FIR prototype design for the polyphase interpolation bank.
///
/// Design note — runtime vs constexpr: the prototype tables run 12K-33K taps and
/// each tap needs sin/sqrt plus a ~50-term Bessel I0 series. Constexpr evaluation
/// is interpreted (roughly 1e3-1e4x slower than native), would need hand-rolled
/// constexpr transcendentals before C++26, and would cost tens of seconds to
/// minutes of compile time in every including translation unit. Runtime design
/// takes well under 10 ms, runs once in a constructor, and is off the audio path,
/// so all design math here is plain runtime double precision.
// ANCHOR_END: kai_design_note
#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

namespace tap::samplerate::detail {

    // ANCHOR: kai_besseli0
    /// Modified Bessel function of the first kind, order zero, by power series.
    /// Converges for all practical Kaiser betas (|x| < ~40); terms are added until
    /// they no longer contribute at double precision.
    inline double bessel_i0(double x) noexcept {
        const double half_x = 0.5 * x;
        double       term   = 1.0;
        double       sum    = 1.0;
        for (int k = 1; k < 1000; ++k) {
            const double r = half_x / static_cast<double>(k);
            term *= r * r;
            sum += term;
            if (term < 1e-21 * sum) {
                break;
            }
        }
        return sum;
    }
    // ANCHOR_END: kai_besseli0

    // ANCHOR: kai_beta
    /// Kaiser window shape parameter for a given stopband attenuation in dB
    /// (Kaiser's published empirical fit).
    inline double kaiser_beta(double atten_db) noexcept {
        if (atten_db > 50.0) {
            return 0.1102 * (atten_db - 8.7);
        }
        if (atten_db > 21.0) {
            return 0.5842 * std::pow(atten_db - 21.0, 0.4) + 0.07886 * (atten_db - 21.0);
        }
        return 0.0;
    }
    // ANCHOR_END: kai_beta

    // ANCHOR: kai_estimate
    /// Kaiser/harris FIR length estimate, expressed per polyphase branch.
    ///
    /// \param attenDb        target stopband attenuation in dB
    /// \param transWidthNorm transition width normalized to the *input* sample rate
    ///                       (e.g. 8 kHz transition at 48 kHz -> 8000/48000)
    /// \return estimated taps per polyphase phase: N = (A - 8) / (2.285 * 2*pi * df)
    inline std::size_t estimate_taps(double atten_db, double trans_width_norm) noexcept {
        // Clamp pathological inputs (attenDb < 8, non-positive width): the raw
        // formula goes negative/infinite there and casting that to size_t is UB.
        if (!(trans_width_norm > 0.0)) {
            return 4;
        }
        const double n = (atten_db - 8.0) / (2.285 * 2.0 * std::numbers::pi * trans_width_norm);
        return n > 4.0 ? static_cast<std::size_t>(std::ceil(n)) : 4;
    }
    // ANCHOR_END: kai_estimate

    // ANCHOR: kai_sinc
    /// sin(pi x)/(pi x) with the removable singularity handled.
    inline double sinc(double x) noexcept {
        if (std::abs(x) < 1e-12) {
            return 1.0;
        }
        const double px = std::numbers::pi * x;
        return std::sin(px) / px;
    }
    // ANCHOR_END: kai_sinc

    // ANCHOR: kai_prototype
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
    inline void design_prototype(std::span<double> h, std::size_t num_phases, double cutoff_norm,
                                 double beta) noexcept {
        const std::size_t n       = h.size();
        const double      center  = 0.5 * static_cast<double>(n - 1);
        const double      i0_beta = bessel_i0(beta);
        double            sum     = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double t = (static_cast<double>(i) - center) / static_cast<double>(num_phases);
            const double u = (static_cast<double>(i) - center) / center; // window argument, [-1, 1]
            const double w = bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0_beta;
            h[i]           = cutoff_norm * sinc(cutoff_norm * t) * w;
            sum += h[i];
        }
        const double gain = static_cast<double>(num_phases) / sum;
        for (auto& v : h) {
            v *= gain;
        }
    }
    // ANCHOR_END: kai_prototype

    /// Solves the dense n x n system m * out = rhs in place (Gaussian elimination
    /// with partial pivoting; row-major m). Small systems only — the compensated
    /// design below solves at most 15 unknowns.
    inline void solve_dense(std::span<double> m, std::span<double> rhs, std::span<double> out, std::size_t n) noexcept {
        std::vector<std::size_t> order(n);
        for (std::size_t i = 0; i < n; ++i) {
            order[i] = i;
        }
        for (std::size_t col = 0; col < n; ++col) {
            std::size_t piv = col;
            for (std::size_t r = col + 1; r < n; ++r) {
                if (std::abs(m[order[r] * n + col]) > std::abs(m[order[piv] * n + col])) {
                    piv = r;
                }
            }
            std::swap(order[col], order[piv]);
            const std::size_t p = order[col];
            for (std::size_t r = col + 1; r < n; ++r) {
                const std::size_t rr = order[r];
                const double      f  = m[rr * n + col] / m[p * n + col];
                for (std::size_t q = col; q < n; ++q) {
                    m[rr * n + q] -= f * m[p * n + q];
                }
                rhs[rr] -= f * rhs[p];
            }
        }
        for (std::size_t col = n; col-- > 0;) {
            const std::size_t p = order[col];
            double            v = rhs[p];
            for (std::size_t q = col + 1; q < n; ++q) {
                v -= m[p * n + q] * out[q];
            }
            out[col] = v / m[p * n + col];
        }
    }

    // ANCHOR: pw_comp_design
    /// Designs a prototype with transmission zeros at every integer multiple of
    /// the sample rate, passband droop pre-compensated (the music-dsp thread's
    /// suggestion, done inside the passband spec — see the epilogue chapter of
    /// the book and notebooks/asrc_rbj_analysis.ipynb).
    ///
    /// Construction: the zeros come from convolving with a one-input-sample rect
    /// (multiplies the response by sinc(f/fs), zero at every k*fs — exactly where
    /// the images of low-frequency program energy sit). The rect's passband droop
    /// (-2.64 dB at 20 kHz) is cancelled by tilting the design target by
    /// 1/sinc(f/fs), expressed as a short cosine series in f — which in time is a
    /// weighted sum of the brickwall kernel at small integer shifts, so the whole
    /// design stays closed-form: no FFT, no dependency. One correction pass
    /// (measure the built passband by direct DFT, fold the deviation back into
    /// the tilt) holds every preset's ripple within +/-0.005 dB, a >=2x margin
    /// on the spec. (A second pass buys ~1 dB of margin for another kernel
    /// build plus probe sweep — real money on soft-double targets, where every
    /// design flop is a libcall; the pass count and probe count below are
    /// sized by the M33 instruction-count ledger in docs/PERFORMANCE.md.)
    ///
    /// \param h            output, length L*T for the TOTAL taps per phase T; the
    ///                     sinc design uses T-1 taps and the rect supplies the
    ///                     +1 (composite length L*(T-1)+L-1, one zero of padding)
    /// \param numPhases    L
    /// \param cutoffNorm   as designPrototype
    /// \param beta         Kaiser shape parameter for the T-1-tap base design
    /// \param passbandNorm passband edge / sample rate (flatness is corrected and
    ///                     verified up to here)
    ///
    /// Costs a few ms more than designPrototype (three kernel builds plus ~100
    /// direct-DFT probes); still constructor-only, off the audio path. Allocates
    /// workspace; may throw std::bad_alloc.
    // ANCHOR_END: pw_comp_design
    inline void design_prototype_compensated(std::span<double> h, std::size_t num_phases, double cutoff_norm,
                                             double beta, double passband_norm) {
        const std::size_t L     = num_phases;
        const std::size_t total = h.size() / L; // total taps per phase (with rect)
        const std::size_t td    = total - 1;    // sinc-design taps per phase
        const std::size_t n     = L * td;       // fine-grid design length
        const std::size_t nc    = n + L - 1;    // composite length after rect
        // Compensator order: enough cosine terms to hold the passband tilt to
        // ~1e-4, capped so the shifted kernels stay well inside short windows.
        const std::size_t M = std::min<std::size_t>(14, (td - 1) / 5);

        constexpr std::size_t k_grid  = 1001; // fit grid over f/fs in [0, 0.5]
        constexpr std::size_t k_probe = 24;   // passband correction probes
        std::vector<double>   target(k_grid), a(M + 1), fine(n), probe(k_probe);
        for (std::size_t g = 0; g < k_grid; ++g) {
            const double f  = 0.5 * static_cast<double>(g) / static_cast<double>(k_grid - 1);
            const double pf = std::numbers::pi * f;
            target[g]       = f < 1e-9 ? 1.0 : pf / std::sin(pf); // 1/sinc(f/fs)
        }

        const auto fit_cosine_series = [&] {
            // Weighted LS of target on cos(2*pi*m*f): exact where flatness is
            // specified (the passband, heavy weight), merely tracked above it.
            // Basis by Chebyshev recurrence: cos(m x) from the two previous
            // orders, one real cosine per grid point.
            std::vector<double> nm((M + 1) * (M + 1), 0.0), rhs(M + 1, 0.0), basis(M + 1);
            for (std::size_t g = 0; g < k_grid; ++g) {
                const double f  = 0.5 * static_cast<double>(g) / static_cast<double>(k_grid - 1);
                const double w2 = f <= passband_norm + 0.02 ? 1e8 : 1.0; // (weight 1e4)^2
                const double c1 = std::cos(2.0 * std::numbers::pi * f);
                basis[0]        = 1.0;
                if (M >= 1) {
                    basis[1] = c1;
                }
                for (std::size_t m = 2; m <= M; ++m) {
                    basis[m] = 2.0 * c1 * basis[m - 1] - basis[m - 2];
                }
                for (std::size_t r = 0; r <= M; ++r) {
                    for (std::size_t q = 0; q <= M; ++q) {
                        nm[r * (M + 1) + q] += w2 * basis[r] * basis[q];
                    }
                    rhs[r] += w2 * basis[r] * target[g];
                }
            }
            solve_dense(nm, rhs, a, M + 1);
        };

        const auto build = [&] {
            // Tilted ideal kernel: sum of the brickwall sinc at integer shifts.
            // Centered at n/2 (not (n-1)/2): the even-length rect below shifts
            // the composite by (L-1)/2, and n/2 + (L-1)/2 == (L*total - 1)/2 —
            // the exact center designPrototype uses, so the bank's phase/delay
            // convention is identical for both designs. (Getting this wrong is a
            // half-fine-sample delay error: ~-72 dB at 1 kHz, worse by 6 dB per
            // octave — the fractional-delay accuracy tests catch it.)
            //
            // Transcendental budget: the naive form of this loop calls libm sin
            // once per tap per compensator term (~2M calls across the design; a
            // measured +225M constructor instructions on Cortex-M55, and worse
            // where doubles are soft). Instead: sin(pi*c*(t -+ m)) expands by
            // angle addition over precomputed sin/cos(pi*c*m), and sin/cos of
            // the per-tap angle advance by a unit rotator, re-synced with real
            // libm calls every 4096 taps to bound drift far below the design's
            // own accuracy floor.
            const double        center  = 0.5 * static_cast<double>(n);
            const double        i0_beta = bessel_i0(beta);
            std::vector<double> cs(M + 1), sn(M + 1);
            for (std::size_t m = 0; m <= M; ++m) {
                cs[m] = std::cos(std::numbers::pi * cutoff_norm * static_cast<double>(m));
                sn[m] = std::sin(std::numbers::pi * cutoff_norm * static_cast<double>(m));
            }
            const double step   = std::numbers::pi * cutoff_norm / static_cast<double>(L);
            const double step_c = std::cos(step), step_s = std::sin(step);
            double       ang_s = 0.0, ang_c = 1.0; // sin/cos(pi*c*t_i), re-synced below
            for (std::size_t i = 0; i < n; ++i) {
                const double t = (static_cast<double>(i) - center) / static_cast<double>(L);
                if (i % 4096 == 0) {
                    ang_s = std::sin(std::numbers::pi * cutoff_norm * t);
                    ang_c = std::cos(std::numbers::pi * cutoff_norm * t);
                }
                const auto shifted_sinc = [&](double dm, double sin_shift, double cos_shift) {
                    const double x = cutoff_norm * (t - dm); // dm may be negative
                    if (std::abs(x) < 1e-12) {
                        return 1.0;
                    }
                    // sin(pi*c*(t - dm)) = sin(pi*c*t)cos(pi*c*dm) - cos(..)sin(..)
                    return (ang_s * cos_shift - ang_c * sin_shift) / (std::numbers::pi * x);
                };
                double v = a[0] * cutoff_norm * shifted_sinc(0.0, 0.0, 1.0);
                for (std::size_t m = 1; m <= M; ++m) {
                    const double dm = static_cast<double>(m);
                    v += 0.5 * a[m] * cutoff_norm * (shifted_sinc(dm, sn[m], cs[m]) + shifted_sinc(-dm, -sn[m], cs[m]));
                }
                const double u      = (static_cast<double>(i) - center) / center;
                fine[i]             = v * bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0_beta;
                const double next_s = ang_s * step_c + ang_c * step_s;
                ang_c               = ang_c * step_c - ang_s * step_s;
                ang_s               = next_s;
            }
            // ANCHOR: pw_comp_rect
            // Rect convolution as a running sum: exact zeros at every k*fs.
            double run = 0.0;
            for (std::size_t i = 0; i < nc; ++i) {
                run += i < n ? fine[i] : 0.0;
                if (i >= L) {
                    run -= fine[i - L];
                }
                h[i] = run / static_cast<double>(L);
            }
            for (std::size_t i = nc; i < h.size(); ++i) {
                h[i] = 0.0;
            }
            // ANCHOR_END: pw_comp_rect
            double sum = 0.0;
            for (std::size_t i = 0; i < nc; ++i) {
                sum += h[i];
            }
            const double gain = static_cast<double>(L) / sum;
            for (std::size_t i = 0; i < nc; ++i) {
                h[i] *= gain;
            }
        };

        for (int pass = 0; pass < 1; ++pass) {
            fit_cosine_series();
            build();
            // Probe the built passband by direct DFT (cos projection about the
            // composite's symmetry center, (L*total - 1)/2 == nc/2) and fold the
            // deviation into the tilt. One rotator per probe frequency: two libm
            // calls each instead of one per tap (rotator drift over ~2^14 steps
            // is ~1e-12, five orders below the ripple being measured).
            const double center = 0.5 * static_cast<double>(nc);
            for (std::size_t j = 0; j < k_probe; ++j) {
                const double f    = passband_norm * static_cast<double>(j + 1) / k_probe;
                const double th   = 2.0 * std::numbers::pi * f / static_cast<double>(L);
                const double th_c = std::cos(th), th_s = std::sin(th);
                double       rc = std::cos(th * -center), rs = std::sin(th * -center);
                double       acc = 0.0;
                for (std::size_t i = 0; i < nc; ++i) {
                    acc += h[i] * rc;
                    const double nrc = rc * th_c - rs * th_s;
                    rs               = rs * th_c + rc * th_s;
                    rc               = nrc;
                }
                probe[j] = std::abs(acc) / static_cast<double>(L);
            }
            for (std::size_t g = 0; g < k_grid; ++g) {
                const double f = 0.5 * static_cast<double>(g) / static_cast<double>(k_grid - 1);
                if (f > passband_norm) {
                    continue;
                }
                // probe[j] sits at f = passbandNorm*(j+1)/kProbe, i.e. x = j+1
                const double x = f / passband_norm * k_probe - 1.0;
                double       d;
                if (x <= 0.0) {
                    d = probe[0];
                }
                else if (x >= static_cast<double>(k_probe - 1)) {
                    d = probe[k_probe - 1];
                }
                else {
                    const auto   j  = static_cast<std::size_t>(x);
                    const double fr = x - static_cast<double>(j);
                    d               = probe[j] * (1.0 - fr) + probe[j + 1] * fr;
                }
                target[g] /= std::max(d, 0.5);
            }
        }
        fit_cosine_series();
        build();
    }

} // namespace tap::samplerate::detail
