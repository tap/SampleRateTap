// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// pico2_uac2 — a Raspberry Pi Pico 2 (RP2350) as a USB Audio Class 2.0
// asynchronous-mode device on its own crystal: the bench's independent clock
// (the family roadmap's Q1 and A11). Plugged into the Pi, it is one of the two
// `hw:` devices the ALSA bridge (examples/alsa_bridge.cpp) spans, and the one
// whose clock is known to share nothing with the host's.
//
// The device's sample clock is the RP2350's 1 MHz timer, which counts the
// 12 MHz crystal through the tick generator: at t microseconds after a
// stream starts, the device has produced (or consumed)
//
//     frames(t) = floor(t * 48,000 * (1 + ppm / 10^6) / 10^6)
//               = floor(t * 6 * (10^6 + ppm) / (125 * 10^6))
//
// in exact integer arithmetic (no overflow for 2^63 / (6 * 1,001,000) us,
// some 426 hours, per stream). `ppm` is PICO2_UAC2_PPM, 0 by default: an
// offset injected on top of the crystal's own error, so a run can be checked
// against a known difference (README).
//
// Both directions are asynchronous isochronous endpoints (USB 2.0, 5.12.4.1):
//
//  - Capture (IN): before every packet (TinyUSB's pre-load callback) the
//    firmware writes exactly the frames its clock produced since the last
//    one: 47, 48 or 49 a millisecond, averaging the device's rate, never the
//    host's. The callback runs in the main loop, up to a few hundred
//    microseconds after the frame it serves, so its times pass through a
//    second-order delay-locked loop first (F. Adriaensen, "Using a DLL to
//    filter time", Linux Audio Conference 2005; 1 Hz bandwidth): the
//    packets then step 47/48/49 as the clocks beat, not with the loop's
//    latency, and the DLL's time stays within a bounded offset of the
//    real one, so the long-run count is the device's. A stereo tone
//    (997 Hz left, 441 Hz right, -6 dBFS), indexed by device frame, so the
//    tone's pitch at the host also carries the clock.
//  - Playback (OUT): the main loop drains the endpoint's FIFO at the device's
//    clock, and the explicit feedback endpoint (USB 2.0, 5.12.4.2) tells the
//    host the rate; TinyUSB computes it from the FIFO's level, held half full
//    (its AUDIO_FEEDBACK_METHOD_FIFO_COUNT), so the host follows the crystal.
//
// Core 1 prints a status line a second on UART0 (GP0/GP1, 115200 8N1): the
// frames each way, the playback FIFO's range, underruns and the level heard.
// Core 0 only queues the numbers, so a serial line never stalls the USB loop.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <numbers>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "tusb.h"
#include "usb_descriptors.h"

#ifndef PICO2_UAC2_PPM
#define PICO2_UAC2_PPM 0
#endif

namespace {

    constexpr std::int64_t kPpm = PICO2_UAC2_PPM;
    // async's charter is ±1000 ppm; the packet size leaves room for no more.
    static_assert(kPpm >= -1000 && kPpm <= 1000, "PICO2_UAC2_PPM must lie within ±1000 ppm");

    constexpr std::uint32_t kFrameBytes         = UAC2_CHANNELS * UAC2_BYTES_PER_SAMPLE;
    constexpr std::uint64_t kMaxFramesPerPacket = UAC2_EP_SIZE / kFrameBytes; // 49
    // Capture falls this far behind only if the host stopped polling or the
    // loop stalled; the backlog is then skipped (and counted), never burst.
    constexpr std::uint64_t kInBacklogLimit = 3 * kMaxFramesPerPacket;
    // Playback starts draining once the FIFO is half full, where the
    // FIFO-count feedback holds it.
    constexpr std::uint32_t kOutFifoFrames  = CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / kFrameBytes;
    constexpr std::uint32_t kOutPrimeFrames = kOutFifoFrames / 2;
    constexpr std::uint64_t kChunkFrames    = 64;

