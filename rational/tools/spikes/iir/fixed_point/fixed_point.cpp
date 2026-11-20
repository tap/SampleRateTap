// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0, go/no-go 4 and the fixed-point rows (IIR_PLAN I6, I8): zero-input
// limit cycles, the decay to an exactly zero state, and the headroom under
// full-scale input, for the stored-state formats the plan names.
//
// Formats (I8): Q15 samples in Q2.29 32-bit states, Q31 samples in Q2.61
// 64-bit states; one-multiplier coefficients in Q31 (every a in (0, 1)),
// second-order coefficients (the near-linear sections and the guard band
// biquads) in Q2.30; one rounding point per section, at the section's
// output, from a wide accumulator (the difference node x - y1 formed there
// at full width).
//
// 1. The search (Jackson 1969: a zero-input limit cycle is the response to
//    the rounding error, so it lies within |y| <= e_max * ||h||_1, e_max the
//    rounding's largest error, 1/2 LSB rounding and 1 LSB truncating, h the
//    section's recursive impulse response). With zero input a cascade's first
//    section is autonomous after one sample (two for a second-order one),
//    and a section whose upstream output is zero is autonomous too, so the
//    cascade is free of zero-input limit cycles exactly when each section is.
//    Each section's autonomous recursion runs over every state in the box
//    |y| <= ceil(e_max ||h||_1) + 1: an exhaustive search, complete by that
//    bound. In integer units the recursion is the same for Q2.29 and Q2.61
//    states (the coefficient's quantization is the same), so one search
//    covers both formats.
// 2. The decay: the most samples any state of the format's full headroom
//    (|state| < 4: 2^31 LSB for Q2.29, 2^63 for Q2.61) takes to reach exactly
//    zero, per section (1-D: exact, the map is monotone in |u|; 2-D: the
//    linear decay into the box plus the box's longest time to zero, from the
//    exhaustive search) and summed along each path.
// 3. Bit-true Q15 stages (Q2.29 states) driven by full-scale square waves and
//    clipped sines: the largest |state| and |x - y1| seen, against the two
//    guard bits, and the frames from the last full-scale input to an exactly
//    zero state.
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "../cpp/designs.h"

using namespace iir_spike;
using i64  = std::int64_t;
using i128 = __int128;

enum class rnd { half_up, half_even, half_away, floor, toward_zero };
static const char* rnd_name(rnd r) {
    switch (r) {
    case rnd::half_up:
        return "round half up";
    case rnd::half_even:
        return "round half even";
    case rnd::half_away:
        return "round half away";
    case rnd::floor:
        return "truncate (floor)";
    default:
        return "magnitude truncation";
    }
}
constexpr rnd k_modes[] = {rnd::half_up, rnd::half_even, rnd::half_away, rnd::floor, rnd::toward_zero};

// acc / 2^sh under the rounding mode; acc may be negative.
static i128 shr(i128 acc, int sh, rnd r) {
    const i128 one = i128(1) << sh, half = one >> 1;
    const i128 fl  = acc >= 0 ? acc >> sh : -((-acc + one - 1) >> sh); // floor
    const i128 rem = acc - fl * one;                                   // in [0, one)
    switch (r) {
    case rnd::floor:
        return fl;
    case rnd::toward_zero:
        return (acc < 0 && rem != 0) ? fl + 1 : fl;
    case rnd::half_up:
        return rem >= half ? fl + 1 : fl;
    case rnd::half_away:
        if (rem > half)
            return fl + 1;
        if (rem < half)
            return fl;
        return acc >= 0 ? fl + 1 : fl;
    default:
        if (rem > half)
            return fl + 1;
        if (rem < half)
            return fl;
        return (fl & 1) ? fl + 1 : fl;
    }
}

static i64 q31(double a) {
    return static_cast<i64>(std::llround(a * 2147483648.0));
}
static i64 q230(double a) {
    return static_cast<i64>(std::llround(a * 1073741824.0));
}

