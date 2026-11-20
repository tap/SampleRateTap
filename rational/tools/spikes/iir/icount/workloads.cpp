// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0 counted workloads (scratch, not shipped): one binary per scenario
// (IIR_SC), each bracketing the counted calls with tools/probe/
// call_marker.h's markers, so the counting plugin's TAP_SR_CALL_MAX line
// gives their total and count (run_targets.py divides). Mono, 32 output
// frames per call, 16 calls after a warm-up block, economy unless named.
//
// The kernels are written as a shipping stage would have them: section
// counts and the format fixed at compile time, the state in std::arrays.
// Formats (IIR-0's recommendation from fixed_point.cpp): float and double
// states and products; Q15 in Q3.28 int32 states with Q31 coefficients
// (|x - y1| < 8 in 32 bits), Q31 in Q3.60 int64 states (a 64 x 32 multiply);
// magnitude truncation at each section's output (no zero-input limit
// cycle, the exhaustive search).
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

#include "../cpp/designs.h"
#include "../ftz/flush_guard.h"
#include "call_marker.h"
#if __has_include("../cpp/fourpath_designs.h")
#include "../cpp/fourpath_designs.h"
#endif
#if IIR_SC >= 44 && IIR_SC <= 47
#include "tap/sr/rational/stage.h"
#endif

using namespace iir_spike;

#ifndef IIR_SC
#define IIR_SC 0
#endif

namespace {

    constexpr int k_frames = 32;
    constexpr int k_calls  = 16;

    // -------- float / double one-multiplier path, N sections
    template <class T, int N, bool Flush = false>
    struct fpath {
        std::array<T, (N > 0 ? N : 1)> a{};
        std::array<T, N + 1>           s{};
        void                           init(std::span<const double> c) {
            for (int i = 0; i < N; ++i)
                a[static_cast<std::size_t>(i)] = static_cast<T>(c[static_cast<std::size_t>(i)]);
        }
        static T fl(T v) noexcept {
            if constexpr (Flush)
                return std::fabs(v) < std::numeric_limits<T>::min() ? T(0) : v;
            else
                return v;
        }
        [[gnu::always_inline]] T operator()(T v) noexcept {
            for (int i = 0; i < N; ++i) {
                const T y = s[i] + a[i] * (v - s[i + 1]);
                s[i]      = fl(v);
                v         = y;
            }
            s[N] = fl(v);
            return v;
        }
    };

    // -------- Q15 (Q3.28 int32 states) one-multiplier path
    inline std::int32_t mt31(std::int64_t p) noexcept { // p / 2^31 toward zero
        return static_cast<std::int32_t>((p + ((p >> 63) & 0x7fffffff)) >> 31);
    }
    // Floor truncation (an arithmetic shift) is enough for a one-multiplier
    // section with a > 0 (its zero-input recursion u <- floor(-a u) alternates
    // sign and shrinks every two samples: fixed_point.cpp finds no limit cycle
    // under it in any first-order section); the second-order sections need
    // magnitude truncation.
    inline std::int32_t fl31(std::int64_t p) noexcept {
        return static_cast<std::int32_t>(p >> 31);
    }
    template <int N, bool Floor = false>
    struct qpath15 {
        std::array<std::int32_t, (N > 0 ? N : 1)> a{};
        std::array<std::int32_t, N + 1>           s{};
        void                                      init(std::span<const double> c) {
            for (int i = 0; i < N; ++i)
                a[static_cast<std::size_t>(i)] =
                    static_cast<std::int32_t>(std::lround(c[static_cast<std::size_t>(i)] * 2147483648.0));
        }
        [[gnu::always_inline]] std::int32_t operator()(std::int32_t v) noexcept {
            for (int i = 0; i < N; ++i) {
                const std::int64_t p = static_cast<std::int64_t>(a[i]) * (v - s[i + 1]);
                const std::int32_t y = s[i] + (Floor ? fl31(p) : mt31(p));
                s[i]                 = v;
                v                    = y;
            }
            s[N] = v;
            return v;
        }
    };