    /// The device's sample clock: frames in `us` microseconds of the crystal.
    std::uint64_t device_frames(std::uint64_t us) {
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(us) * 6 * (1'000'000 + kPpm) / 125'000'000);
    }

    constexpr std::size_t kTableBits = 12;
    std::int16_t          g_sine[std::size_t{1} << kTableBits];

    constexpr std::uint32_t phase_increment(double hz) {
        return static_cast<std::uint32_t>(hz / UAC2_SAMPLE_RATE * 4294967296.0 + 0.5);
    }
    constexpr std::uint32_t kIncLeft  = phase_increment(997.0);
    constexpr std::uint32_t kIncRight = phase_increment(441.0);

    /// The tone at device frame `frame`: the phase is the frame index times
    /// the increment, modulo 2^32, so a skipped frame costs no drift.
    std::int16_t tone(std::uint64_t frame, std::uint32_t increment) {
        const auto phase = static_cast<std::uint32_t>(frame * increment);
        return g_sine[phase >> (32 - kTableBits)];
    }

    struct out_state {
        bool          streaming       = false; // the host chose alternate 1
        bool          started         = false; // primed, draining at the device's clock
        std::uint64_t base_us         = 0;
        std::uint64_t consumed        = 0; // device frames this stream, played or missed
        std::uint64_t frames          = 0; // frames taken from the FIFO, all streams
        std::uint64_t underrun_frames = 0;
        std::uint32_t underruns       = 0; // the FIFO ran dry: re-primed
        std::uint32_t fifo_min        = 0;
        std::uint32_t fifo_max        = 0;
        std::int32_t  peak            = 0;
    };

    struct in_state {
        bool          streaming  = false;
        bool          started    = false;
        std::uint64_t base_us    = 0;
        double        dll_now    = 0.0; // this packet's filtered time, us since base_us
        double        dll_next   = 0.0; // the next packet's predicted time
        double        dll_period = 0.0; // the host's frame period, in device us
        std::uint64_t sent       = 0;   // device frames this stream, sent or skipped
        std::uint64_t frames     = 0;   // frames sent, all streams
        std::uint64_t skipped    = 0;
        std::uint32_t resyncs    = 0; // the DLL lost the frame clock and restarted
    };

    // The DLL's coefficients for a 1 Hz bandwidth at one update a
    // millisecond: omega = 2 pi B T, b = sqrt(2) omega, c = omega^2.
    constexpr double kDllOmega = 2.0 * std::numbers::pi * 1.0 * 1e-3;
    constexpr double kDllB     = std::numbers::sqrt2 * kDllOmega;
    constexpr double kDllC     = kDllOmega * kDllOmega;
    // An error this large is a lost frame clock, not jitter: restart.
    constexpr double kDllResyncUs = 5000.0;

    out_state    g_out;
    in_state     g_in;
    std::int16_t g_buf[kChunkFrames * UAC2_CHANNELS];

    /// One second's numbers, handed from core 0 to core 1.
    struct status {
        std::uint64_t t_s;
        std::uint64_t out_frames;
        std::uint64_t out_underrun_frames;
        std::uint64_t in_frames;
        std::uint64_t in_skipped;
        std::uint32_t in_resyncs;
        std::uint32_t out_underruns;
        std::uint32_t fifo_min;
        std::uint32_t fifo_max;
        std::int32_t  out_peak;
        bool          out_streaming;
        bool          in_streaming;
    };
    queue_t g_status_queue;

    void reset_fifo_range() {
        g_out.fifo_min = kOutFifoFrames;
        g_out.fifo_max = 0;
        g_out.peak     = 0;
    }

    /// Playback: drains what the device's clock owes since the stream
    /// started, from the endpoint's FIFO. A dry FIFO is counted and the
    /// stream re-primes, so the feedback starts again from half full.
    void service_out() {
        if (!g_out.streaming) {
            return;
        }
        const std::uint32_t avail = tud_audio_available() / kFrameBytes;
        g_out.fifo_min            = std::min(g_out.fifo_min, avail);
        g_out.fifo_max            = std::max(g_out.fifo_max, avail);
        const std::uint64_t now   = time_us_64();
        if (!g_out.started) {
            if (avail >= kOutPrimeFrames) {
                g_out.started  = true;
                g_out.base_us  = now;
                g_out.consumed = 0;
            }
            return;
        }
        std::uint64_t owed = device_frames(now - g_out.base_us) - g_out.consumed;
        while (owed > 0) {
            const std::uint64_t chunk = std::min(owed, kChunkFrames);
            const std::uint32_t got =
                tud_audio_read(g_buf, static_cast<std::uint16_t>(chunk * kFrameBytes)) / kFrameBytes;
            for (std::uint32_t i = 0; i < got * UAC2_CHANNELS; ++i) {
                g_out.peak = std::max(g_out.peak, std::abs(static_cast<std::int32_t>(g_buf[i])));
            }
            g_out.frames += got;
            g_out.consumed += chunk;
            owed -= chunk;
            if (got < chunk) {
                g_out.underrun_frames += chunk - got;
                ++g_out.underruns;
                g_out.started = false;
                return;
            }
        }
    }

    void set_streaming(std::uint8_t itf, bool on) {
        if (itf == ITF_NUM_AUDIO_STREAMING_OUT) {
            g_out.streaming = on;
            g_out.started   = false;
        }
        else if (itf == ITF_NUM_AUDIO_STREAMING_IN) {
            g_in.streaming = on;
            g_in.started   = false;
        }
        gpio_put(PICO_DEFAULT_LED_PIN, g_out.streaming || g_in.streaming);
    }

    [[noreturn]] void status_core() {
        while (true) {
            status s{};
            queue_remove_blocking(&g_status_queue, &s);
            const double peak_db =
                s.out_peak > 0 ? 20.0 * std::log10(static_cast<double>(s.out_peak) / 32768.0) : -999.0;
            std::printf("TAP_SR_UAC2 t=%llu out=%s frames=%llu fifo=%lu..%lu underruns=%lu (%llu frames) peak=%.1f dBFS"
                        " in=%s frames=%llu skipped=%llu resyncs=%lu\n",
                        static_cast<unsigned long long>(s.t_s), s.out_streaming ? "on" : "off",
                        static_cast<unsigned long long>(s.out_frames), static_cast<unsigned long>(s.fifo_min),
                        static_cast<unsigned long>(s.fifo_max), static_cast<unsigned long>(s.out_underruns),
                        static_cast<unsigned long long>(s.out_underrun_frames), peak_db, s.in_streaming ? "on" : "off",
                        static_cast<unsigned long long>(s.in_frames), static_cast<unsigned long long>(s.in_skipped),
                        static_cast<unsigned long>(s.in_resyncs));
        }
    }

} // namespace

