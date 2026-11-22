// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The host's flush-to-zero for the recursive stages' batteries
// (docs/IIR_PLAN.md I9): flushing subnormals is the host's job, so the tests
// set the bits a host sets on its audio thread (x86-64 MXCSR FTZ | DAZ,
// AArch64 FPCR.FZ, 32-bit Arm FPSCR.FZ; tap::dsp::k_flush_to_zero_bits) in
// their fixture's SetUp and restore the thread's register in TearDown, so
// the rest of the battery on the same thread is untouched. Writing the
// register is the tests' business only: no tap/dsp or tap/sr header ever
// does (tap/dsp/subnormals.h reads it, as a debug-build precondition).
#pragma once

#include <cstdint>

#include <gtest/gtest.h>

#include "tap/dsp/subnormals.h"

namespace tap::sr::rational::test {

    /// Writes the calling thread's floating-point control register, where
    /// the target has one (subnormals.h names the targets).
    inline void write_floating_point_control(std::uint64_t v) noexcept {
#if defined(TAP_DSP_FP_CONTROL_X86_64)
        _mm_setcsr(static_cast<unsigned>(v));
#elif defined(TAP_DSP_FP_CONTROL_AARCH64)
#if defined(_MSC_VER) && !defined(__clang__)
        _WriteStatusReg(ARM64_FPCR, static_cast<__int64>(v));
#else
        __asm__ volatile("msr fpcr, %0" : : "r"(v));
#endif
#elif defined(TAP_DSP_FP_CONTROL_AARCH32)
        __builtin_arm_set_fpscr(static_cast<unsigned>(v));
#else
        static_cast<void>(v);
#endif
    }

    /// Sets (or clears) the host's flush-to-zero bits; returns the register
    /// as it was.
    inline std::uint64_t set_flush_to_zero(bool on) noexcept {
        const std::uint64_t saved = tap::dsp::floating_point_control();
        write_floating_point_control(on ? (saved | tap::dsp::k_flush_to_zero_bits)
                                        : (saved & ~tap::dsp::k_flush_to_zero_bits));
        return saved;
    }

    /// The register's bits that are not cumulative exception flags (which an
    /// arithmetic call may legitimately raise): what "the stage never touches
    /// the environment" compares.
    inline constexpr std::uint64_t k_control_mask =
#if defined(TAP_DSP_FP_CONTROL_X86_64)
        ~std::uint64_t{0x3f}; // MXCSR flags IE DE ZE OE UE PE
#elif defined(TAP_DSP_FP_CONTROL_AARCH64)
        ~std::uint64_t{0}; // FPCR holds no flags (they are FPSR's)
#elif defined(TAP_DSP_FP_CONTROL_AARCH32)
        ~std::uint64_t{0xF800009Fu}; // FPSCR N Z C V QC and IDC IXC UFC OFC DZC IOC
#else
        ~std::uint64_t{0};
#endif

    /// The fixture of every recursive test that runs as a host would: the
    /// bits set in SetUp, the caller's register restored in TearDown.
    class flush_to_zero_fixture : public ::testing::Test {
      protected:
        void SetUp() override { m_saved = set_flush_to_zero(true); }
        void TearDown() override { write_floating_point_control(m_saved); }

      private:
        std::uint64_t m_saved = 0;
    };

    /// The same with the bits cleared: the honest limit's host.
    class no_flush_to_zero_fixture : public ::testing::Test {
      protected:
        void SetUp() override { m_saved = set_flush_to_zero(false); }
        void TearDown() override { write_floating_point_control(m_saved); }

      private:
        std::uint64_t m_saved = 0;
    };

} // namespace tap::sr::rational::test
