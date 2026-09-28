// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// The bluetooth_bridge composition — the documented answer to "I need
// 44.1 <-> 48 across independent clocks" (a Bluetooth chip on its own
// crystal being the motivating case), and the reason neither package ever
// grows the other's scope:
//
//     RatioTap converts the NUMBER (44.1 <-> 48, exact rational, one clock).
//     SampleRateTap absorbs the CLOCK (near-unity ppm drift, servo).
//
// RatioTap is clock-agnostic — a pure sample-count transformer — so placing
// it on the Bluetooth side leaves the ASRC running at nominal 48 kHz on both
// faces, exactly its designed regime; the BT crystal's ppm offset passes
// through the fixed ratio unchanged (ppm is dimensionless).
//
//   receive:  BT codec 44.1k @ BT clock -> RatioTap up 44.1->48 -> asrc.push
//             ... asrc.pull @ local 48k clock
//
// This example runs the receive path against a deterministic two-clock
// simulation (+200 ppm Bluetooth crystal), waits for the servo to lock, and
// measures the recovered tone at the local clock: correct frequency, full
// amplitude, converter-grade SNR. Single-threaded for determinism; in a real
// deployment the push side lives on the BT thread and the pull side in the
// audio callback (both ends are noexcept and allocation-free).
//
// Build:  cmake -B build -DTAP_RATIO_BUILD_EXAMPLES=ON && cmake --build build
// Run:    ./build/examples/bluetooth_bridge

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "srt/asrc.h"
#include "tap/dsp/analysis/sine_analysis.h"
#include "tap/ratio/converter.h"

int main() {
    // The Bluetooth chip's crystal runs +200 ppm off the local clock.
    constexpr double k_bt_ppm  = 200.0;
    constexpr double k_tone_hz = 997.0;
    constexpr double k_amp     = 0.5;

    // Stage 1 (BT side): exact rational 44.1 -> 48, economy profile.
    tap::ratio::converter_to_48k ratio(1);

    // Stage 2 (clock boundary): near-unity ASRC at nominal 48 kHz.
    tap::samplerate::config cfg;
    cfg.channels = 1;
    tap::samplerate::async_sample_rate_converter asrc(cfg);

    // Deterministic two-clock simulation: for every local 48 kHz output
    // block we owe the BT domain dt * 44100 * (1 + ppm) input samples;
    // fractional sample debts carry across blocks.
    const double      bt_rate  = 44100.0 * (1.0 + k_bt_ppm * 1e-6);
    double            bt_debt  = 0.0;
    std::uint64_t     bt_index = 0;
    const std::size_t k_block  = 32; // local audio callback size

    std::vector<float> bt_frames;
    std::vector<float> up48;
    std::vector<float> out(k_block);
    std::vector<float> tail;
    tail.reserve(1 << 16);

    const double total_seconds = 12.0;
    const auto   blocks        = static_cast<std::uint64_t>(total_seconds * 48000.0 / static_cast<double>(k_block));
    for (std::uint64_t b = 0; b < blocks; ++b) {
        // BT thread: the codec delivered whatever its crystal produced.
        bt_debt += static_cast<double>(k_block) / 48000.0 * bt_rate;
        const auto n_bt = static_cast<std::size_t>(bt_debt);
        bt_debt -= static_cast<double>(n_bt);
        bt_frames.resize(n_bt);
        for (std::size_t i = 0; i < n_bt; ++i) {
            bt_frames[i] = static_cast<float>(
                k_amp * std::sin(2.0 * std::numbers::pi * k_tone_hz / bt_rate * static_cast<double>(bt_index++)));
        }
        // RatioTap converts the number; the result is nominal 48 k, still
        // paced by the BT crystal — which is exactly what asrc.push expects.
        up48.resize(ratio.outputs_for(n_bt));
        const std::size_t made = ratio.process(bt_frames.data(), n_bt, up48.data());
        asrc.push(up48.data(), made);

        // Local audio callback: pull at the local 48 kHz clock.
        asrc.pull(out.data(), k_block);
        if (b * k_block > static_cast<std::uint64_t>(total_seconds - 2.0) * 48000) {
            tail.insert(tail.end(), out.begin(), out.end());
        }
    }

    const auto st = asrc.status();
    // The ASRC sees the BT crystal's ppm, unchanged through the fixed ratio.
    std::printf("asrc state: %s, ppm estimate: %+.1f (crystal: %+.1f)\n",
                st.state == tap::samplerate::converter_state::locked ? "locked" : "not locked", st.ppm, k_bt_ppm);

    // The recovered tone at the local clock: 997 Hz exactly (the tone rode
    // the BT crystal, and the bridge absorbed both the ratio and the drift).
    const auto fit = tap::dsp::analysis::fit_sine_tracked(tail, k_tone_hz / 48000.0);
    std::printf("recovered tone: %.3f Hz, amplitude %.4f, SNR %.1f dB\n", fit.freq_norm * 48000.0, fit.amplitude,
                tap::dsp::analysis::snr_db(fit));
    std::printf("bridge latency: %.2f ms (RatioTap %.2f + ASRC %.2f)\n",
                ratio.latency_input_frames() / 44100.0 * 1e3 + asrc.designed_latency_seconds() * 1e3,
                ratio.latency_input_frames() / 44100.0 * 1e3, asrc.designed_latency_seconds() * 1e3);

    const bool ok = st.state == tap::samplerate::converter_state::locked && st.underruns == 0
                    && std::abs(fit.amplitude - k_amp) < 0.01 && std::abs(fit.freq_norm * 48000.0 - k_tone_hz) < 0.05
                    && tap::dsp::analysis::snr_db(fit) > 70.0;
    std::printf("%s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