// ---------------------------------------------------------------------------
// 1-D: u <- R(-a u), a in Q31 (the one-multiplier section, and the first-order
// guard section's recursive part, y <- R(-a1 y)).
struct lc1 {
    bool   cycle   = false;
    i64    amp     = 0;
    int    period  = 0;
    double bound   = 0;
    int    decay29 = 0, decay61 = 0;
};
static lc1 search1(i64 aq, rnd r) {
    lc1          out;
    const double a    = static_cast<double>(aq) / 2147483648.0;
    const double emax = (r == rnd::floor || r == rnd::toward_zero) ? 1.0 : 0.5;
    out.bound         = emax / (1.0 - std::fabs(a));
    const i64 B       = static_cast<i64>(std::ceil(out.bound)) + 1;
    auto      step    = [&](i64 u) { return static_cast<i64>(shr(-i128(aq) * u, 31, r)); };
    for (i64 u0 = -B; u0 <= B; ++u0) {
        std::map<i64, int> seen;
        i64                u = u0;
        for (int n = 0; n < 10000 && u != 0; ++n) {
            if (auto it = seen.find(u); it != seen.end()) {
                out.cycle  = true;
                out.period = std::max(out.period, n - it->second);
                for (auto& [v, k] : seen)
                    if (k >= it->second)
                        out.amp = std::max(out.amp, v < 0 ? -v : v);
                break;
            }
            seen[u] = n;
            u       = step(u);
        }
    }
    auto decay = [&](i128 u) {
        int n = 0;
        for (; u != 0 && n < 100000; ++n)
            u = shr(-i128(aq) * u, 31, r);
        return n;
    };
    out.decay29 = decay((i128(1) << 31) - 1);
    out.decay61 = decay((i128(1) << 63) - 1);
    return out;
}

