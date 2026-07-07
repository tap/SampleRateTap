// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
// Program-weighted multitone metric: a deterministic pink-weighted tone
// comb and the residual measurement that scores a converter against it.
//
// Why this exists: every other quality number in this suite is a single
// sine — the worst-case metric. A filter with transmission zeros at k*fs
// (filter_spec::imageZeros) is deliberately optimized for a different
// promise: alias rejection weighted by where real program energy lives
// (predominantly the bottom octaves). That promise is unverifiable with
// single sines, so this header supplies the instrument: K log-spaced tones
// with pink (equal-energy-per-octave) amplitudes, and a fit-subtract
// residual over the converted tail. See the book's epilogue chapter.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include "srt/detail/kaiser.h" // detail::solveDense for the joint LS
#include "support/sine_analysis.h"

namespace srt_test {

    // ANCHOR: pw_comb
    struct tone_comb {
        std::vector<double> freq_hz;   // log-spaced
        std::vector<double> amplitude; // pink: a ~ 1/sqrt(f), scaled to peakSum
        std::vector<double> phase;     // golden-angle sequence: bounded crest

        /// K tones from fLo to fHi; sum of amplitudes == peakSum, so the summed
        /// signal can never exceed peakSum even in the worst phase alignment.
        static tone_comb pink(std::size_t k, double f_lo, double f_hi, double peak_sum) {
            tone_comb c;
            double    sum = 0.0;
            for (std::size_t i = 0; i < k; ++i) {
                const double f = f_lo * std::pow(f_hi / f_lo, static_cast<double>(i) / static_cast<double>(k - 1));
                c.freq_hz.push_back(f);
                c.amplitude.push_back(1.0 / std::sqrt(f / f_lo));
                c.phase.push_back(2.0 * std::numbers::pi * 0.6180339887498949 * static_cast<double>(i * i));
                sum += c.amplitude.back();
            }
            for (auto& a : c.amplitude) {
                a *= peak_sum / sum;
            }
            return c;
        }

        /// Sample of the comb at input sample index i (rate fs).
        double sample_at(std::uint64_t i, double fs) const {
            double v = 0.0;
            for (std::size_t k = 0; k < freq_hz.size(); ++k) {
                v += amplitude[k]
                     * std::sin(2.0 * std::numbers::pi * freq_hz[k] / fs * static_cast<double>(i) + phase[k]);
            }
            return v;
        }
    };
    // ANCHOR_END: pw_comb

    /// Fits a*sin + b*cos at a fixed normalized frequency (no DC term; the
    /// joint fit models DC), returning the fitted component's power.
    struct tone_fit {
        double a = 0.0, b = 0.0;
        double power() const { return 0.5 * (a * a + b * b); }
    };

