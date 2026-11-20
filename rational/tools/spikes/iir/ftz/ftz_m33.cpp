// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0: moved from the audit's scratch programs (dsp-4, dsp-14), the code unchanged:
// a section's zero-input decay on the M33 with FPSCR.FZ off and on. ftz_check.cpp
// repeats it on every target.
#include <cstdint>
#include <cstdio>
static inline uint32_t get_fpscr() {
    uint32_t r;
    __asm volatile("vmrs %0, fpscr" : "=r"(r));
    return r;
}
static inline void set_fpscr(uint32_t v) {
    __asm volatile("vmsr fpscr, %0" ::"r"(v) : "memory");
}
template <class T>
void run(const char* nm, T a) {
    volatile T av = a;        // keep it a runtime multiply
    T          y = 1, x1 = 0; // one-multiplier allpass, zero input: y = a*(0 - y) + 0
    int        n = 0;
    for (; n < 20000 && y != 0; ++n) {
        T x  = 0;
        T yn = av * (x - y) + x1;
        x1   = x;
        y    = yn;
    }
    std::printf("%s a=%.4f: %s after %d samples, |y|=%g\n", nm, (double)a, y == 0 ? "zero" : "NOT zero", n,
                (double)(y < 0 ? -y : y));
}
int main() {
    std::printf("FPSCR before: 0x%08lx\n", (unsigned long)get_fpscr());
    for (int ftz = 0; ftz < 2; ++ftz) {
        set_fpscr(ftz ? (get_fpscr() | (1u << 24)) : (get_fpscr() & ~(1u << 24)));
        std::printf("-- FPSCR.FZ=%d (0x%08lx)\n", ftz, (unsigned long)get_fpscr());
        run<float>("float ", 0.8133946666070333f);
        run<double>("double", 0.8133946666070333);
        run<float>("float ", 0.25221956029506937f);
        run<double>("double", 0.25221956029506937);
    }
    return 0;
}