// ---------------------------------------------------------------------------
// 2-D: y <- R(-c1 y1 - c2 y2), c in Q2.30 (a second-order section's recursive part).
struct lc2 {
    bool cycle   = false;
    i64  amp     = 0;
    int  period  = 0;
    i64  box     = 0;
    int  box_t   = 0; // the longest time to zero from the box
    int  decay29 = 0, decay61 = 0;
};
static lc2 search2(i64 c1q, i64 c2q, rnd r) {
    lc2          out;
    const double c1 = static_cast<double>(c1q) / 1073741824.0, c2 = static_cast<double>(c2q) / 1073741824.0;
    // ||h||_1 of 1 / (1 + c1 w^-1 + c2 w^-2)
    double l1 = 0, h1 = 0, h2 = 0;
    for (int n = 0; n < 200000; ++n) {
        const double h = (n == 0 ? 1.0 : 0.0) - c1 * h1 - c2 * h2;
        l1 += std::fabs(h);
        h2 = h1;
        h1 = h;
    }
    const double emax = (r == rnd::floor || r == rnd::toward_zero) ? 1.0 : 0.5;
    const i64    B    = static_cast<i64>(std::ceil(emax * l1)) + 1;
    out.box           = B;
    const i64 W       = 2 * B + 1;
    // colour: 0 unknown, -1 cycle, else 1 + time to zero (so 1 = the zero state)
    std::vector<int> colour(static_cast<std::size_t>(W * W), 0);
    auto             idx = [&](i64 y1, i64 y2) { return static_cast<std::size_t>((y1 + B) * W + (y2 + B)); };
    auto             in  = [&](i64 y1, i64 y2) { return y1 >= -B && y1 <= B && y2 >= -B && y2 <= B; };
    auto step         = [&](i64 y1, i64 y2) { return static_cast<i64>(shr(-i128(c1q) * y1 - i128(c2q) * y2, 30, r)); };
    colour[idx(0, 0)] = 1;
    for (i64 s1 = -B; s1 <= B; ++s1) {
        for (i64 s2 = -B; s2 <= B; ++s2) {
            if (colour[idx(s1, s2)] != 0)
                continue;
            std::vector<std::pair<i64, i64>> path;
            std::unordered_map<i64, int>     onpath;
            i64                              y1 = s1, y2 = s2;
            int                              result = 0; // final colour of the end state
            for (int n = 0; n < 100000; ++n) {
                if (in(y1, y2) && colour[idx(y1, y2)] != 0) {
                    result = colour[idx(y1, y2)];
                    break;
                }
                const i64 key = y1 * 4000000007LL + y2;
                if (auto it = onpath.find(key); it != onpath.end()) {
                    result     = -1;
                    out.cycle  = true;
                    out.period = std::max(out.period, static_cast<int>(path.size()) - it->second);
                    for (std::size_t k = static_cast<std::size_t>(it->second); k < path.size(); ++k)
                        out.amp = std::max({out.amp, std::abs(path[k].first), std::abs(path[k].second)});
                    break;
                }
                onpath[key] = static_cast<int>(path.size());
                path.emplace_back(y1, y2);
                const i64 y = step(y1, y2);
                y2          = y1;
                y1          = y;
            }
            // colour the path backwards
            int c = result;
            for (auto it = path.rbegin(); it != path.rend(); ++it) {
                if (c > 0)
                    ++c;
                if (in(it->first, it->second))
                    colour[idx(it->first, it->second)] = c;
            }
        }
    }
    for (int c : colour)
        out.box_t = std::max(out.box_t, c - 1);
    // the linear decay into the box from the full headroom M (infinity-norm of the companion matrix's powers)
    auto into_box = [&](double M) {
        double m11 = 1, m12 = 0, m21 = 0, m22 = 1; // A^n, A = [[-c1, -c2], [1, 0]]
        for (int n = 0; n < 1000000; ++n) {
            const double norm = std::max(std::fabs(m11) + std::fabs(m12), std::fabs(m21) + std::fabs(m22));
            if (M * norm <= 1.0)
                return n;
            const double n11 = -c1 * m11 - c2 * m21, n12 = -c1 * m12 - c2 * m22;
            m21 = m11;
            m22 = m12;
            m11 = n11;
            m12 = n12;
        }
        return -1;
    };
    if (!out.cycle) {
        out.decay29 = into_box(2147483648.0) + out.box_t;
        out.decay61 = into_box(9.2233720368547758e18) + out.box_t;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Bit-true Q15 two-path stages, Q2.29 states, for the headroom and decay checks.
struct q15_first_order_path {
    std::vector<i64> a; // Q31
    std::vector<i64> s; // Q2.29
    rnd              r;
    i64              max_state = 0, max_diff = 0;
    q15_first_order_path(std::span<const double> c, rnd rr)
        : s(c.size() + 1, 0)
        , r(rr) {
        for (double v : c)
            a.push_back(q31(v));
    }
    i64 operator()(i64 v) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            const i64  d   = v - s[i + 1]; // formed at full width
            const i128 acc = (i128(s[i]) << 31) + i128(a[i]) * d;
            const i64  y   = static_cast<i64>(shr(acc, 31, r));
            max_diff       = std::max(max_diff, d < 0 ? -d : d);
            s[i]           = v;
            v              = y;
            max_state      = std::max(max_state, y < 0 ? -y : y);
        }
        s[a.size()] = v;
        return v;
    }
    bool zero() const {
        return std::all_of(s.begin(), s.end(), [](i64 x) { return x == 0; });
    }
};
struct q15_sos_path {
    struct sec {
        i64  c1, c2;
        bool second;
    };
    std::vector<sec> secs;
    std::vector<i64> s1, s2;
    rnd              r;
    i64              max_state = 0, max_diff = 0;
    q15_sos_path(std::span<const double> flat, int order, rnd rr)
        : r(rr) {
        std::size_t i = 0;
        for (int left = order; left > 0;) {
            if (left >= 2) {
                secs.push_back({q230(flat[i]), q230(flat[i + 1]), true});
                i += 2;
                left -= 2;
            }
            else {
                secs.push_back({q230(flat[i]), 0, false});
                i += 1;
                left -= 1;
            }
        }
        s1.assign(secs.size() + 1, 0);
        s2.assign(secs.size() + 1, 0);
    }
    i64 operator()(i64 v) {
        for (std::size_t i = 0; i < secs.size(); ++i) {
            const sec& c = secs[i];
            i64        y;
            if (c.second) {
                const i64  d0 = v - s2[i + 1], d1 = s1[i] - s1[i + 1];
                const i128 acc = (i128(s2[i]) << 30) + i128(c.c2) * d0 + i128(c.c1) * d1;
                y              = static_cast<i64>(shr(acc, 30, r));
                max_diff       = std::max({max_diff, d0 < 0 ? -d0 : d0, d1 < 0 ? -d1 : d1});
            }
            else {
                const i64  d   = v - s1[i + 1];
                const i128 acc = (i128(s1[i]) << 30) + i128(c.c1) * d;
                y              = static_cast<i64>(shr(acc, 30, r));
                max_diff       = std::max(max_diff, d < 0 ? -d : d);
            }
            s2[i]     = s1[i];
            s1[i]     = v;
            v         = y;
            max_state = std::max(max_state, y < 0 ? -y : y);
        }
        s2[secs.size()] = s1[secs.size()];
        s1[secs.size()] = v;
        return v;
    }
    bool zero() const {
        return std::all_of(s1.begin(), s1.end(), [](i64 x) { return x == 0; })
               && std::all_of(s2.begin(), s2.end(), [](i64 x) { return x == 0; });
    }
};

