// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
// Deterministic single-threaded simulation of two independent clock domains
// driving one converter. Producer and consumer events are interleaved by
// next-event virtual time, so runs are exactly reproducible.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "srt/asrc.h"

namespace srt_test {

    // ANCHOR: pf_knobs
    template <tap::samplerate::sample_type S>
    struct two_clock_sim_t {
        tap::samplerate::basic_async_sample_rate_converter<S>& asrc;
        double                                     fs_in;  ///< input-domain event rate (true input sample rate)
        double                                     fs_out; ///< output-domain event rate (true output sample rate)
        std::size_t                                channels  = 1;
        std::size_t                                chunk_in  = 32; ///< frames pushed per producer event
        std::size_t                                chunk_out = 32; ///< frames pulled per consumer event
        /// Input signal generator: value at input sample index i (all channels).
        std::function<S(std::uint64_t)> gen = [](std::uint64_t) { return S{}; };
        /// Per-channel generator (sample index, channel); overrides gen when set.
        std::function<S(std::uint64_t, std::size_t)> gen_ch = {};
        /// Optional input-rate modulation: fsIn scale factor at virtual time t
        /// (e.g. for drift-ramp tests). Defaults to constant 1.
        std::function<double(double)> fs_in_scale = [](double) { return 1.0; };
        // ANCHOR_END: pf_knobs

        // ANCHOR: pf_run
        /// Runs for `seconds` of output-clock virtual time. onOut receives every
        /// pulled block: (interleavedSamples, frames, virtualTime).
        template <typename OnOutput>
        void run(double seconds, OnOutput&& on_out) {
            std::vector<S> in_buf(chunk_in * channels);
            std::vector<S> out_buf(chunk_out * channels);
            double         t_in  = 0.0;
            double         t_out = 0.0;
            std::uint64_t  idx   = 0;
            while (t_out < seconds) {
                if (t_in <= t_out) {
                    for (std::size_t f = 0; f < chunk_in; ++f) {
                        if (gen_ch) {
                            for (std::size_t c = 0; c < channels; ++c) {
                                in_buf[f * channels + c] = gen_ch(idx, c);
                            }
                            ++idx;
                        }
                        else {
                            const S v = gen(idx++);
                            for (std::size_t c = 0; c < channels; ++c) {
                                in_buf[f * channels + c] = v;
                            }
                        }
                    }
                    asrc.push(in_buf.data(), chunk_in);
                    t_in += static_cast<double>(chunk_in) / (fs_in * fs_in_scale(t_in));
                }
                else {
                    asrc.pull(out_buf.data(), chunk_out);
                    on_out(out_buf.data(), chunk_out, t_out);
                    t_out += static_cast<double>(chunk_out) / fs_out;
                }
            }
        }
        // ANCHOR_END: pf_run
    };

    using two_clock_sim = two_clock_sim_t<float>;

} // namespace srt_test