    // -------- Q31 (Q3.60 int64 states): a 64 x 32 multiply, toward zero
    template <bool Floor = false>
    inline std::int64_t mul64x32_mt(std::int64_t d, std::int32_t a) noexcept {
        const std::int64_t hi = static_cast<std::int64_t>(static_cast<std::int32_t>(d >> 32)) * a;
        const std::int64_t lo = static_cast<std::int64_t>(static_cast<std::uint32_t>(d)) * a;
        // d * a = hi 2^32 + lo; / 2^31 = 2 hi + lo / 2^31
        const std::int64_t q = 2 * hi + (lo >> 31);
        if constexpr (Floor)
            return q;
        const bool r = (lo & 0x7fffffff) != 0;
        return q + ((q < 0) & r);
    }
    template <int N, bool Floor = false>
    struct qpath31 {
        std::array<std::int32_t, (N > 0 ? N : 1)> a{};
        std::array<std::int64_t, N + 1>           s{};
        void                                      init(std::span<const double> c) {
            for (int i = 0; i < N; ++i)
                a[static_cast<std::size_t>(i)] =
                    static_cast<std::int32_t>(std::lround(c[static_cast<std::size_t>(i)] * 2147483648.0));
        }
        [[gnu::always_inline]] std::int64_t operator()(std::int64_t v) noexcept {
            for (int i = 0; i < N; ++i) {
                const std::int64_t y = s[i] + mul64x32_mt<Floor>(v - s[i + 1], a[i]);
                s[i]                 = v;
                v                    = y;
            }
            s[N] = v;
            return v;
        }
    };

    // -------- near-linear allpass path: N2 second-order sections, then N1 (0 or 1) first-order
    template <class T, int N2, int N1>
    struct sospath {
        std::array<T, N2 + N1>           c1{};
        std::array<T, (N2 > 0 ? N2 : 1)> c2{};
        std::array<T, N2 + N1 + 1>       s1{}, s2{};
        void                             init(std::span<const double> flat) {
            std::size_t k = 0;
            for (int i = 0; i < N2; ++i) {
                c1[static_cast<std::size_t>(i)] = static_cast<T>(flat[k++]);
                c2[static_cast<std::size_t>(i)] = static_cast<T>(flat[k++]);
            }
            if (N1)
                c1[N2] = static_cast<T>(flat[k]);
        }
        [[gnu::always_inline]] T operator()(T v) noexcept {
            for (int i = 0; i < N2; ++i) {
                const T y = c2[i] * (v - s2[i + 1]) + c1[i] * (s1[i] - s1[i + 1]) + s2[i];
                s2[i]     = s1[i];
                s1[i]     = v;
                v         = y;
            }
            if constexpr (N1 == 1) {
                const T y = s1[N2] + c1[N2] * (v - s1[N2 + 1]);
                s2[N2]    = s1[N2];
                s1[N2]    = v;
                v         = y;
            }
            constexpr int n = N2 + N1;
            s2[n]           = s1[n];
            s1[n]           = v;
            return v;
        }
    };
    inline std::int32_t mt30(std::int64_t p) noexcept {
        return static_cast<std::int32_t>((p + ((p >> 63) & 0x3fffffff)) >> 30);
    }
    template <int N2, int N1>
    struct qsospath15 { // Q2.30 coefficients, Q3.28 states
        std::array<std::int32_t, N2 + N1>           c1{};
        std::array<std::int32_t, (N2 > 0 ? N2 : 1)> c2{};
        std::array<std::int32_t, N2 + N1 + 1>       s1{}, s2{};
        void                                        init(std::span<const double> flat) {
            std::size_t k = 0;
            auto        q = [](double v) { return static_cast<std::int32_t>(std::lround(v * 1073741824.0)); };
            for (int i = 0; i < N2; ++i) {
                c1[static_cast<std::size_t>(i)] = q(flat[k++]);
                c2[static_cast<std::size_t>(i)] = q(flat[k++]);
            }
            if (N1)
                c1[N2] = q(flat[k]);
        }
        [[gnu::always_inline]] std::int32_t operator()(std::int32_t v) noexcept {
            for (int i = 0; i < N2; ++i) {
                const std::int64_t acc = static_cast<std::int64_t>(c2[i]) * (v - s2[i + 1])
                                         + static_cast<std::int64_t>(c1[i]) * (s1[i] - s1[i + 1]);
                const std::int32_t y = s2[i] + mt30(acc);
                s2[i]                = s1[i];
                s1[i]                = v;
                v                    = y;
            }
            if constexpr (N1 == 1) {
                const std::int32_t y = s1[N2] + mt30(static_cast<std::int64_t>(c1[N2]) * (v - s1[N2 + 1]));
                s2[N2]               = s1[N2];
                s1[N2]               = v;
                v                    = y;
            }
            constexpr int n = N2 + N1;
            s2[n]           = s1[n];
            s1[n]           = v;
            return v;
        }
    };

