// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0: moved from the plan's scratch programs, the code unchanged. The source of
// IIR_PLAN I9's Cortex-M listing (vmrs, orr, vmsr, vmsr); flush_guard.h is the
// guard as v8 specifies it, counted by ../icount.
// The guard on Arm: save the FP status/control register, set FZ (bit 24),
// run the block, restore. Freestanding, for the instruction count only.
extern "C" void work(float*, unsigned) noexcept;
#if defined(__aarch64__)
static inline unsigned long get_fpcr() {
    unsigned long v;
    __asm__ volatile("mrs %0, fpcr" : "=r"(v));
    return v;
}
static inline void set_fpcr(unsigned long v) {
    __asm__ volatile("msr fpcr, %0" : : "r"(v));
}
#else
static inline unsigned get_fpcr() {
    unsigned v;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(v));
    return v;
}
static inline void set_fpcr(unsigned v) {
    __asm__ volatile("vmsr fpscr, %0" : : "r"(v));
}
#endif
extern "C" void guarded(float* x, unsigned n) noexcept {
    const auto saved = get_fpcr();
    set_fpcr(saved | (1ul << 24));
    work(x, n);
    set_fpcr(saved);
}
extern "C" void plain(float* x, unsigned n) noexcept {
    work(x, n);
}