//--------------------------------------------------------------------+
// TinyUSB audio class callbacks (declared by tusb.h, C linkage)
//--------------------------------------------------------------------+

// Capture: called before every IN packet, the first included.
bool tud_audio_tx_done_pre_load_cb(std::uint8_t rhport, std::uint8_t func_id, std::uint8_t ep_in,
                                   std::uint8_t cur_alt_setting) {
    (void)rhport;
    (void)func_id;
    (void)ep_in;
    (void)cur_alt_setting;
    const std::uint64_t now = time_us_64();
    // The DLL (Adriaensen 2005, section 4): the event's filtered time is the
    // last prediction; the error against the measured time moves the next
    // prediction (b) and the period (c).
    const auto measured = static_cast<double>(now - g_in.base_us);
    if (!g_in.started) {
        g_in.started    = true;
        g_in.base_us    = now;
        g_in.dll_now    = 0.0;
        g_in.dll_period = 1000.0;
        g_in.dll_next   = g_in.dll_period;
        g_in.sent       = 0;
    }
    else if (const double error = measured - g_in.dll_next; std::abs(error) > kDllResyncUs) {
        g_in.dll_now    = measured;
        g_in.dll_period = 1000.0;
        g_in.dll_next   = measured + g_in.dll_period;
        ++g_in.resyncs;
    }
    else {
        g_in.dll_now = g_in.dll_next;
        g_in.dll_next += kDllB * error + g_in.dll_period;
        g_in.dll_period += kDllC * error;
    }
    const std::uint64_t due  = device_frames(static_cast<std::uint64_t>(std::max(g_in.dll_now, 0.0)));
    std::uint64_t       owed = due - g_in.sent;
    if (owed > kInBacklogLimit) {
        g_in.skipped += owed - kMaxFramesPerPacket;
        g_in.sent = due - kMaxFramesPerPacket;
        owed      = kMaxFramesPerPacket;
    }
    const std::uint64_t n = std::min(owed, kMaxFramesPerPacket);
    // n <= 49 frames; g_buf holds 64.
    for (std::uint64_t i = 0; i < n; ++i) {
        const std::uint64_t frame    = g_in.sent + i;
        g_buf[i * UAC2_CHANNELS]     = tone(frame, kIncLeft);
        g_buf[i * UAC2_CHANNELS + 1] = tone(frame, kIncRight);
    }
    tud_audio_write(g_buf, static_cast<std::uint16_t>(n * kFrameBytes));
    g_in.sent += n;
    g_in.frames += n;
    return true;
}

