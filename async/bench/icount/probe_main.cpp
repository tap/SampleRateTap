// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The size and stack probes (README, "Footprint"; scripts/ceilings.py): one
// converter in one format at the default profile, stereo, streamed 32-frame
// blocks through push() and pull(). Bare-metal targets have no argv, so the
// format is chosen at compile time.
//
// TAP_SR_PROBE_TYPE:  0 = float, 1 = Q15, 2 = Q31
// TAP_SR_PROBE_STACK: 0 = the size probe (no stdio: the image is the
//                     startup, the converter and what it pulls in, measured
//                     for its .text and .rodata ceilings), 1 = the stack
//                     probe (each call's high-water under QEMU, printed)
//
// Each measured call is a noinline function of its own, so the static
// analysis has one root per call (probe_push, probe_pull) and the painted
// high-water starts where that root's frame does.
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "tap/sr/async/converter.h"

#if TAP_SR_PROBE_STACK
#include <algorithm>
#include <cstdio>

#include "stack_meter.h"
#endif

#ifndef TAP_SR_PROBE_TYPE
#define TAP_SR_PROBE_TYPE 0
#endif

namespace {

#if TAP_SR_PROBE_TYPE == 0
    using sample = float;
#elif TAP_SR_PROBE_TYPE == 1
    using sample = std::int16_t;
#else
    using sample = std::int32_t;
#endif
    using converter = tap::sr::async::basic_converter<sample>;

    constexpr std::size_t k_ch     = 2;
    constexpr std::size_t k_block  = 32;
    constexpr std::size_t k_blocks = 1500; // 1 s at 48 kHz: priming, lock and steady state

    sample g_in[k_block * k_ch];
    sample g_out[k_block * k_ch];

    // A deterministic triangle, no libm: the size probe holds the converter
    // and the runtime, not the fixture's sin().
    template <typename S>
    S triangle(std::size_t i) {
        const auto v = static_cast<int>(i % 32) - 16;
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v) / static_cast<S>(32);
        }
        else {
            return static_cast<S>(v * 1024);
        }
    }

    [[gnu::noinline]] std::size_t probe_push(converter& c, const sample* in) noexcept {
        return c.push(in, k_block);
    }
    [[gnu::noinline]] std::size_t probe_pull(converter& c, sample* out) noexcept {
        return c.pull(out, k_block);
    }

} // namespace

int main() {
    tap::sr::async::config cfg;
    cfg.channels = k_ch;
    converter c(cfg);
    for (std::size_t i = 0; i < k_block * k_ch; ++i) {
        g_in[i] = triangle<sample>(i);
    }
    std::size_t made = 0;
#if TAP_SR_PROBE_STACK
    std::size_t push_hw = 0;
    std::size_t pull_hw = 0;
    for (std::size_t b = 0; b < k_blocks; ++b) {
        push_hw = std::max(push_hw, tap::sr::probe::high_water([&] { made += probe_push(c, g_in); }));
        pull_hw = std::max(pull_hw, tap::sr::probe::high_water([&] { made += probe_pull(c, g_out); }));
    }
    const bool ok = push_hw < tap::sr::probe::k_paint_bytes && pull_hw < tap::sr::probe::k_paint_bytes
                    && made > k_blocks * k_block && c.status().underruns == 0;
    std::printf("TAP_SR_STACK push %u\n", static_cast<unsigned>(push_hw));
    std::printf("TAP_SR_STACK pull %u\n", static_cast<unsigned>(pull_hw));
    std::printf("TAP_SR_PROBE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
#else
    for (std::size_t b = 0; b < k_blocks; ++b) {
        made += probe_push(c, g_in);
        made += probe_pull(c, g_out);
    }
    // The results reach the exit code, so --gc-sections keeps what is measured.
    return static_cast<int>((made + static_cast<std::size_t>(g_out[0] != g_out[1])) & 1U);
#endif
}