    // -------- Hexagon's alternatives to the per-section flush (IIR-0 measures them):
    // the states checked once per call, or every stored state passed through
    // (v + c) - c, which zeroes |v| < ulp(c) / 2 and rounds the rest to
    // multiples of ulp(c) (c = 2^-60: ulp 2^-83, some -500 dB below full scale).
    template <class T, int N, bool F>
    void flush_states(fpath<T, N, F>& p) noexcept {
        for (auto& v : p.s)
            v = std::fabs(v) < std::numeric_limits<T>::min() ? T(0) : v;
    }
    template <class T, int N>
    struct opath {
        std::array<T, (N > 0 ? N : 1)> a{};
        std::array<T, N + 1>           s{};
        void                           init(std::span<const double> c) {
            for (int i = 0; i < N; ++i)
                a[static_cast<std::size_t>(i)] = static_cast<T>(c[static_cast<std::size_t>(i)]);
        }
        [[gnu::always_inline]] T operator()(T v) noexcept {
            constexpr T c = T(8.673617379884035e-19); // 2^-60
            for (int i = 0; i < N; ++i) {
                const T y = s[i] + a[i] * (v - s[i + 1]);
                s[i]      = v;
                v         = (y + c) - c;
            }
            s[N] = v;
            return v;
        }
    };
    template <class T, int N0, int N1c>
    struct hb_dec_offset {
        opath<T, N0>  p0;
        opath<T, N1c> p1;
        T             odd = 0;
        void          init(const nonlinear_design& d) {
            p0.init(d.a0);
            p1.init(d.a1);
        }
        [[gnu::noinline]] void process(const T* in, T* out, int n) noexcept {
            T o = odd;
            for (int f = 0; f < n; ++f) {
                out[f] = T(0.5) * (p0(in[2 * f]) + p1(o));
                o      = in[2 * f + 1];
            }
            odd = o;
        }
    };

