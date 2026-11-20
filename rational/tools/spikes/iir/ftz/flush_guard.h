// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0 spike (scratch, not shipped): the scoped flush-to-zero guard of
// IIR_PLAN I9 as v8 specifies it, to compile and count on every target.
// - The mechanism is chosen per target and sample type at compile time: the
//   guard where the control bit governs that type's arithmetic, the
//   per-section flush (kernels.h, flush::per_section) everywhere else.
// - It reads the control register and touches it only when the bits it
//   needs are clear; then it sets them and on exit restores only those bits,
//   so the call's exception flags survive in both host states.
// - Compiler builtins, no CMSIS: __builtin_arm_get_fpscr /
//   __builtin_arm_set_fpscr (GCC, Clang) on 32-bit Arm with an FPU,
//   _mm_getcsr / _mm_setcsr on x86-64, mrs / msr FPCR on AArch64.
// - Hexagon: USR has no flush-to-zero field (README, Hexagon), and none on a
//   soft-float target (Cortex-M4 without an FPU), so the guard is empty there.
#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace iir_spike {

#if defined(__x86_64__) || defined(_M_X64)
    using fpcr_t                         = unsigned;
    inline constexpr fpcr_t k_flush_bits = 0x8040u; // MXCSR FTZ (15) and DAZ (6)
    inline fpcr_t           read_fpcr() noexcept {
        return _mm_getcsr();
    }
    inline void write_fpcr(fpcr_t v) noexcept {
        _mm_setcsr(v);
    }
    inline constexpr bool k_hw_float = true, k_hw_double = true;
#elif defined(__aarch64__)
    using fpcr_t                         = std::uint64_t;
    inline constexpr fpcr_t k_flush_bits = fpcr_t{1} << 24; // FPCR.FZ
    inline fpcr_t           read_fpcr() noexcept {
        fpcr_t v;
        __asm__ volatile("mrs %0, fpcr" : "=r"(v));
        return v;
    }
    inline void write_fpcr(fpcr_t v) noexcept {
        __asm__ volatile("msr fpcr, %0" : : "r"(v));
    }
    inline constexpr bool k_hw_float = true, k_hw_double = true;
#elif defined(__arm__) && defined(__ARM_FP)
    using fpcr_t                         = unsigned;
    inline constexpr fpcr_t k_flush_bits = 1u << 24; // FPSCR.FZ
    inline fpcr_t           read_fpcr() noexcept {
        return __builtin_arm_get_fpscr();
    }
    inline void write_fpcr(fpcr_t v) noexcept {
        __builtin_arm_set_fpscr(v);
    }
    // __ARM_FP: bit 2 single precision, bit 3 double precision in hardware.
    inline constexpr bool k_hw_float = (__ARM_FP & 4) != 0, k_hw_double = (__ARM_FP & 8) != 0;
#else
    using fpcr_t                         = unsigned;
    inline constexpr fpcr_t k_flush_bits = 0;
    inline fpcr_t           read_fpcr() noexcept {
        return 0;
    }
    inline void           write_fpcr(fpcr_t) noexcept {}
    inline constexpr bool k_hw_float = false, k_hw_double = false;
#endif

    /// True where the control bit governs T's arithmetic (so the guard is the mechanism).
    template <class T>
    inline constexpr bool k_guard_governs = sizeof(T) == sizeof(float) ? k_hw_float : k_hw_double;

    /// The conditional guard: no write when the caller already has the bits.
    template <class T>
    struct flush_guard {
        fpcr_t saved   = 0;
        bool   changed = false;
        flush_guard() noexcept {
            if constexpr (k_guard_governs<T>) {
                saved   = read_fpcr();
                changed = (saved & k_flush_bits) != k_flush_bits;
                if (changed)
                    write_fpcr(saved | k_flush_bits);
            }
        }
        ~flush_guard() {
            if constexpr (k_guard_governs<T>) {
                if (changed)
                    write_fpcr((read_fpcr() & ~k_flush_bits) | (saved & k_flush_bits));
            }
        }
        flush_guard(const flush_guard&)            = delete;
        flush_guard& operator=(const flush_guard&) = delete;
    };

} // namespace iir_spike