    inline tone_fit fit_tone_fixed(std::span<const double> x, double freq_norm) {
        const double w  = 2.0 * std::numbers::pi * freq_norm;
        double       ss = 0.0, sc = 0.0, cc = 0.0, rs = 0.0, rc = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) {
            const double s = std::sin(w * static_cast<double>(i));
            const double c = std::cos(w * static_cast<double>(i));
            ss += s * s;
            sc += s * c;
            cc += c * c;
            rs += s * x[i];
            rc += c * x[i];
        }
        const double det = ss * cc - sc * sc;
        tone_fit     f;
        f.a = (rs * cc - rc * sc) / det;
        f.b = (rc * ss - rs * sc) / det;
        return f;
    }

    /// Refines a tone's frequency by comparing the fitted phase of the two
    /// window halves (fitSineTracked's method, on the double work buffer and
    /// without a DC term), returning the refined normalized frequency.
    inline double track_tone_freq(std::span<const double> x, double freq_norm) {
        double            f    = freq_norm;
        const std::size_t half = x.size() / 2;
        for (int iter = 0; iter < 4; ++iter) {
            const tone_fit a         = fit_tone_fixed(x.first(half), f);
            const tone_fit b         = fit_tone_fixed(x.subspan(half), f);
            const double   two_pi    = 2.0 * std::numbers::pi;
            const double   predicted = std::atan2(a.b, a.a) + two_pi * f * static_cast<double>(half);
            const double   dphi      = std::remainder(std::atan2(b.b, b.a) - predicted, two_pi);
            f += dphi / (two_pi * static_cast<double>(half));
        }
        return f;
    }

    // ANCHOR: pw_metric
    /// Joint least-squares fit of all tones at once (2K unknowns via normal
    /// equations), writing per-tone fits and returning the residual out-of-model
    /// power. Sequential fit-subtract is NOT enough here: 24 tones on a
    /// rectangular window leak into each other far above the -120 dB floors
    /// being measured, and Gauss-Seidel over that coupling converges too slowly
    /// to be an instrument (measured: it floors near 48 dB on exact synthetic
    /// tones; the joint solve reaches the float quantization floor).
    inline double joint_fit_residual_power(std::span<const double> x, std::span<const double> nus,
                                           std::span<tone_fit> fits) {
        const std::size_t k  = nus.size();
        const std::size_t n2 = 2 * k + 1; // +1: a DC column. Subtracting the
        // sample mean beforehand is WRONG: a finite window of pure tones has a
        // legitimate nonzero mean (partial cycles of the low tones), and
        // removing it injects a constant the sine basis cannot absorb — a
        // measured -48 dB instrument floor. Modeled jointly, DC costs nothing.
        std::vector<double> ata(n2 * n2, 0.0), aty(n2, 0.0), basis(n2), sol(n2);
        for (std::size_t i = 0; i < x.size(); ++i) {
            for (std::size_t t = 0; t < k; ++t) {
                const double w   = 2.0 * std::numbers::pi * nus[t] * static_cast<double>(i);
                basis[2 * t]     = std::sin(w);
                basis[2 * t + 1] = std::cos(w);
            }
            basis[n2 - 1] = 1.0;
            for (std::size_t r = 0; r < n2; ++r) {
                for (std::size_t q = r; q < n2; ++q) {
                    ata[r * n2 + q] += basis[r] * basis[q];
                }
                aty[r] += basis[r] * x[i];
            }
        }
        for (std::size_t r = 0; r < n2; ++r) {
            for (std::size_t q = 0; q < r; ++q) {
                ata[r * n2 + q] = ata[q * n2 + r];
            }
        }
        srt::detail::solve_dense(ata, aty, sol, n2);
        for (std::size_t t = 0; t < k; ++t) {
            fits[t] = tone_fit{sol[2 * t], sol[2 * t + 1]};
        }
        double resid = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) {
            double model = sol[n2 - 1]; // DC: modeled out, in neither bucket
            for (std::size_t t = 0; t < k; ++t) {
                const double w = 2.0 * std::numbers::pi * nus[t] * static_cast<double>(i);
                model += sol[2 * t] * std::sin(w) + sol[2 * t + 1] * std::cos(w);
            }
            const double r = x[i] - model;
            resid += r * r;
        }
        return resid / static_cast<double>(x.size());
    }

    /// Program-weighted SNR: total fitted tone power over the power of what is
    /// left after subtracting every tone — aliases, servo FM, noise, all of it.
    ///
    /// The comb is generated at physical Hz, so the converted tones sit at
    /// freqHz/fsOut regardless of the clock offset. At these SNR levels a fit
    /// frequency must be exact to ~1e-9 relative, so the servo's sub-ppm
    /// settling residual is estimated from the data: joint-fit at the nominal
    /// ratio, re-track each tone against (residual + that tone), pool the
    /// implied ratios amplitude-weighted — every tone rides the SAME physical
    /// clock ratio, so pooling averages the tracking noise down — then
    /// joint-fit once more at the pooled ratio.
    inline double program_weighted_snr_db(std::span<const float> tail, const tone_comb& comb, double /*fsIn*/,
                                          double fs_out) {
        std::vector<double> work(tail.begin(), tail.end());
        const std::size_t   k = comb.freq_hz.size();
        std::vector<double> nus(k);
        for (std::size_t t = 0; t < k; ++t) {
            nus[t] = comb.freq_hz[t] / fs_out;
        }
        std::vector<tone_fit> fits(k);
        joint_fit_residual_power(work, nus, fits);

        // Ratio refinement against the joint residual, two rounds. Pooling
        // weight is (amplitude * frequency)^2 — Fisher weighting: a tone's
        // phase drift over the window scales with its frequency, so the high
        // tones carry nearly all the ratio information even though pink
        // weighting makes them the quietest. (Amplitude-only weighting leaves
        // the ratio unresolved and floors the whole instrument at -48 dB.)
        std::vector<double> lone(work.size());
        double              resid_power = 0.0;
        for (int round = 0; round < 2; ++round) {
            std::vector<double> resid(work);
            for (std::size_t i = 0; i < work.size(); ++i) {
                double model = 0.0;
                for (std::size_t t = 0; t < k; ++t) {
                    const double w = 2.0 * std::numbers::pi * nus[t] * static_cast<double>(i);
                    model += fits[t].a * std::sin(w) + fits[t].b * std::cos(w);
                }
                resid[i] -= model;
            }
            double rho_num = 0.0, rho_den = 0.0;
            for (std::size_t t = 0; t < k; ++t) {
                const double w = 2.0 * std::numbers::pi * nus[t];
                for (std::size_t i = 0; i < lone.size(); ++i) {
                    lone[i] = resid[i] + fits[t].a * std::sin(w * static_cast<double>(i))
                              + fits[t].b * std::cos(w * static_cast<double>(i));
                }
                const double refined = track_tone_freq(lone, nus[t]);
                const double wt      = comb.amplitude[t] * nus[t];
                rho_num += wt * wt * (refined / nus[t]);
                rho_den += wt * wt;
            }
            const double rho = rho_num / rho_den;
            for (std::size_t t = 0; t < k; ++t) {
                nus[t] *= rho;
            }
            resid_power = joint_fit_residual_power(work, nus, fits);
        }
        double signal = 0.0;
        for (const auto& f : fits) {
            signal += f.power();
        }
        return 10.0 * std::log10(signal / resid_power);
    }
    // ANCHOR_END: pw_metric

} // namespace srt_test
