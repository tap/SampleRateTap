// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The host ratchet's workloads (the roadmap's A8; scripts/icount.py --target
// host): the float stage as a host runs it, process() in 128-frame blocks at
// the default profile (economy), up and down by 2, counted under callgrind
// at 2 and 12 channels.
//
// Only tap_sr_host_counted() is counted (callgrind --toggle-collect): the
// construction, the input fixture and its libm sin() calls run before it,
// the checksum and the marker line after, so the count is the streaming
// loop alone and the dynamic loader, libc start-up and the environment
// never reach it.
//
// TAP_SR_RATIONAL_HC_DIR: 0 = up 2, 1 = down 2
// TAP_SR_RATIONAL_HC_CH:  channel count (2 or 12)
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <vector>

#include "tap/sr/rational/rational.h"

#ifndef TAP_SR_RATIONAL_HC_DIR
#define TAP_SR_RATIONAL_HC_DIR 0
#endif
#ifndef TAP_SR_RATIONAL_HC_CH
#define TAP_SR_RATIONAL_HC_CH 2
#endif

namespace {

#if TAP_SR_RATIONAL_HC_DIR == 0
    using converter = tap::sr::rational::converter<tap::sr::rational::up_2>;
#else
    using converter = tap::sr::rational::converter<tap::sr::rational::down_2>;
#endif

    constexpr std::size_t k_ch     = TAP_SR_RATIONAL_HC_CH;
    constexpr std::size_t k_block  = 128;
    constexpr std::size_t k_blocks = 2 * 48000 / k_block; // 2 s of input

} // namespace

/// The counted region: every block through process(), the input cycled.
extern "C" [[gnu::noinline]] double tap_sr_host_counted(converter& c, const float* input, std::size_t input_frames,
                                                        float* out) {
    double      sink = 0.0;
    std::size_t off  = 0;
    for (std::size_t b = 0; b < k_blocks; ++b) {
        const std::size_t made = c.process(input + off * k_ch, k_block, out);
        off                    = off + 2 * k_block > input_frames ? 0 : off + k_block;
        sink += static_cast<double>(out[0]) + static_cast<double>(out[made * k_ch - 1]);
    }
    return sink;
}

int main() {
    converter c(k_ch);

    constexpr std::size_t k_input_frames = 12000; // cycled
    std::vector<float>    input(k_input_frames * k_ch);
    for (std::size_t i = 0; i < k_input_frames; ++i) {
        for (std::size_t ch = 0; ch < k_ch; ++ch) {
            const double f = 997.0 + 101.0 * static_cast<double>(ch);
            input[i * k_ch + ch] =
                static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / 48000.0));
        }
    }
    std::vector<float> out(3 * k_block * k_ch); // outputs_for(128) <= 257

    const double checksum = tap_sr_host_counted(c, input.data(), k_input_frames, out.data());
    const bool   ok       = checksum == checksum; // NaN check
    std::printf("RATIONAL_ICOUNT_DONE ok=%d checksum=%.17g\n", ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
