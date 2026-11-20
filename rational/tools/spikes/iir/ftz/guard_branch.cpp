// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0: moved from the plan's scratch programs, the code unchanged. The source of
// IIR_PLAN I9's table of the unconditional and conditional guards on x86-64;
// re-run in this spike, outputs/ftz_host.txt.
// The guard's two forms on x86-64, per call: unconditional (read, set,
// restore: two MXCSR writes) and conditional (read, compare; write and
// restore only when FTZ|DAZ are not already set). Each runs with the
// caller's FTZ|DAZ off and on. Median of 15 runs of 4M calls; indicative.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include <xmmintrin.h>
constexpr unsigned k_bits = 0x8040u;
struct unconditional {
    unsigned saved;
    unconditional() noexcept
        : saved(_mm_getcsr()) {
        _mm_setcsr(saved | k_bits);
    }
    ~unconditional() { _mm_setcsr(saved); }
};
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
volatile float sink = 1.0f;
template <class G>
__attribute__((noinline)) void call() {
    G g;
    sink = sink * 1.0000001f;
}
__attribute__((noinline)) void none() {
    sink = sink * 1.0000001f;
}
template <class F>
double median_ns(F f) {
    std::vector<double> r;
    for (int k = 0; k < 15; ++k) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 4000000; ++i)
            f();
        r.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 4e6);
    }
    std::sort(r.begin(), r.end());
    return r[7];
}
int main() {
    const unsigned base = _mm_getcsr();
    for (bool host_ftz : {false, true}) {
        _mm_setcsr(host_ftz ? (base | k_bits) : (base & ~k_bits));
        const double n = median_ns(none), u = median_ns(call<unconditional>), c = median_ns(call<conditional>);
        std::printf("caller's FTZ|DAZ %s: no guard %.2f ns | unconditional +%.2f ns | conditional +%.2f ns per call\n",
                    host_ftz ? "on " : "off", n, u - n, c - n);
    }
    _mm_setcsr(base);
}
