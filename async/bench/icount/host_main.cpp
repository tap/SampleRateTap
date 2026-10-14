// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The host ratchet's workloads (the roadmap's A8; scripts/icount.py --target
// host): the float pipeline as a host runs it, duplex push()/pull() in
// 128-frame blocks at the default profile, counted under callgrind. At 12
// channels the converter takes the channel-parallel frame-major path that
// only host builds compile (TAP_DSP_CHANNEL_PARALLEL), which no QEMU leg
// executes; at 2 it takes the planar path the embedded legs count.
//
// Only tap_sr_host_counted() is counted (callgrind --toggle-collect): the
// construction, the input fixture and its libm sin() calls run before it,
// the checksum and the marker line after, so the count is the streaming
// loop alone and the dynamic loader, libc start-up and the environment
// never reach it.
//
// TAP_SR_ASYNC_HC_CH: channel count (2 or 12)
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

#include "tap/sr/async/converter.h"

#ifndef TAP_SR_ASYNC_HC_CH
#define TAP_SR_ASYNC_HC_CH 2
#endif

namespace {

    constexpr std::size_t k_ch     = TAP_SR_ASYNC_HC_CH;
    constexpr std::size_t k_block  = 128;
    constexpr std::size_t k_blocks = 2 * 48000 / k_block; // 2 s of virtual audio

} // namespace

/// The counted region: every block pushed and pulled, the input cycled.
extern "C" [[gnu::noinline]] double tap_sr_host_counted(tap::sr::async::converter& c, const float* input,
                                                        std::size_t input_frames, float* out) {
    double      sink = 0.0;
    std::size_t off  = 0;
    for (std::size_t b = 0; b < k_blocks; ++b) {
        c.push(input + off * k_ch, k_block);
        c.pull(out, k_block);
        off = off + 2 * k_block > input_frames ? 0 : off + k_block;
        sink += static_cast<double>(out[0]) + static_cast<double>(out[k_block * k_ch - 1]);
    }
    return sink;
}

int main() {
    tap::sr::async::config cfg;
    cfg.channels = k_ch;
    // The setpoint must exceed the pull block (the README's latency notes);
    // two blocks, as the host benchmark (bench/bench_converter.cpp) sets it.
    cfg.target_latency_frames = 2 * k_block;
    tap::sr::async::converter c(cfg);

    constexpr std::size_t k_input_frames = 12000; // 0.25 s, cycled
    std::vector<float>    input(k_input_frames * k_ch);
    for (std::size_t i = 0; i < k_input_frames; ++i) {
        for (std::size_t ch = 0; ch < k_ch; ++ch) {
            const double f = 997.0 + 101.0 * static_cast<double>(ch);
            input[i * k_ch + ch] =
                static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / 48000.0));
        }
    }
    std::vector<float> out(k_block * k_ch);

    double     checksum = tap_sr_host_counted(c, input.data(), k_input_frames, out.data());
    const auto st       = c.status();
    if (st.underruns != 0 || st.resyncs != 0) {
        checksum = std::numeric_limits<double>::quiet_NaN();
    }
    const bool ok = checksum == checksum; // NaN check
    std::printf("SRT_ICOUNT_DONE ok=%d checksum=%.17g\n", ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