    // -------- half-band stages over blocks
    template <class T, int N0, int N1c, bool Flush = false, bool CallFlush = false>
    struct hb_dec {
        fpath<T, N0, Flush>  p0;
        fpath<T, N1c, Flush> p1;
        T                    odd = 0;
        void                 init(const nonlinear_design& d) {
            p0.init(d.a0);
            p1.init(d.a1);
        }
        [[gnu::noinline]] void process(const T* in, T* out, int n) noexcept {
            T o = odd;
            for (int f = 0; f < n; ++f) {
                out[f] = T(0.5) * (p0(in[2 * f]) + p1(o));
                o      = in[2 * f + 1];
            }
            odd = o;
            if constexpr (CallFlush) {
                flush_states(p0);
                flush_states(p1);
            }
        }
    };
    template <class T, int N0, int N1c>
    struct hb_int {
        fpath<T, N0>  p0;
        fpath<T, N1c> p1;
        void          init(const nonlinear_design& d) {
            p0.init(d.a0);
            p1.init(d.a1);
        }
        [[gnu::noinline]] void process(const T* in, T* out, int n) noexcept {
            for (int f = 0; f < n; ++f) {
                out[2 * f]     = p0(in[f]);
                out[2 * f + 1] = p1(in[f]);
            }
        }
    };
    template <int N0, int N1c, bool Floor = false>
    struct hb_dec_q15 {
        qpath15<N0, Floor>  p0;
        qpath15<N1c, Floor> p1;
        std::int32_t        odd = 0;
        void                init(const nonlinear_design& d) {
            p0.init(d.a0);
            p1.init(d.a1);
        }
        // Q15 in and out (int16 carried in int32 lanes between stages), states Q3.28
        [[gnu::noinline]] void process(const std::int16_t* in, std::int16_t* out, int n) noexcept {
            std::int32_t o = odd;
            for (int f = 0; f < n; ++f) {
                const std::int32_t s = p0(static_cast<std::int32_t>(in[2 * f]) << 13) + p1(o);
                std::int32_t       r = (s + (1 << 13)) >> 14;
                r                    = r > 32767 ? 32767 : (r < -32768 ? -32768 : r);
                out[f]               = static_cast<std::int16_t>(r);
                o                    = static_cast<std::int32_t>(in[2 * f + 1]) << 13;
            }
            odd = o;
        }
    };
    template <int N0, int N1c>
    struct hb_int_q15 {
        qpath15<N0>  p0;
        qpath15<N1c> p1;
        void         init(const nonlinear_design& d) {
            p0.init(d.a0);
            p1.init(d.a1);
        }
        static std::int16_t out15(std::int32_t v) noexcept {
            std::int32_t r = (v + (1 << 12)) >> 13;
            return static_cast<std::int16_t>(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
        }
        [[gnu::noinline]] void process(const std::int16_t* in, std::int16_t* out, int n) noexcept {
            for (int f = 0; f < n; ++f) {
                const std::int32_t x = static_cast<std::int32_t>(in[f]) << 13;
                out[2 * f]           = out15(p0(x));
                out[2 * f + 1]       = out15(p1(x));
            }
        }
    };
    template <int N0, int N1c, bool Floor = false>
    struct hb_dec_q31 {
        qpath31<N0, Floor>  p0;
        qpath31<N1c, Floor> p1;
        std::int64_t        odd = 0;
        void                init(const nonlinear_design& d) {
            p0.init(d.a0);
            p1.init(d.a1);
        }
        [[gnu::noinline]] void process(const std::int32_t* in, std::int32_t* out, int n) noexcept {
            std::int64_t o = odd;
            for (int f = 0; f < n; ++f) {
                const std::int64_t s = p0(static_cast<std::int64_t>(in[2 * f]) << 29) + p1(o);
                std::int64_t       r = (s + (std::int64_t{1} << 29)) >> 30;
                r                    = r > 2147483647 ? 2147483647 : (r < -2147483647 - 1 ? -2147483647 - 1 : r);
                out[f]               = static_cast<std::int32_t>(r);
                o                    = static_cast<std::int64_t>(in[2 * f + 1]) << 29;
            }
            odd = o;
        }
    };
    template <class T, int K, int N2, int N1>
    struct nr_dec {
        std::array<T, K>       dly{};
        sospath<T, N2, N1>     p1;
        T                      odd = 0;
        void                   init(const near_linear_design& d) { p1.init(d.sos); }
        [[gnu::noinline]] void process(const T* in, T* out, int n) noexcept {
            T o = odd;
            for (int f = 0; f < n; ++f) {
                const T d0 = dly[K - 1];
                for (int k = K - 1; k > 0; --k)
                    dly[k] = dly[k - 1];
                dly[0] = in[2 * f];
                out[f] = T(0.5) * (d0 + p1(o));
                o      = in[2 * f + 1];
            }
            odd = o;
        }
    };
    template <int K, int N2, int N1>
    struct nr_dec_q15 {
        std::array<std::int32_t, K> dly{};
        qsospath15<N2, N1>          p1;
        std::int32_t                odd = 0;
        void                        init(const near_linear_design& d) { p1.init(d.sos); }
        [[gnu::noinline]] void      process(const std::int16_t* in, std::int16_t* out, int n) noexcept {
            std::int32_t o = odd;
            for (int f = 0; f < n; ++f) {
                const std::int32_t d0 = dly[K - 1];
                for (int k = K - 1; k > 0; --k)
                    dly[k] = dly[k - 1];
                dly[0]               = static_cast<std::int32_t>(in[2 * f]) << 13;
                const std::int32_t s = d0 + p1(o);
                std::int32_t       r = (s + (1 << 13)) >> 14;
                out[f]               = static_cast<std::int16_t>(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
                o                    = static_cast<std::int32_t>(in[2 * f + 1]) << 13;
            }
            odd = o;
        }
    };

#if defined(IIR_HAVE_FOURPATH)
    // -------- the four-path decimator: path k gets x[4n - k]; sizes from the layout
    template <class T, int A0, int A1, int A2, int A3>
    struct fp_dec {
        fpath<T, A0> p0;
        fpath<T, A1> p1;
        fpath<T, A2> p2;
        fpath<T, A3> p3;
        T            l1 = 0, l2 = 0, l3 = 0; // x[4n - 1], x[4n - 2], x[4n - 3] from the previous frame
        void         init(const fourpath_design& d) {
            p0.init(d.paths[0]);
            p1.init(d.paths[1]);
            p2.init(d.paths[2]);
            p3.init(d.paths[3]);
        }
        [[gnu::noinline]] void process(const T* in, T* out, int n) noexcept {
            T a = l1, b = l2, c = l3;
            for (int f = 0; f < n; ++f) {
                out[f] = T(0.25) * ((p0(in[4 * f]) + p1(a)) + (p2(b) + p3(c)));
                a      = in[4 * f + 3];
                b      = in[4 * f + 2];
                c      = in[4 * f + 1];
            }
            l1 = a;
            l2 = b;
            l3 = c;
        }
    };
    // Q15 in and out, Q3.28 states, floor truncation (first-order sections only)
    template <int A0, int A1, int A2, int A3>
    struct fp_dec_q15 {
        qpath15<A0, true> p0;
        qpath15<A1, true> p1;
        qpath15<A2, true> p2;
        qpath15<A3, true> p3;
        std::int32_t      l1 = 0, l2 = 0, l3 = 0;
        void              init(const fourpath_design& d) {
            p0.init(d.paths[0]);
            p1.init(d.paths[1]);
            p2.init(d.paths[2]);
            p3.init(d.paths[3]);
        }
        [[gnu::noinline]] void process(const std::int16_t* in, std::int16_t* out, int n) noexcept {
            std::int32_t a = l1, b = l2, c = l3;
            for (int f = 0; f < n; ++f) {
                const std::int32_t s = (p0(static_cast<std::int32_t>(in[4 * f]) << 13) + p1(a)) + (p2(b) + p3(c));
                std::int32_t       r = (s + (1 << 14)) >> 15;
                out[f]               = static_cast<std::int16_t>(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
                a                    = static_cast<std::int32_t>(in[4 * f + 3]) << 13;
                b                    = static_cast<std::int32_t>(in[4 * f + 2]) << 13;
                c                    = static_cast<std::int32_t>(in[4 * f + 1]) << 13;
            }
            l1 = a;
            l2 = b;
            l3 = c;
        }
    };
#endif

