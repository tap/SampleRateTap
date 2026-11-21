// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Interleaved against planar (informational, never a gate): one engine's
// float converter at Ch channels, either as one converter<Ch> over
// interleaved frames (the engines' API, _i) or as Ch converter<1>s over
// contiguous channel buffers (what a planar host would run, _p). Counted by
// scripts/layout.py on the host (callgrind) and on the QEMU targets.
//
// TAP_SR_LY_ENGINE  0 = async (push/pull 1:1, target latency 2 blocks, as
//                   async/bench/icount/host_main.cpp), 1 = bridge
//                   (44.1 -> 48 kHz, profile 0), 2 = rational (up2)
// TAP_SR_LY_CH      1, 2, 6 (5.1) or 12 (7.1.4)
// TAP_SR_LY_PLANAR  0 = interleaved, 1 = planar
//
// Only tap_sr_layout_block() is counted: one 128-frame block through every
// channel. On the host callgrind toggles collection on it (--toggle-collect),
// so its calls sum; on QEMU each call is bracketed by tools/probe's
// call markers and the plugin's TAP_SR_CALL_MAX total= field is that sum.
// Construction (Ch designs in the planar layout, one in the interleaved),
// the input fixture and the checksum stay outside, so the count is the
// streaming work alone and the two layouts are comparable.
//
// The checksum folds each channel's output in frame order into its own
// FNV-1a-64, then the channel hashes in order: the two layouts of a pair
// print the same checksum exactly when their outputs are bit-identical.
//
// Async planar is measured, not endorsed: each converter<1> runs its own
// clock servo, which a deployment must not do (async's C ABI refuses that
// fan-out); driven identically here, the servos stay in step.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

#include "call_marker.h"

#if TAP_SR_LY_ENGINE == 0
#include "tap/sr/async/converter.h"
#elif TAP_SR_LY_ENGINE == 1
#include "tap/sr/bridge/converter.h"
#else
#include "tap/sr/rational/rational.h"
#endif

#ifndef TAP_SR_LY_CH
#define TAP_SR_LY_CH 2
#endif
#ifndef TAP_SR_LY_PLANAR
#define TAP_SR_LY_PLANAR 0
#endif

namespace {

    constexpr std::size_t k_ch      = TAP_SR_LY_CH;
    constexpr std::size_t k_block   = 128;
    constexpr std::size_t k_blocks  = 375; // 48,000 frames
    constexpr std::size_t k_out_max = 3 * k_block;

    template <std::size_t Ch>
    struct engine {
#if TAP_SR_LY_ENGINE == 0
        tap::sr::async::converter<Ch> c{[] {
            tap::sr::async::config cfg;
            cfg.target_latency_frames = 2 * k_block;
            return cfg;
        }()};
        std::size_t                   process(const float* in, std::size_t n, float* out) noexcept {
            c.push(in, n);
            c.pull(out, n);
            return n;
        }
#elif TAP_SR_LY_ENGINE == 1
        tap::sr::bridge::basic_converter<float, Ch, tap::sr::bridge::direction::up_to_48k, 0> c;
        std::size_t process(const float* in, std::size_t n, float* out) noexcept { return c.process(in, n, out); }
#else
        tap::sr::rational::converter<Ch, tap::sr::rational::up2> c;
        std::size_t process(const float* in, std::size_t n, float* out) noexcept { return c.process(in, n, out); }
#endif
    };

    struct state {
#if TAP_SR_LY_PLANAR == 0
        std::unique_ptr<engine<k_ch>> e   = std::make_unique<engine<k_ch>>();
        std::vector<float>            in  = std::vector<float>(k_block * k_blocks * k_ch);
        std::vector<float>            out = std::vector<float>(k_out_max * k_ch);
#else
        std::array<std::unique_ptr<engine<1>>, k_ch> e;
        std::array<std::vector<float>, k_ch>         in;
        std::array<std::vector<float>, k_ch>         out;
#endif
        std::size_t made = 0;
    };

} // namespace

/// The counted region: one block through every channel.
extern "C" [[gnu::noinline]] void tap_sr_layout_block(state& s, std::size_t b) {
#if TAP_SR_LY_PLANAR == 0
    s.made = s.e->process(s.in.data() + b * k_block * k_ch, k_block, s.out.data());
#else
    for (std::size_t c = 0; c < k_ch; ++c) {
        s.made = s.e[c]->process(s.in[c].data() + b * k_block, k_block, s.out[c].data());
    }
#endif
}

int main() {
    state s;
#if TAP_SR_LY_PLANAR == 1
    for (std::size_t c = 0; c < k_ch; ++c) {
        s.e[c] = std::make_unique<engine<1>>();
        s.in[c].resize(k_block * k_blocks);
        s.out[c].resize(k_out_max);
    }
#endif
    for (std::size_t i = 0; i < k_block * k_blocks; ++i) {
        for (std::size_t c = 0; c < k_ch; ++c) {
            const double f = 997.0 + 101.0 * static_cast<double>(c);
            const auto   v =
                static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / 48000.0));
#if TAP_SR_LY_PLANAR == 0
            s.in[i * k_ch + c] = v;
#else
            s.in[c][i] = v;
#endif
        }
    }

    std::array<std::uint64_t, k_ch> h{};
    h.fill(0xcbf29ce484222325ull);
    bool nonzero = false;
    for (std::size_t b = 0; b < k_blocks; ++b) {
        tap_sr_call_begin();
        tap_sr_layout_block(s, b);
        tap_sr_call_end();
        for (std::size_t i = 0; i < s.made; ++i) {
            for (std::size_t c = 0; c < k_ch; ++c) {
#if TAP_SR_LY_PLANAR == 0
                const float v = s.out[i * k_ch + c];
#else
                const float v = s.out[c][i];
#endif
                std::uint32_t bits = 0;
                std::memcpy(&bits, &v, sizeof bits);
                h[c]    = (h[c] ^ bits) * 0x100000001b3ull;
                nonzero = nonzero || v != 0.0f;
            }
        }
    }
    std::uint64_t all = 0xcbf29ce484222325ull;
    for (const std::uint64_t v : h) {
        all = (all ^ v) * 0x100000001b3ull;
    }
    std::printf("TAP_SR_LAYOUT_DONE ok=%d checksum=0x%016llx\n", nonzero ? 1 : 0, static_cast<unsigned long long>(all));
    return nonzero ? 0 : 1;
}