constexpr i64 k_four = i64(1) << 31; // |state| < 4 in Q2.29

int main() {
    // ---- 1. the search, every section of every design
    std::printf("## 1. Zero-input limit cycles, exhaustive per section\n\n");
    std::printf("Sections searched: every one-multiplier coefficient (nonlinear, every profile and divisor) and every "
                "first-order coefficient of the near-linear paths, in Q31; every second-order section (the near-linear "
                "paths and the Chebyshev II guard band biquads, orders 3 to 11, every profile), in Q2.30.\n\n");
    std::printf("| rounding | 1st-order sections | with a limit cycle | largest amplitude (LSB) | 2nd-order sections | "
                "with a limit cycle | largest amplitude (LSB) |\n|---|---:|---:|---:|---:|---:|---:|\n");
    std::vector<double>                    first;
    std::vector<std::pair<double, double>> second;
    for (const auto& d : k_nonlinear) {
        for (double a : d.a0)
            first.push_back(a);
        for (double a : d.a1)
            first.push_back(a);
    }
    for (const auto& d : k_near_linear) {
        std::size_t i = 0;
        for (int left = d.N; left > 0;) {
            if (left >= 2) {
                second.emplace_back(d.sos[i], d.sos[i + 1]);
                i += 2;
                left -= 2;
            }
            else {
                first.push_back(d.sos[i]);
                i += 1;
                left -= 1;
            }
        }
    }
    std::vector<double> guard_first;
    for (const auto& g : k_guard) {
        for (std::size_t k = 0; k < g.sos.size(); k += 6) {
            if (g.sos[k + 5] == 0.0 && g.sos[k + 2] == 0.0)
                guard_first.push_back(g.sos[k + 4]); // first-order: y <- -a1 y
            else
                second.emplace_back(g.sos[k + 4], g.sos[k + 5]);
        }
    }
    for (double v : guard_first)
        first.push_back(v);
    for (rnd r : k_modes) {
        int n1 = 0, c1 = 0, n2 = 0, c2 = 0;
        i64 a1 = 0, a2 = 0;
        for (double a : first) {
            const auto s = search1(q31(a), r);
            ++n1;
            if (s.cycle) {
                ++c1;
                a1 = std::max(a1, s.amp);
            }
        }
        for (auto [x, y] : second) {
            const auto s = search2(q230(x), q230(y), r);
            ++n2;
            if (s.cycle) {
                ++c2;
                a2 = std::max(a2, s.amp);
            }
        }
        std::printf("| %s | %d | %d | %" PRId64 " | %d | %d | %" PRId64 " |\n", rnd_name(r), n1, c1, a1, n2, c2, a2);
        std::fflush(stdout);
    }

    // ---- 2. decay to exact zero under magnitude truncation, per design
    const rnd chosen = rnd::toward_zero;
    std::printf("\n## 2. The decay to an exactly zero state (%s), low-rate frames\n\n", rnd_name(chosen));
    std::printf(
        "Per path: 1 + the sum over its sections of each one's longest decay from the full headroom; the stage: "
        "the longer path, plus the decimator's one-sample odd-path latch (near-linear: the delay path's K "
        "frames run alongside). Q15 in Q2.29 states, Q31 in Q2.61.\n\n");
    std::printf("| profile | divisor | nonlinear Q15 | nonlinear Q31 | near-linear Q15 | near-linear Q31 |\n"
                "|---|---:|---:|---:|---:|---:|\n");
    for (std::size_t k = 0; k < std::size(k_nonlinear); ++k) {
        const auto& nl     = k_nonlinear[k];
        const auto& nr     = k_near_linear[k];
        long        p29[2] = {1, 1}, p61[2] = {1, 1};
        for (int p = 0; p < 2; ++p) {
            for (double a : (p ? nl.a1 : nl.a0)) {
                const auto s = search1(q31(a), chosen);
                p29[p] += s.decay29;
                p61[p] += s.decay61;
            }
        }
        long        n29 = 1, n61 = 1;
        std::size_t i = 0;
        for (int left = nr.N; left > 0;) {
            if (left >= 2) {
                const auto s = search2(q230(nr.sos[i]), q230(nr.sos[i + 1]), chosen);
                n29 += s.decay29 + 1;
                n61 += s.decay61 + 1;
                i += 2;
                left -= 2;
            }
            else {
                const auto s = search1(q31(nr.sos[i]), chosen);
                n29 += s.decay29;
                n61 += s.decay61;
                i += 1;
                left -= 1;
            }
        }
        std::printf("| `%s` | %d | %ld | %ld | %ld | %ld |\n", nl.profile, nl.divisor, std::max(p29[0], p29[1]) + 1,
                    std::max(p61[0], p61[1]) + 1, std::max<long>(n29 + 1, nr.K), std::max<long>(n61 + 1, nr.K));
    }

    // ---- 3. bit-true Q15: full-scale square waves and clipped sines, then silence
    std::printf("\n## 3. Bit-true Q15 stages (Q2.29 states, %s): full-scale input, then silence\n\n", rnd_name(chosen));
    std::printf("Decimators driven by full-scale square waves (periods 2 to 400 high-rate samples) and by sines at "
                "+12 dB clipped to full scale (eight frequencies), 6000 outputs each, then zeros. Largest |state| and "
                "|difference node| in units of full scale (two guard bits: < 4), and the most frames from the first "
                "zero input to an exactly zero state. Every divisor.\n\n");
    std::printf("| profile | variant | largest state | largest difference node | frames to zero (observed) |\n"
                "|---|---|---:|---:|---:|\n");
    for (const char* prof : {"super_economy", "economy", "balanced", "transparent"}) {
        for (int variant = 0; variant < 2; ++variant) {
            i64 ms = 0, md = 0;
            int zmax = 0;
            for (const auto& nl : k_nonlinear) {
                if (std::string(nl.profile) != prof)
                    continue;
                const near_linear_design* nr = nullptr;
                for (const auto& x : k_near_linear)
                    if (std::string(x.profile) == prof && x.divisor == nl.divisor)
                        nr = &x;
                std::vector<std::vector<int>> inputs;
                for (int per = 2; per <= 400; ++per) {
                    std::vector<int> x(12000);
                    for (int n = 0; n < 12000; ++n)
                        x[static_cast<std::size_t>(n)] = (n % per) < per / 2 ? 32767 : -32768;
                    inputs.push_back(x);
                }
                for (double fr : {0.001, 0.01, 0.03, 0.07, 0.11, 0.17, 0.23, 0.29}) {
                    std::vector<int> x(12000);
                    for (int n = 0; n < 12000; ++n)
                        x[static_cast<std::size_t>(n)] = static_cast<int>(
                            std::lround(32767.0 * std::clamp(4.0 * std::sin(2 * M_PI * fr * n), -1.0, 1.0)));
                    inputs.push_back(x);
                }
                for (const auto& x : inputs) {
                    if (variant == 0) {
                        q15_first_order_path p0(nl.a0, chosen), p1(nl.a1, chosen);
                        i64                  odd = 0;
                        for (std::size_t n = 0; n + 1 < x.size(); n += 2) {
                            p0(i64(x[n]) << 14);
                            p1(odd);
                            odd = i64(x[n + 1]) << 14;
                        }
                        int z = 0;
                        for (; z < 200000 && !(p0.zero() && p1.zero() && odd == 0); ++z) {
                            p0(0);
                            p1(odd);
                            odd = 0;
                        }
                        zmax = std::max(zmax, z);
                        ms   = std::max({ms, p0.max_state, p1.max_state});
                        md   = std::max({md, p0.max_diff, p1.max_diff});
                    }
                    else {
                        q15_sos_path p1(nr->sos, nr->N, chosen);
                        i64          odd = 0;
                        for (std::size_t n = 0; n + 1 < x.size(); n += 2) {
                            p1(odd);
                            odd = i64(x[n + 1]) << 14;
                        }
                        int z = 0;
                        for (; z < 200000 && !(p1.zero() && odd == 0); ++z) {
                            p1(odd);
                            odd = 0;
                        }
                        zmax = std::max({zmax, z, nr->K});
                        ms   = std::max(ms, p1.max_state);
                        md   = std::max(md, p1.max_diff);
                    }
                }
            }
            std::printf("| `%s` | %s | %.3f | %.3f | %d |\n", prof, variant ? "near-linear" : "nonlinear",
                        static_cast<double>(ms) / 536870912.0, static_cast<double>(md) / 536870912.0, zmax);
            if (ms >= k_four)
                std::printf("STATE OVERFLOW of the two guard bits\n");
        }
    }

    // ---- 4. the Q15 floor: the bit-true decimator against the same structure in double
    std::printf("\n## 4. The Q15 floor (Q2.29 states, %s, the output rounded half up to Q15)\n\n", rnd_name(chosen));
    std::printf(
        "A sine at 997/48000 of the high rate, -1 dBFS, quantized to Q15, through the bit-true decimator and "
        "through the same structure in double on the same input; error power against a full-scale sine's, "
        "2^16 outputs after 4096. Divisor 1. Beside each, the double reference rounded to Q15: the floor any Q15 "
        "output of that signal has.\n\n");
    std::printf(
        "| profile | nonlinear dB | its reference rounded to Q15 | near-linear dB | its reference rounded to Q15 "
        "|\n|---|---:|---:|---:|---:|\n");
    for (const char* prof : {"super_economy", "economy", "balanced", "transparent"}) {
        double res[2] = {0, 0}, alone[2] = {0, 0};
        for (int variant = 0; variant < 2; ++variant) {
            const nonlinear_design*   nl = nullptr;
            const near_linear_design* nr = nullptr;
            for (const auto& x : k_nonlinear)
                if (std::string(x.profile) == prof && x.divisor == 1)
                    nl = &x;
            for (const auto& x : k_near_linear)
                if (std::string(x.profile) == prof && x.divisor == 1)
                    nr = &x;
            const std::size_t n_out = 4096 + 65536;
            std::vector<int>  x(2 * n_out);
            for (std::size_t n = 0; n < x.size(); ++n)
                x[n] = static_cast<int>(std::lround(32767.0 * 0.891250938 * std::sin(2 * M_PI * 997.0 / 48000.0 * n)));
            // double reference of the same structure
            std::vector<double> ref, got;
            if (variant == 0) {
                q15_first_order_path p0(nl->a0, chosen), p1(nl->a1, chosen);
                std::vector<double>  s0(nl->a0.size() + 1, 0.0), s1(nl->a1.size() + 1, 0.0);
                auto                 path = [](std::span<const double> a, std::vector<double>& s, double v) {
                    for (std::size_t i = 0; i < a.size(); ++i) {
                        const double y = s[i] + a[i] * (v - s[i + 1]);
                        s[i]           = v;
                        v              = y;
                    }
                    s[a.size()] = v;
                    return v;
                };
                i64    odd   = 0;
                double odd_d = 0;
                for (std::size_t n = 0; n + 1 < x.size(); n += 2) {
                    const i64 sum = p0(i64(x[n]) << 14) + p1(odd);
                    i64       r   = (sum + (i64(1) << 14)) >> 15;
                    r             = std::clamp<i64>(r, -32768, 32767);
                    got.push_back(static_cast<double>(r) / 32768.0);
                    ref.push_back(0.5 * (path(nl->a0, s0, x[n] / 32768.0) + path(nl->a1, s1, odd_d)));
                    odd   = i64(x[n + 1]) << 14;
                    odd_d = x[n + 1] / 32768.0;
                }
            }
            else {
                q15_sos_path        p1(nr->sos, nr->N, chosen);
                std::vector<double> dl(static_cast<std::size_t>(nr->K), 0.0);
                std::vector<i64>    dq(static_cast<std::size_t>(nr->K), 0);
                // the double allpass path: the direct form of its denominator
                const std::size_t   N = static_cast<std::size_t>(nr->N);
                std::vector<double> xin(N + 1, 0.0), yout(N + 1, 0.0);
                std::size_t         at    = 0;
                i64                 odd   = 0;
                double              odd_d = 0;
                for (std::size_t n = 0; n + 1 < x.size(); n += 2) {
                    const i64    d0q = dq[at];
                    const double d0  = dl[at];
                    dq[at]           = i64(x[n]) << 14;
                    dl[at]           = x[n] / 32768.0;
                    at               = (at + 1) % dq.size();
                    const i64 sum    = d0q + p1(odd);
                    i64       r      = (sum + (i64(1) << 14)) >> 15;
                    r                = std::clamp<i64>(r, -32768, 32767);
                    got.push_back(static_cast<double>(r) / 32768.0);
                    // A(w) = w^-N D(1/w) / D(w): y = sum_k den[N-k] x[-k] - sum_{k>=1} den[k] y[-k]
                    for (std::size_t k = N; k > 0; --k) {
                        xin[k]  = xin[k - 1];
                        yout[k] = yout[k - 1];
                    }
                    xin[0]   = odd_d;
                    double y = 0;
                    for (std::size_t k = 0; k <= N; ++k)
                        y += nr->den[N - k] * xin[k];
                    for (std::size_t k = 1; k <= N; ++k)
                        y -= nr->den[k] * yout[k];
                    yout[0] = y;
                    ref.push_back(0.5 * (d0 + y));
                    odd   = i64(x[n + 1]) << 14;
                    odd_d = x[n + 1] / 32768.0;
                }
            }
            double e2 = 0, r2 = 0;
            for (std::size_t i = 4096; i < got.size(); ++i) {
                e2 += (got[i] - ref[i]) * (got[i] - ref[i]);
                const double rq = std::floor(ref[i] * 32768.0 + 0.5) / 32768.0 - ref[i];
                r2 += rq * rq;
            }
            res[variant]   = 10 * std::log10(e2 / static_cast<double>(got.size() - 4096) / 0.5);
            alone[variant] = 10 * std::log10(r2 / static_cast<double>(got.size() - 4096) / 0.5);
        }
        std::printf("| `%s` | %.1f | %.1f | %.1f | %.1f |\n", prof, res[0], alone[0], res[1], alone[1]);
    }
}