    // -------- harness
    template <class T>
    T test_input(int i) {
        const double v = 0.5 * std::sin(0.0371 * i) + 0.3 * std::sin(0.71 * i);
        if constexpr (std::is_same_v<T, std::int16_t>)
            return static_cast<T>(std::lround(v * 32767.0));
        else if constexpr (std::is_same_v<T, std::int32_t>)
            return static_cast<T>(std::lround(v * 2147483647.0));
        else
            return static_cast<T>(v);
    }

    volatile double g_sink = 0;

    template <class T, class Stage, int Factor>
    void run_dec(Stage& st) {
        static T in[k_frames * Factor];
        static T out[k_frames];
        for (int i = 0; i < k_frames * Factor; ++i)
            in[i] = test_input<T>(i);
        st.process(in, out, k_frames); // warm-up
        double acc = 0;
        for (int c = 0; c < k_calls; ++c) {
            for (int i = 0; i < k_frames * Factor; ++i)
                in[i] = test_input<T>(i + (c + 1) * k_frames * Factor);
            tap_sr_call_begin();
            st.process(in, out, k_frames);
            tap_sr_call_end();
            for (int i = 0; i < k_frames; ++i)
                acc += static_cast<double>(out[i]);
        }
        g_sink = acc;
    }
    template <class T, class Stage>
    void run_int(Stage& st) {
        static T in[k_frames / 2];
        static T out[k_frames];
        for (int c = 0; c <= k_calls; ++c) {
            for (int i = 0; i < k_frames / 2; ++i)
                in[i] = test_input<T>(i + c * k_frames / 2);
            if (c)
                tap_sr_call_begin();
            st.process(in, out, k_frames / 2);
            if (c)
                tap_sr_call_end();
            for (int i = 0; i < k_frames; ++i)
                g_sink = g_sink + static_cast<double>(out[i]);
        }
    }

