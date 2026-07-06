#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.hpp"

namespace {

    // After every (re)fill the converter ramps in over kFadeFrames=64 frames so
    // dropout recovery does not click. With a DC input the ramp is directly
    // observable: the first produced frame is strongly attenuated and the
    // output reaches the full DC value once the ramp has passed.
    TEST(Fade, OutputRampsAfterFill) {
        srt::Config cfg;
        cfg.channels = 1;
        srt::AsyncSampleRateConverter asrc(cfg);

        std::vector<float> in(32, 0.5f);
        std::vector<float> out(32);
        std::vector<float> made;
        for (int it = 0; it < 400 && made.size() < 200; ++it) {
            asrc.push(in.data(), in.size());
            const std::size_t n = asrc.pull(out.data(), out.size());
            for (std::size_t k = 0; k < n; ++k)
                made.push_back(out[k]);
        }
        ASSERT_GE(made.size(), 200u);

        EXPECT_LT(std::abs(made[0]), 0.1f) << "first frame should be attenuated";
        for (std::size_t k = 1; k < 64; ++k)
            EXPECT_GE(made[k] + 1e-6f, made[k - 1]) << "ramp must be monotonic at " << k;
        EXPECT_NEAR(made[80], 0.5f, 0.01f) << "full level after the ramp";
    }

} // namespace
