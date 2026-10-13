// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The size and stack probes (README, "Footprint"; scripts/ceilings.py): the
// by-2 pair in one format at the default profile (economy), stereo, an up_2
// stage and a down_2 stage each streaming 32-frame blocks through process(). Bare-metal targets have no argv, so the
// format is chosen at compile time.
//
// TAP_SR_PROBE_TYPE:  0 = float, 1 = Q15, 2 = Q31
// TAP_SR_PROBE_STACK: 0 = the size probe (no stdio: the image is the
//                     startup, both stages and what they pull in,
//                     measured for its .text and .rodata ceilings), 1 = the
//                     stack probe (each call's high-water under QEMU, printed)
//
// Each measured call is a noinline function of its own, so the static
// analysis has one root per call (probe_up, probe_down) and the painted
// high-water starts where that root's frame does.
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "tap/sr/rational/stage.h"

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
    using up_stage   = tap::sr::rational::basic_stage<sample, tap::sr::rational::up_2>;
    using down_stage = tap::sr::rational::basic_stage<sample, tap::sr::rational::down_2>;

    constexpr std::size_t k_ch     = 2;
    constexpr std::size_t k_block  = 32;
    constexpr std::size_t k_blocks = 1500;
    constexpr std::size_t k_room   = 2 * k_block + 2; // outputs_for(n) <= floor(n L / M) + 2

    sample g_in[k_block * k_ch];
    sample g_out[k_room * k_ch];

    // A deterministic triangle, no libm: the size probe holds the stages
    // and the runtime, not a fixture's sin().
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

    [[gnu::noinline]] std::size_t probe_up(up_stage& c, const sample* in, sample* out) noexcept {
        return c.process(in, k_block, out);
    }
    [[gnu::noinline]] std::size_t probe_down(down_stage& c, const sample* in, sample* out) noexcept {
        return c.process(in, k_block, out);
    }

} // namespace

int main() {
    up_stage   up(k_ch);
    down_stage down(k_ch);
    for (std::size_t i = 0; i < k_block * k_ch; ++i) {
        g_in[i] = triangle<sample>(i);
    }
    std::size_t made = 0;
#if TAP_SR_PROBE_STACK
    std::size_t up_hw   = 0;
    std::size_t down_hw = 0;
    for (std::size_t b = 0; b < k_blocks; ++b) {
        up_hw   = std::max(up_hw, tap::sr::probe::high_water([&] { made += probe_up(up, g_in, g_out); }));
        down_hw = std::max(down_hw, tap::sr::probe::high_water([&] { made += probe_down(down, g_in, g_out); }));
    }
    const bool ok =
        up_hw < tap::sr::probe::k_paint_bytes && down_hw < tap::sr::probe::k_paint_bytes && made > k_blocks * k_block;
    std::printf("TAP_SR_STACK up %u\n", static_cast<unsigned>(up_hw));
    std::printf("TAP_SR_STACK down %u\n", static_cast<unsigned>(down_hw));
    std::printf("TAP_SR_PROBE_DONE ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
#else
    for (std::size_t b = 0; b < k_blocks; ++b) {
        made += probe_up(up, g_in, g_out);
        made += probe_down(down, g_in, g_out);
    }
    // The results reach the exit code, so --gc-sections keeps what is measured.
    return static_cast<int>((made + static_cast<std::size_t>(g_out[0] != g_out[1])) & 1U);
#endif
}