    // A guarded call: the guard around a stage's block (or around nothing).
    template <class T, class Stage, bool Guard = true>
    [[gnu::noinline]] void guarded(Stage& st, const T* in, T* out, int n) noexcept {
        if constexpr (Guard) {
            flush_guard<T> g;
            st.process(in, out, n);
        }
        else {
            st.process(in, out, n);
        }
    }
    struct nothing {
        [[gnu::noinline]] void process(const float*, float* out, int) noexcept { out[0] = 0.0f; }
    };

    template <class T, class Stage, bool Guard = true>
    void run_guarded(Stage& st, bool host_on) {
        if constexpr (k_guard_governs<T>) {
            const auto b = read_fpcr();
            write_fpcr(host_on ? (b | k_flush_bits) : (b & ~k_flush_bits));
        }
        static T in[k_frames * 2];
        static T out[k_frames];
        for (int i = 0; i < k_frames * 2; ++i)
            in[i] = test_input<T>(i);
        guarded<T, Stage, Guard>(st, in, out, k_frames);
        for (int c = 0; c < k_calls; ++c) {
            tap_sr_call_begin();
            guarded<T, Stage, Guard>(st, in, out, k_frames);
            tap_sr_call_end();
        }
        g_sink = out[0];
    }

    const nonlinear_design& nl(const char* p, int d) {
        for (const auto& x : k_nonlinear)
            if (std::string_view(x.profile) == p && x.divisor == d)
                return x;
        return k_nonlinear[0];
    }
    const near_linear_design& nr(const char* p, int d) {
        for (const auto& x : k_near_linear)
            if (std::string_view(x.profile) == p && x.divisor == d)
                return x;
        return k_near_linear[0];
    }

