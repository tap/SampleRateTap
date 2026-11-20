// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0, go/no-go 3 (IIR_PLAN I14): the float profile's noise floor with
// float states and products, against the same structure in long double
// (x86's 64-bit significand), for every profile, both variants, decimating
// and interpolating, at divisor 1 (the full designs; the relaxed ones have
// fewer, better-damped sections). Also double states with float I/O, the
// alternative I14 declines on cost.
//
// Measured per configuration, with a full-scale sine (997/48000 of the high
// rate decimating, of the low rate interpolating; 2^18 output samples after
// a 4096-sample settle) and with full-scale uniform white noise:
// - total error power against a full-scale sine's (0.5), in dB: the floor
//   a 120 dB claim is checked against;
// - the error spectrum's largest bin (Hann, 65536 points, a full-scale sine
//   reading 0 dB), for scale against a tone's alias;
// - the float output's own rounding (the reference rounded to float), the
//   floor any float output has, for comparison.
// Build: g++ -std=c++20 -O2 -I../../../../../submodules/dsptap/include
//        float_floor.cpp -o float_floor   (x86-64; long double is 80-bit)
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "kernels.h"
#include "tap/dsp/fft.h"

using namespace iir_spike;

namespace {

    constexpr std::size_t k_settle = 4096;
    constexpr std::size_t k_out    = 1u << 18;
    constexpr std::size_t k_fft    = 1u << 16;

    struct floor_result {
        double total_db;
        double max_bin_db;
    };

    floor_result measure(const std::vector<long double>& ref, const std::vector<double>& got) {
        double e2 = 0;
        for (std::size_t i = 0; i < ref.size(); ++i) {
            const double e = static_cast<double>(static_cast<long double>(got[i]) - ref[i]);
            e2 += e * e;
        }
        const double total = 10 * std::log10(e2 / static_cast<double>(ref.size()) / 0.5 + 1e-300);
        // the largest bin of the windowed error spectrum, averaged over the segments' maxima
        tap::dsp::real_fft  fft(k_fft);
        std::vector<double> buf(k_fft);
        double              max_bin = 0;
        for (std::size_t seg = 0; seg + k_fft <= ref.size(); seg += k_fft) {
            for (std::size_t i = 0; i < k_fft; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(k_fft));
                buf[i]         = w * static_cast<double>(static_cast<long double>(got[seg + i]) - ref[seg + i]);
            }
            fft.forward_inplace(buf.data());
            for (std::size_t k = 1; k < k_fft / 2; ++k) {
                const double m = std::hypot(buf[2 * k], buf[2 * k + 1]) / (static_cast<double>(k_fft) / 4);
                max_bin        = std::max(max_bin, m);
            }
        }
        return {total, 20 * std::log10(max_bin + 1e-300)};
    }

    // Run one structure in arithmetic T over the input; outputs as double/long double.
    template <class T, class Stage>
    std::vector<long double> run(Stage st, const std::vector<double>& x, bool decimate, bool round_io) {
        std::vector<long double> y;
        auto                     io = [&](long double v) -> T {
            return round_io ? static_cast<T>(static_cast<float>(v)) : static_cast<T>(v);
        };
        if (decimate) {
            for (std::size_t n = 0; n + 1 < x.size(); n += 2) {
                const T v = st.decimate(io(x[n]), io(x[n + 1]));
                y.push_back(round_io ? static_cast<long double>(static_cast<float>(v)) : static_cast<long double>(v));
            }
        }
        else {
            for (double xv : x) {
                T e, o;
                st.interpolate(io(xv), e, o);
                y.push_back(round_io ? static_cast<long double>(static_cast<float>(e)) : static_cast<long double>(e));
                y.push_back(round_io ? static_cast<long double>(static_cast<float>(o)) : static_cast<long double>(o));
            }
        }
        return y;
    }

    template <template <class, flush> class HB, class Design>
    void one(const Design& dsg, const char* variant, bool decimate, const char* signal) {
        const std::size_t   n_in = decimate ? 2 * (k_settle + k_out) : (k_settle + k_out) / 2;
        std::vector<double> x(n_in);
        if (std::string_view(signal) == "sine") {
            for (std::size_t i = 0; i < n_in; ++i)
                x[i] = std::sin(2.0 * M_PI * 997.0 / 48000.0 * static_cast<double>(i));
        }
        else {
            std::mt19937_64                        rng(12345);
            std::uniform_real_distribution<double> u(-1.0, 1.0);
            for (auto& v : x)
                v = static_cast<double>(static_cast<float>(u(rng)));
        }
        for (auto& v : x)
            v = static_cast<double>(static_cast<float>(v)); // float input, exactly
        const auto ref  = run<long double>(HB<long double, flush::none>(dsg), x, decimate, false);
        const auto f32  = run<float>(HB<float, flush::none>(dsg), x, decimate, true);
        const auto f64  = run<double>(HB<double, flush::none>(dsg), x, decimate, true);
        auto       tail = [](const std::vector<long double>& v) {
            return std::vector<long double>(v.end() - static_cast<std::ptrdiff_t>(k_out), v.end());
        };
        auto tail_d = [&](const std::vector<long double>& v) {
            std::vector<double> o;
            for (std::size_t i = v.size() - k_out; i < v.size(); ++i)
                o.push_back(static_cast<double>(v[i]));
            return o;
        };
        const auto          r  = tail(ref);
        const auto          rf = measure(r, tail_d(f32));
        const auto          rd = measure(r, tail_d(f64));
        std::vector<double> rounded;
        for (auto v : r)
            rounded.push_back(static_cast<double>(static_cast<float>(v)));
        const auto ro = measure(r, rounded);
        std::printf("| `%s` | %s | %s | %s | %.1f | %.1f | %.1f | %.1f | %.1f |\n", dsg.profile, variant,
                    decimate ? "down" : "up", signal, rf.total_db, rf.max_bin_db, rd.total_db, ro.total_db,
                    ro.max_bin_db);
    }

} // namespace

int main() {
    std::printf(
        "| profile | variant | dir | input | float states: total dB | max bin dB | double states, float I/O: "
        "total dB | float output rounding alone: total dB | max bin dB |\n|---|---|---|---|---:|---:|---:|---:|---:|\n");
    for (const char* prof : {"super_economy", "economy", "balanced", "transparent"}) {
        for (bool dec : {true, false}) {
            for (const char* sig : {"sine", "noise"}) {
                one<nonlinear_halfband>(find_nonlinear(prof, 1), "nonlinear", dec, sig);
                one<near_linear_halfband>(find_near_linear(prof, 1), "near-linear", dec, sig);
            }
        }
    }
}