void tud_audio_feedback_params_cb(std::uint8_t func_id, std::uint8_t alt_itf, audio_feedback_params_t* feedback_param) {
    (void)func_id;
    (void)alt_itf;
    feedback_param->method      = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
    feedback_param->sample_freq = UAC2_SAMPLE_RATE;
}

bool tud_audio_set_itf_cb(std::uint8_t rhport, tusb_control_request_t const* p_request) {
    (void)rhport;
    const std::uint8_t itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    const std::uint8_t alt = tu_u16_low(tu_le16toh(p_request->wValue));
    set_streaming(itf, alt != 0);
    return true;
}

bool tud_audio_set_itf_close_EP_cb(std::uint8_t rhport, tusb_control_request_t const* p_request) {
    (void)rhport;
    set_streaming(tu_u16_low(tu_le16toh(p_request->wIndex)), false);
    return true;
}

// The clock's requests (UAC2 5.2.5.1): its frequency (CUR and RANGE, one
// fixed rate) and its validity. Nothing is settable.
bool tud_audio_get_req_entity_cb(std::uint8_t rhport, tusb_control_request_t const* p_request) {
    const auto* request = reinterpret_cast<audio_control_request_t const*>(p_request);
    if (request->bEntityID != UAC2_ENTITY_CLOCK) {
        return false;
    }
    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
        if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_4_t cur = {static_cast<std::int32_t>(tu_htole32(UAC2_SAMPLE_RATE))};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur, sizeof(cur));
        }
        if (request->bRequest == AUDIO_CS_REQ_RANGE) {
            audio_control_range_4_n_t(1) range = {};
            range.wNumSubRanges                = tu_htole16(1);
            range.subrange[0].bMin             = static_cast<std::int32_t>(UAC2_SAMPLE_RATE);
            range.subrange[0].bMax             = static_cast<std::int32_t>(UAC2_SAMPLE_RATE);
            range.subrange[0].bRes             = 0;
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &range, sizeof(range));
        }
    }
    else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID && request->bRequest == AUDIO_CS_REQ_CUR) {
        audio_control_cur_1_t valid = {1};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &valid, sizeof(valid));
    }
    return false;
}

bool tud_audio_set_req_entity_cb(std::uint8_t rhport, tusb_control_request_t const* p_request, std::uint8_t* buf) {
    (void)rhport;
    const auto* request = reinterpret_cast<audio_control_request_t const*>(p_request);
    // A host may still set the one rate it was offered; anything else stalls.
    if (request->bEntityID == UAC2_ENTITY_CLOCK && request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ
        && request->bRequest == AUDIO_CS_REQ_CUR && p_request->wLength == sizeof(audio_control_cur_4_t)) {
        std::int32_t rate = 0;
        std::memcpy(&rate, buf, sizeof(rate));
        return tu_le32toh(static_cast<std::uint32_t>(rate)) == UAC2_SAMPLE_RATE;
    }
    return false;
}

int main() {
    stdio_init_all(); // UART0 only (CMakeLists.txt): the USB device is the audio function
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    for (std::size_t i = 0; i < std::size(g_sine); ++i) {
        const double x = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(std::size(g_sine));
        g_sine[i]      = static_cast<std::int16_t>(std::lround(0.5 * 32767.0 * std::sin(x))); // -6 dBFS
    }
    reset_fifo_range();
    queue_init(&g_status_queue, sizeof(status), 4);
    multicore_launch_core1(status_core);

    std::printf("pico2_uac2: UAC2 asynchronous, %d Hz x (1 %+lld ppm), stereo S16, playback FIFO %lu frames"
                " (primed at %lu)\n",
                UAC2_SAMPLE_RATE, static_cast<long long>(kPpm), static_cast<unsigned long>(kOutFifoFrames),
                static_cast<unsigned long>(kOutPrimeFrames));

    tud_init(0);

    std::uint64_t next_status = time_us_64() + 1'000'000;
    std::uint64_t seconds     = 0;
    while (true) {
        tud_task();
        service_out();
        if (time_us_64() >= next_status) {
            next_status += 1'000'000;
            ++seconds;
            const status s{seconds,        g_out.frames, g_out.underrun_frames, g_in.frames,
                           g_in.skipped,   g_in.resyncs, g_out.underruns,       g_out.fifo_min,
                           g_out.fifo_max, g_out.peak,   g_out.streaming,       g_in.streaming};
            queue_try_add(&g_status_queue, &s); // a full queue drops the line, never blocks the loop
            reset_fifo_range();
        }
    }
}