    // Cascades of decimators over blocks: stage outputs feed the next stage.
    template <class T, class... S>
    struct cascade;
    template <class T, class S0, class S1>
    struct cascade<T, S0, S1> {
        S0                     s0;
        S1                     s1;
        int                    f0, f1; // decimation factors of each stage
        [[gnu::noinline]] void process(const T* in, T* out, int n) noexcept {
            T mid[k_frames * 8];
            s0.process(in, mid, n * f1);
            s1.process(mid, out, n);
        }
    };
    template <class T, class S0, class S1, class S2>
    struct cascade<T, S0, S1, S2> {
        S0                     s0;
        S1                     s1;
        S2                     s2;
        int                    f1, f2;
        [[gnu::noinline]] void process(const T* in, T* out, int n) noexcept {
            T m0[k_frames * 8], m1[k_frames * 4];
            s0.process(in, m0, n * f1 * f2);
            s1.process(m0, m1, n * f2);
            s2.process(m1, out, n);
        }
    };
    template <class T, class S0, class S1, class S2, class S3>
    struct cascade<T, S0, S1, S2, S3> {
        S0                     s0;
        S1                     s1;
        S2                     s2;
        S3                     s3;
        [[gnu::noinline]] void process(const T* in, T* out, int n) noexcept {
            T m0[k_frames * 8], m1[k_frames * 4], m2[k_frames * 2];
            s0.process(in, m0, n * 8);
            s1.process(m0, m1, n * 4);
            s2.process(m1, m2, n * 2);
            s3.process(m2, out, n);
        }
    };

} // namespace

int main() {
    // The economy designs: N9 at d1 (2 + 2), N5 at d2 and d4 (1 + 1), N3 at d8 (1 + 0);
    // near-linear N7 (3 second-order + 1 first-order, K = 7).
    const char* eco = "economy";
#if IIR_SC == 0
    hb_dec<float, 2, 2> st;
    st.init(nl(eco, 1));
    run_dec<float, decltype(st), 2>(st);
#elif IIR_SC == 1
    hb_dec<double, 2, 2> st;
    st.init(nl(eco, 1));
    run_dec<double, decltype(st), 2>(st);
#elif IIR_SC == 2
    hb_dec_q15<2, 2> st;
    st.init(nl(eco, 1));
    run_dec<std::int16_t, decltype(st), 2>(st);
#elif IIR_SC == 3
    hb_dec_q31<2, 2> st;
    st.init(nl(eco, 1));
    run_dec<std::int32_t, decltype(st), 2>(st);
#elif IIR_SC == 4
    hb_dec<float, 2, 2, true> st;
    st.init(nl(eco, 1));
    run_dec<float, decltype(st), 2>(st);
#elif IIR_SC == 5
    hb_dec<double, 2, 2, true> st;
    st.init(nl(eco, 1));
    run_dec<double, decltype(st), 2>(st);
#elif IIR_SC == 6 || IIR_SC == 7
    hb_dec<float, 2, 2> st;
    st.init(nl(eco, 1));
    run_guarded<float>(st, IIR_SC == 7);
#elif IIR_SC == 8 || IIR_SC == 9
    nothing st;
    run_guarded<float>(st, IIR_SC == 9);
#elif IIR_SC == 10
    nr_dec<float, 7, 3, 1> st;
    st.init(nr(eco, 1));
    run_dec<float, decltype(st), 2>(st);
#elif IIR_SC == 11
    nr_dec_q15<7, 3, 1> st;
    st.init(nr(eco, 1));
    run_dec<std::int16_t, decltype(st), 2>(st);
#elif IIR_SC == 12
    hb_int<float, 2, 2> st;
    st.init(nl(eco, 1));
    run_int<float>(st);
#elif IIR_SC == 13
    hb_int_q15<2, 2> st;
    st.init(nl(eco, 1));
    run_int<std::int16_t>(st);
#elif IIR_SC == 14
    hb_dec<float, 4, 4> st; // transparent N17
    st.init(nl("transparent", 1));
    run_dec<float, decltype(st), 2>(st);
#elif IIR_SC == 15
    hb_dec<double, 4, 4> st;
    st.init(nl("transparent", 1));
    run_dec<double, decltype(st), 2>(st);
#elif IIR_SC == 16
    hb_dec<float, 4, 4, true> st;
    st.init(nl("transparent", 1));
    run_dec<float, decltype(st), 2>(st);
#elif IIR_SC == 17
    hb_dec<double, 4, 4, true> st;
    st.init(nl("transparent", 1));
    run_dec<double, decltype(st), 2>(st);
#elif IIR_SC == 18
    hb_dec_q15<2, 2, true> st;
    st.init(nl(eco, 1));
    run_dec<std::int16_t, decltype(st), 2>(st);
#elif IIR_SC == 19
    hb_dec_q31<2, 2, true> st;
    st.init(nl(eco, 1));
    run_dec<std::int32_t, decltype(st), 2>(st);
#elif IIR_SC == 42
    hb_dec<float, 2, 2, false, true> st;
    st.init(nl(eco, 1));
    run_dec<float, decltype(st), 2>(st);
#elif IIR_SC == 43
    hb_dec_offset<float, 2, 2> st;
    st.init(nl(eco, 1));
    run_dec<float, decltype(st), 2>(st);
#elif IIR_SC >= 44 && IIR_SC <= 47
    // today's FIR half-band, rational's own basic_stage at economy, mono, in
    // the same harness: 44 / 45 down by 2 in float / Q15, 46 / 47 up by 2
    namespace rat         = tap::sr::rational;
    using S               = std::conditional_t<IIR_SC % 2 == 0, float, std::int16_t>;
    constexpr bool k_down = IIR_SC < 46;
    using R               = std::conditional_t<k_down, rat::down_2, rat::up_2>;
    struct fir {
        rat::basic_stage<S, 1, R> st{rat::profile::economy()};
        [[gnu::noinline]] void    process(const S* in, S* out, int n) noexcept {
            st.process(in, static_cast<std::size_t>(k_down ? 2 * n : n), out);
        }
    } st;
    if constexpr (k_down)
        run_dec<S, fir, 2>(st);
    else
        run_int<S>(st);
#elif IIR_SC == 50 && defined(IIR_HAVE_FOURPATH)
    // host check (not counted): tones through economy's four-path stage at D = 2 and through the
    // half-band pair it replaces, the steady-state output amplitude in dB, to compare with
    // python/iir_design.py's responses (README section 9)
    for (double f : {0.01, 0.04, 0.2, 0.25, 0.28, 0.48, 0.5 - 1e-3}) {
        fp_dec<double, 2, 2, 1, 1> fp;
        fp.init(k_fp_economy_2);
        hb_dec<double, 1, 1> h4, h2;
        h4.init(nl(eco, 4));
        h2.init(nl(eco, 2));
        constexpr int n = 4096;
        static double in[4 * n], o1[n], m[2 * n], o2[n];
        for (int i = 0; i < 4 * n; ++i)
            in[i] = std::cos(2 * M_PI * f * i);
        fp.process(in, o1, n);
        h4.process(in, m, 2 * n);
        h2.process(m, o2, n);
        double a1 = 0, a2 = 0;
        for (int i = n / 2; i < n; ++i) {
            a1 = std::max(a1, std::fabs(o1[i]));
            a2 = std::max(a2, std::fabs(o2[i]));
        }
        std::printf("f = %.4f of the input rate: four-path %.2f dB, half-band pair %.2f dB\n", f, 20 * std::log10(a1),
                    20 * std::log10(a2));
    }
#elif IIR_SC == 40
    nothing st;
    run_guarded<float, nothing, false>(st, false);
#elif IIR_SC == 41
    hb_dec<float, 2, 2> st;
    st.init(nl(eco, 1));
    run_guarded<float, decltype(st), false>(st, false);
#elif IIR_SC >= 20 && IIR_SC < 40 && defined(IIR_HAVE_FOURPATH)
#include "cascades.inc"
#else
#error "unknown IIR_SC"
#endif
    std::printf("IIR_WORKLOAD_DONE ok=1 sink=%g\n", static_cast<double>(g_sink));
    return 0;
}
