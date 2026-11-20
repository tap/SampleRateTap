// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (x4x8/family.cpp); see ../README.md.
// Scratch: the two family-specific 1/8 and 8/1 chains cost the same in any
// family (the chain types carry no rate), so compare them directly.
#include <cstdio>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
template <class T>
void row(const char* n, const profile& p) {
    T    s(p);
    auto l = s.latency_output_frames();
    std::printf("  %-34s MAC/out %7.2f  latency %llu/%llu out frames\n", n, s.macs_per_output(),
                (unsigned long long)l.num, (unsigned long long)l.den);
}
int main() {
    for (auto [name, p] :
         {std::pair{"economy", profile::economy()}, std::pair{"super_economy", profile::super_economy()},
          std::pair{"balanced", profile::balanced()}, std::pair{"transparent", profile::transparent()}}) {
        std::printf("%s\n", name);
        row<down_3_ratio_3_4_down_2<float, 1>>("down_3_ratio_3_4_down_2 (48 fam.)", p);
        row<ratio_3_8_down_3<float, 1>>("ratio_3_8_down_3 (44.1 fam.)", p);
        row<chain<1, down_2, down_2, down_2>>("down_2 x3", p);
        row<converter<1, down_8>>("down_8", p);
        row<up_2_ratio_4_3_up_3<float, 1>>("up_2_ratio_4_3_up_3 (48 fam.)", p);
        row<up_3_ratio_8_3<float, 1>>("up_3_ratio_8_3 (44.1 fam.)", p);
        row<chain<1, up_2, up_2, up_2>>("up_2 x3", p);
        row<converter<1, up_8>>("up_8", p);
        row<down_2_down_2<float, 1>>("down_2_down_2", p);
        row<converter<1, ratio<1, 4>>>("ratio<1,4> (one stage)", p);
        row<up_2_up_2<float, 1>>("up_2_up_2", p);
        row<converter<1, ratio<4, 1>>>("ratio<4,1> (one stage)", p);
    }
}
