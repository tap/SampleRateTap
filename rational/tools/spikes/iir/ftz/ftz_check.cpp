// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0, go/no-go 5 and the FTZ rows (IIR_PLAN I9): on each target (x86-64
// host, Cortex-M4 soft-float, M4F, M33, M55 under qemu-system-arm, Hexagon
// under qemu-hexagon), which mechanism the guard picks per type, whether
// silence after signal decays to an exactly zero state with it, whether the
// caller's control bits survive a call in both host states (the plan's
// LeavesTheFloatingPointEnvironmentAsItFoundIt, compared without the
// cumulative flags), the value a section sticks at with no mechanism, and on
// Hexagon which USR bits a write keeps and whether any of them flushes.
// Built by ../icount/CMakeLists.txt; run by ../icount/run_targets.py.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "../cpp/kernels.h"
#include "flush_guard.h"

using namespace iir_spike;

namespace {

#if defined(__x86_64__)
    constexpr fpcr_t k_cumulative = 0x3Fu; // MXCSR exception flags
    const char*      k_target     = "x86-64";
#elif defined(__arm__) && defined(__ARM_FP)
    constexpr fpcr_t k_cumulative = 0xF800009Fu; // NZCV, QC, IDC, IXC, UFC, OFC, DZC, IOC
    const char*      k_target     = "Arm with an FPU";
#elif defined(__arm__)
    constexpr fpcr_t k_cumulative = 0;
    const char*      k_target     = "Arm soft-float";
#elif defined(__hexagon__)
    constexpr fpcr_t k_cumulative = 0;
    const char*      k_target     = "Hexagon";
#else
    constexpr fpcr_t k_cumulative = 0;
    const char*      k_target     = "other";
#endif

    template <class T>
    const char* mechanism() {
        return k_guard_governs<T> ? "guard (control bit)" : "per-section flush";
    }

    // One section, zero input from y = 1: the frames to an exact zero, or the value it sticks at.
    template <class T, flush F>
    void stick(const char* what, double a) {
        volatile T av = static_cast<T>(a);
        T          y = 1, x1 = 0;
        int        n = 0;
        for (; n < 40000 && y != T(0); ++n) {
            T yn = av * (T(0) - y) + x1;
            if constexpr (F == flush::per_section)
                yn = std::fabs(yn) < std::numeric_limits<T>::min() ? T(0) : yn;
            x1 = 0;
            y  = yn;
        }
        std::printf("  %-6s a=%.4f %-26s: %s after %d samples (|y| = %.3g = %.0f x the smallest subnormal)\n",
                    sizeof(T) == 4 ? "float" : "double", a, what, y == T(0) ? "zero" : "NOT zero", n,
                    static_cast<double>(std::fabs(y)),
                    y == T(0) ? 0.0 : static_cast<double>(std::fabs(y) / std::numeric_limits<T>::denorm_min()));
    }

    // The economy and transparent decimators: drive, then silence, with the
    // type's own mechanism; frames to an exactly zero state.
    template <class T>
    int silence(const char* profile) {
        constexpr flush          F = k_guard_governs<T> ? flush::none : flush::per_section;
        nonlinear_halfband<T, F> hb(find_nonlinear(profile, 1));
        flush_guard<T>           g;
        for (int n = 0; n < 2000; ++n)
            hb.decimate(static_cast<T>(std::sin(0.3 * n)), static_cast<T>(std::sin(0.3 * n + 0.15)));
        int z = 0;
        for (; z < 200000 && !hb.zero(); ++z)
            hb.decimate(T(0), T(0));
        return z;
    }

