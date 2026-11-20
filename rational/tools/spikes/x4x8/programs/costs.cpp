// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (x4x8/costs.cpp); see ../README.md.
// Scratch measurement: MACs and exact latencies of the x4 / x8 stages and
// chains rational ships today, and of the single 4th-band stage decision 6
// keeps out, at economy and transparent.
#include <cstdio>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
using tap::dsp::exact_ratio;

template <class T>
void row(const char* name, const profile& p, unsigned D, bool down) {
    T           s(p);
    exact_ratio lat = s.latency_output_frames();
    // full-rate samples: a decimator's latency is in low-rate frames (x D)
    exact_ratio full     = down ? lat * exact_ratio{D, 1} : lat;
    double      per_full = down ? s.macs_per_output() / D : s.macs_per_output();
    std::printf(
        "  %-28s MAC/out %7.2f  MAC/full-rate sample %6.2f  latency %llu/%llu out = %llu/%llu full-rate (%.3f)\n", name,
        s.macs_per_output(), per_full, (unsigned long long)lat.num, (unsigned long long)lat.den,
        (unsigned long long)full.num, (unsigned long long)full.den, full.value());
}

int main() {
    const profile ps[2]    = {profile::economy(), profile::transparent()};
    const char*   names[2] = {"economy", "transparent"};
    for (int i = 0; i < 2; ++i) {
        std::printf("%s\n", names[i]);
        row<converter<1, ratio<1, 4>>>("down 4 (one 4th-band)", ps[i], 4, true);
        row<chain<1, down_2, down_2>>("down 2 . down 2", ps[i], 4, true);
        row<converter<1, ratio<4, 1>>>("up 4 (one 4th-band)", ps[i], 4, false);
        row<chain<1, up_2, up_2>>("up 2 . up 2", ps[i], 4, false);
        row<converter<1, down_8>>("down 8 (one 8th-band)", ps[i], 8, true);
        row<chain<1, down_3, ratio_3_4, down_2>>("down 3 . 3/4 . down 2", ps[i], 8, true);
        row<chain<1, down_2, down_2, down_2>>("down 2 . down 2 . down 2", ps[i], 8, true);
        row<chain<1, down_8>>("chain<down 8>", ps[i], 8, true);
        row<converter<1, up_8>>("up 8 (one 8th-band)", ps[i], 8, false);
        row<chain<1, up_2, ratio_4_3, up_3>>("up 2 . 4/3 . up 3", ps[i], 8, false);
        row<chain<1, up_2, up_2, up_2>>("up 2 . up 2 . up 2", ps[i], 8, false);
    }
}
