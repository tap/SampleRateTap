// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0: moved from the audit's scratch programs (dsp-15), the code unchanged: the
// conditional guard restoring the whole MXCSR leaves the caller's flags differently
// in the two host states; outputs/ftz_host.txt. flush_guard.h restores only the bits.
#include <cstdio>
#include <initializer_list>

#include <xmmintrin.h>
constexpr unsigned k_bits = 0x8040u;
struct conditional {
    unsigned saved;
    bool     changed;
    conditional() noexcept
        : saved(_mm_getcsr())
        , changed((saved & k_bits) != k_bits) {
        if (changed)
            _mm_setcsr(saved | k_bits);
    }
    ~conditional() {
        if (changed)
            _mm_setcsr(saved);
    }
};
__attribute__((noinline)) float stage(volatile float* x) {
    conditional g;
    float       y = 0;
    for (int i = 0; i < 8; ++i)
        y = 0.8134f * (x[i] - y);
    return y;
}
int main() {
    volatile float x[8] = {0.1f, 0.3f, 0.7f, 0.2f, 0.9f, 0.4f, 0.6f, 0.5f};
    for (bool host : {false, true}) {
        unsigned base = (_mm_getcsr() & ~0x3Fu) & ~k_bits;
        if (host)
            base |= k_bits; // clear sticky flags, set host's FTZ|DAZ
        _mm_setcsr(base);
        unsigned before = _mm_getcsr();
        stage(x);
        unsigned after = _mm_getcsr();
        std::printf("host FTZ|DAZ %s: MXCSR before 0x%04x after 0x%04x -> whole register %s, control bits %s\n",
                    host ? "on " : "off", before, after, before == after ? "equal" : "DIFFERS",
                    ((before ^ after) & ~0x3Fu) ? "differ" : "equal");
    }
}