    template <class T>
    void environment() {
        if constexpr (!k_guard_governs<T>) {
            std::printf("  %-6s: no guard on this target (nothing to restore)\n", sizeof(T) == 4 ? "float" : "double");
        }
        else {
            for (bool host : {false, true}) {
                const fpcr_t base = read_fpcr();
                // the sticky flags cleared first, so the call's own flags show (dsp-15)
                write_fpcr((host ? (base | k_flush_bits) : (base & ~k_flush_bits)) & ~k_cumulative);
                const fpcr_t before = read_fpcr();
                silence<T>("economy");
                const fpcr_t after = read_fpcr();
                std::printf("  %-6s host bits %s: before 0x%08lx after 0x%08lx: control bits %s, whole register %s\n",
                            sizeof(T) == 4 ? "float" : "double", host ? "on " : "off",
                            static_cast<unsigned long>(before), static_cast<unsigned long>(after),
                            ((before ^ after) & ~k_cumulative) ? "DIFFER" : "equal",
                            before == after ? "equal" : "differs");
                write_fpcr(base);
            }
        }
    }

#if defined(__hexagon__)
    std::uint32_t read_usr() {
        std::uint32_t v;
        __asm__ volatile("%0 = usr" : "=r"(v));
        return v;
    }
    void write_usr(std::uint32_t v) {
        __asm__ volatile("usr = %0" : : "r"(v));
    }
    void hexagon_usr() {
        const std::uint32_t base = read_usr();
        std::printf("Hexagon USR at start: 0x%08lx\n", static_cast<unsigned long>(base));
        std::uint32_t kept = 0;
        for (int b = 0; b < 32; ++b) {
            if (b >= 25 && b <= 29)
                continue; // the FP exception enables: a trap on the next inexact op
            write_usr(base | (1u << b));
            if (read_usr() & (1u << b))
                kept |= 1u << b;
            write_usr(base);
        }
        std::printf("USR bits a write keeps (exception enables 25..29 not tried): 0x%08lx\n",
                    static_cast<unsigned long>(kept));
        // With every kept non-flag bit set, is a subnormal product flushed?
        const std::uint32_t try_bits = kept & ~0x3Fu & ~(3u << 8); // not the sticky flags or LPCFG
        write_usr(base | try_bits);
        volatile float  f = 1e-38f, fs = 0.25f;
        volatile double d = 1e-308, ds = 0.25;
        const float     pf = f * fs;
        const double    pd = d * ds;
        write_usr(base);
        std::printf("with USR | 0x%08lx: float 1e-38 * 0.25 = %.3g (%s), double 1e-308 * 0.25 = %.3g (%s)\n",
                    static_cast<unsigned long>(try_bits), static_cast<double>(pf),
                    pf == 0.0f ? "flushed" : "subnormal kept", pd, pd == 0.0 ? "flushed" : "subnormal kept");
    }
#endif

} // namespace

int main() {
    std::printf("target: %s; float: %s; double: %s\n", k_target, mechanism<float>(), mechanism<double>());
    std::printf("sticking with no mechanism and the control bit as the target starts:\n");
    for (double a : {0.8133946666070333, 0.9187887417920493}) {
        stick<float, flush::none>("no mechanism", a);
        stick<double, flush::none>("no mechanism", a);
        stick<float, flush::per_section>("per-section flush", a);
        stick<double, flush::per_section>("per-section flush", a);
    }
    if constexpr (k_guard_governs<float> || k_guard_governs<double>) {
        const fpcr_t base = read_fpcr();
        write_fpcr(base | k_flush_bits);
        std::printf("with the control bit set:\n");
        for (double a : {0.8133946666070333, 0.9187887417920493}) {
            stick<float, flush::none>("control bit", a);
            stick<double, flush::none>("control bit", a);
        }
        write_fpcr(base);
    }
    std::printf("silence after signal, each type's own mechanism (frames to an exactly zero state):\n");
    for (const char* p : {"economy", "transparent"})
        std::printf("  %-11s float %d, double %d\n", p, silence<float>(p), silence<double>(p));
    std::printf("the caller's environment after a guarded call:\n");
    environment<float>();
    environment<double>();
#if defined(__hexagon__)
    hexagon_usr();
#endif
    std::printf("IIR_FTZ_CHECK_DONE\n");
    return 0;
}
