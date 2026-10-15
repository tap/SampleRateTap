// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// ALSA duplex bridge: the "Setup 1" harness from docs/HARDWARE_TESTING.md.
//
// Two independent audio devices, two threads, real crystals:
//  - capture thread: blocking snd_pcm_readi() from --in, paced by that
//    device's clock -> asrc.push()
//  - playback thread: asrc.pull() -> blocking snd_pcm_writei() to --out,
//    paced by the other device's clock
// The main thread prints status() once per second, optionally appends it to
// a CSV (--csv), and the playback thread can dump the post-ASRC interleaved
// float stream to disk (--dump) for offline analysis with the notebook
// tooling — the clocks are real even if the signal never goes analog.
//
// Tone mode (--tone <hz>): the capture thread still blocks on snd_pcm_readi()
// so push() stays paced by the input device's real clock, but the captured
// samples are discarded and a synthetic sine is pushed instead — a clean
// known signal through real clocks without trusting the dongle's analog path.
//
// The measurement (the roadmap's A11, Block B's bench): only hw: devices are
// accepted, because a plugin device ("default", plughw:, dmix) may route
// through PipeWire or ALSA's own rate converter and the bridge would then
// measure that resampler's clock, not the dongle's crystal. Each device's
// thread counts the frames it moves and the xruns it recovers from, and
// after every transfer records the device's last hardware pointer update
// (snd_pcm_htimestamp, on CLOCK_MONOTONIC_RAW, where the driver provides it).
// The per-second CSV samples all of it beside CLOCK_MONOTONIC_RAW, so
// alsa_bridge_report.py can regress each device's frames against time and
// check the servo's ppm against the crystals' measured ratio. --rt runs both
// audio threads SCHED_FIFO with every page locked (mlockall), and fails
// rather than measuring without them.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <thread>
#include <vector>

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>

#include "tap/sr/async/async.h"

namespace {

    std::atomic<bool> g_stop{false};
    std::atomic<bool> g_failed{false}; // an audio thread could not run as asked (--rt)

    /// Nanoseconds on CLOCK_MONOTONIC_RAW: the clock the CSV samples and the
    /// devices' hardware timestamps are taken on, never slewed by NTP.
    std::int64_t mono_raw_ns() {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        return static_cast<std::int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
    }

    /// One device's telemetry, written by its own audio thread only and read
    /// by the main thread once per second. The hardware position and its
    /// timestamp form a pair, published through a sequence counter so the
    /// reader never sees one from one update and the other from the next.
    struct device_telemetry {
        std::atomic<std::uint64_t> frames{0}; ///< frames the thread moved
        std::atomic<std::uint64_t> xruns{0};  ///< -EPIPE recoveries (capture overrun, playback underrun)
        std::atomic<std::uint64_t> errors{0}; ///< every other recovery (suspend, interrupted call)

        void publish_hw(std::uint64_t hw_frames, std::int64_t hw_ns) {
            const std::uint32_t s = m_seq.load(std::memory_order_relaxed);
            m_seq.store(s + 1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            m_hw_frames.store(hw_frames, std::memory_order_relaxed);
            m_hw_ns.store(hw_ns, std::memory_order_relaxed);
            m_seq.store(s + 2, std::memory_order_release);
        }

        /// The last pair, or false while the driver has given no timestamp.
        bool read_hw(std::uint64_t& hw_frames, std::int64_t& hw_ns) const {
            for (;;) {
                const std::uint32_t s0 = m_seq.load(std::memory_order_acquire);
                if ((s0 & 1U) != 0) {
                    continue;
                }
                hw_frames = m_hw_frames.load(std::memory_order_relaxed);
                hw_ns     = m_hw_ns.load(std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_acquire);
                if (m_seq.load(std::memory_order_relaxed) == s0) {
                    return s0 != 0;
                }
            }
        }

      private:
        std::atomic<std::uint32_t> m_seq{0};
        std::atomic<std::uint64_t> m_hw_frames{0};
        std::atomic<std::int64_t>  m_hw_ns{0};
    };

    device_telemetry g_in_tel;
    device_telemetry g_out_tel;

    void on_sigint(int) {
        g_stop.store(true, std::memory_order_relaxed);
    }

    const char* state_name(tap::sr::async::converter_state s) {
        switch (s) {
        case tap::sr::async::converter_state::filling:
            return "Filling";
        case tap::sr::async::converter_state::acquiring:
            return "Acquiring";
        case tap::sr::async::converter_state::locked:
            return "Locked";
        }
        return "?";
    }

    struct cli_args {
        const char*       in_dev    = nullptr;
        const char*       out_dev   = nullptr;
        unsigned          rate      = 48000;
        unsigned          channels  = 2;
        snd_pcm_uframes_t period    = 128;
        std::size_t       latency   = 96;
        const char*       csv_path  = nullptr;
        const char*       dump_path = nullptr;
        unsigned long     seconds   = 0;   // 0 = run until SIGINT
        double            tone_hz   = 0.0; // 0 = pass captured audio through
        bool              rt        = false;
    };

    void usage(const char* prog) {
        std::printf("usage: %s [options]\n"
                    "  --in <dev>       ALSA capture device, hw: only (e.g. hw:1,0); required\n"
                    "  --out <dev>      ALSA playback device, hw: only (e.g. hw:2,0); required\n"
                    "  --rate <hz>      nominal sample rate of both devices (default 48000)\n"
                    "  --channels <n>   channel count (default 2)\n"
                    "  --period <n>     frames per ALSA period (default 128)\n"
                    "  --latency <n>    converter target_latency_frames (default 96)\n"
                    "  --csv <path>     write per-second telemetry CSV (alsa_bridge_report.py)\n"
                    "  --dump <path>    write post-ASRC interleaved float stream raw\n"
                    "  --seconds <n>    run time in seconds (default 0 = until SIGINT)\n"
                    "  --tone <hz>      push a synthetic sine paced by the input device's\n"
                    "                   clock instead of the captured samples\n"
                    "  --rt             run both audio threads SCHED_FIFO with memory locked;\n"
                    "                   fail if the system refuses either\n",
                    prog);
    }

    bool parse_args(int argc, char** argv, cli_args& a) {
        const auto value = [&](int& i) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s: missing value for %s\n", argv[0], argv[i]);
                return nullptr;
            }
            return argv[++i];
        };
        for (int i = 1; i < argc; ++i) {
            const char* flag = argv[i];
            if (std::strcmp(flag, "--help") == 0 || std::strcmp(flag, "-h") == 0) {
                usage(argv[0]);
                std::exit(0);
            }
            if (std::strcmp(flag, "--rt") == 0) {
                a.rt = true;
                continue;
            }
            const char* v = value(i);
            if (v == nullptr) {
                return false;
            }
            char* end = nullptr;
            if (std::strcmp(flag, "--in") == 0) {
                a.in_dev = v;
            }
            else if (std::strcmp(flag, "--out") == 0) {
                a.out_dev = v;
            }
            else if (std::strcmp(flag, "--rate") == 0) {
                a.rate = static_cast<unsigned>(std::strtoul(v, &end, 10));
            }
            else if (std::strcmp(flag, "--channels") == 0) {
                a.channels = static_cast<unsigned>(std::strtoul(v, &end, 10));
            }
            else if (std::strcmp(flag, "--period") == 0) {
                a.period = static_cast<snd_pcm_uframes_t>(std::strtoul(v, &end, 10));
            }
            else if (std::strcmp(flag, "--latency") == 0) {
                a.latency = static_cast<std::size_t>(std::strtoul(v, &end, 10));
            }
            else if (std::strcmp(flag, "--csv") == 0) {
                a.csv_path = v;
            }
            else if (std::strcmp(flag, "--dump") == 0) {
                a.dump_path = v;
            }
            else if (std::strcmp(flag, "--seconds") == 0) {
                a.seconds = std::strtoul(v, &end, 10);
            }
            else if (std::strcmp(flag, "--tone") == 0) {
                a.tone_hz = std::strtod(v, &end);
            }
            else {
                std::fprintf(stderr, "%s: unknown option %s\n", argv[0], flag);
                return false;
            }
            if (end != nullptr && (end == v || *end != '\0')) {
                std::fprintf(stderr, "%s: bad numeric value '%s' for %s\n", argv[0], v, flag);
                return false;
            }
        }
        if (a.in_dev == nullptr || a.out_dev == nullptr) {
            std::fprintf(stderr, "%s: --in and --out are required\n", argv[0]);
            return false;
        }
        return true;
    }

    /// Only a hw: device runs on the dongle's own clock: "default", plughw:,
    /// dmix and friends may resample (PipeWire's converter on a Pi, or ALSA's
    /// rate plugin), and the bridge would measure that converter instead.
    bool is_hw_device(const char* name) {
        return std::strncmp(name, "hw:", 3) == 0;
    }

    /// SCHED_FIFO for the calling thread, or a failure the run stops on.
    bool make_realtime(const char* who) {
        sched_param sp{};
        sp.sched_priority = 80;
        const int err     = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
        if (err != 0) {
            std::fprintf(stderr, "--rt: %s thread cannot run SCHED_FIFO: %s\n", who, std::strerror(err));
            g_failed.store(true, std::memory_order_relaxed);
            g_stop.store(true, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    struct alsa_device {
        snd_pcm_t*        pcm           = nullptr;
        snd_pcm_format_t  format        = SND_PCM_FORMAT_UNKNOWN;
        snd_pcm_uframes_t period_frames = 0;
        snd_pcm_uframes_t buffer_frames = 0;

        alsa_device()                              = default;
        alsa_device(const alsa_device&)            = delete;
        alsa_device& operator=(const alsa_device&) = delete;
        ~alsa_device() {
            if (pcm != nullptr) {
                snd_pcm_close(pcm);
            }
        }
    };

    bool open_device(alsa_device& dev, const char* name, snd_pcm_stream_t stream, const cli_args& a) {
        const char* dir = stream == SND_PCM_STREAM_CAPTURE ? "capture" : "playback";
        int         err = snd_pcm_open(&dev.pcm, name, stream, 0);
        if (err < 0) {
            std::fprintf(stderr, "cannot open %s device '%s': %s\n", dir, name, snd_strerror(err));
            return false;
        }
        const auto fail = [&](const char* what, int e) {
            std::fprintf(stderr, "%s '%s': %s failed: %s\n", dir, name, what, snd_strerror(e));
            return false;
        };
        snd_pcm_hw_params_t* hw = nullptr;
        snd_pcm_hw_params_alloca(&hw);
        if ((err = snd_pcm_hw_params_any(dev.pcm, hw)) < 0) {
            return fail("hw_params_any", err);
        }
        if ((err = snd_pcm_hw_params_set_access(dev.pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
            return fail("set interleaved access", err);
        }
        dev.format = SND_PCM_FORMAT_FLOAT_LE; // native; fall back to S16 + conversion
        if (snd_pcm_hw_params_set_format(dev.pcm, hw, dev.format) < 0) {
            dev.format = SND_PCM_FORMAT_S16_LE;
            if ((err = snd_pcm_hw_params_set_format(dev.pcm, hw, dev.format)) < 0) {
                return fail("set format (FLOAT_LE or S16_LE)", err);
            }
        }
        if ((err = snd_pcm_hw_params_set_channels(dev.pcm, hw, a.channels)) < 0) {
            return fail("set channels", err);
        }
        unsigned rate = a.rate;
        if ((err = snd_pcm_hw_params_set_rate_near(dev.pcm, hw, &rate, nullptr)) < 0) {
            return fail("set rate", err);
        }
        if (rate != a.rate) {
            std::fprintf(stderr, "%s '%s': device cannot do %u Hz (offered %u Hz)\n", dir, name, a.rate, rate);
            return false;
        }
        dev.period_frames = a.period;
        int sub           = 0;
        if ((err = snd_pcm_hw_params_set_period_size_near(dev.pcm, hw, &dev.period_frames, &sub)) < 0) {
            return fail("set period size", err);
        }
        snd_pcm_uframes_t buf_frames = 4 * dev.period_frames;
        if ((err = snd_pcm_hw_params_set_buffer_size_near(dev.pcm, hw, &buf_frames)) < 0) {
            return fail("set buffer size", err);
        }
        if ((err = snd_pcm_hw_params(dev.pcm, hw)) < 0) {
            return fail("hw_params commit", err);
        }
        dev.buffer_frames = buf_frames;
        // Timestamps on every pointer update, on the raw monotonic clock the
        // CSV samples; a driver without them leaves snd_pcm_htimestamp at 0.
        snd_pcm_sw_params_t* sw = nullptr;
        snd_pcm_sw_params_alloca(&sw);
        if ((err = snd_pcm_sw_params_current(dev.pcm, sw)) < 0) {
            return fail("sw_params_current", err);
        }
        if ((err = snd_pcm_sw_params_set_tstamp_mode(dev.pcm, sw, SND_PCM_TSTAMP_ENABLE)) < 0) {
            return fail("set tstamp mode", err);
        }
        if ((err = snd_pcm_sw_params_set_tstamp_type(dev.pcm, sw, SND_PCM_TSTAMP_TYPE_MONOTONIC_RAW)) < 0) {
            return fail("set tstamp type (MONOTONIC_RAW)", err);
        }
        if ((err = snd_pcm_sw_params(dev.pcm, sw)) < 0) {
            return fail("sw_params commit", err);
        }
        std::printf("%s '%s': %s, %u Hz, %u ch, period %lu, buffer %lu\n", dir, name, snd_pcm_format_name(dev.format),
                    rate, a.channels, static_cast<unsigned long>(dev.period_frames),
                    static_cast<unsigned long>(buf_frames));
        return true;
    }

    /// After a transfer: the device's last hardware pointer update, as the
    /// frames the hardware had moved at that instant. Capture: the frames read
    /// plus those waiting to be read; playback: the frames written minus those
    /// still queued.
    void record_hw_position(const alsa_device& dev, device_telemetry& tel, bool capture) {
        snd_pcm_uframes_t avail = 0;
        snd_htimestamp_t  ts{};
        if (snd_pcm_htimestamp(dev.pcm, &avail, &ts) < 0 || (ts.tv_sec == 0 && ts.tv_nsec == 0)) {
            return;
        }
        const std::uint64_t moved = tel.frames.load(std::memory_order_relaxed);
        std::uint64_t       hw    = 0;
        if (capture) {
            hw = moved + avail;
        }
        else {
            const std::uint64_t queued = dev.buffer_frames > avail ? dev.buffer_frames - avail : 0;
            hw                         = moved > queued ? moved - queued : 0;
        }
        tel.publish_hw(hw, static_cast<std::int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec);
    }

    void s16_to_float(const std::int16_t* src, float* dst, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            dst[i] = static_cast<float>(src[i]) * (1.0f / 32768.0f);
        }
    }

    void float_to_s16(const float* src, std::int16_t* dst, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            const float x = std::clamp(src[i], -1.0f, 1.0f);
            dst[i]        = static_cast<std::int16_t>(std::lrintf(x * 32767.0f));
        }
    }

} // namespace

int main(int argc, char** argv) {
    cli_args args;
    if (!parse_args(argc, argv, args)) {
        usage(argv[0]);
        return 2;
    }
    if (args.channels == 0 || args.rate == 0 || args.period == 0 || args.latency == 0) {
        std::fprintf(stderr, "%s: rate/channels/period/latency must be nonzero\n", argv[0]);
        return 2;
    }
    for (const char* dev : {args.in_dev, args.out_dev}) {
        if (!is_hw_device(dev)) {
            std::fprintf(stderr,
                         "%s: refusing '%s': only hw: devices are measured (a plugin device such as "
                         "'default' or plughw: may resample, and the bridge would see that converter's "
                         "clock, not the crystal's)\n",
                         argv[0], dev);
            return 2;
        }
    }
    if (args.rt && mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::fprintf(stderr, "%s: --rt: mlockall failed: %s (raise RLIMIT_MEMLOCK or run privileged)\n", argv[0],
                     std::strerror(errno));
        return 1;
    }

    alsa_device in;
    alsa_device out;
    if (!open_device(in, args.in_dev, SND_PCM_STREAM_CAPTURE, args)
        || !open_device(out, args.out_dev, SND_PCM_STREAM_PLAYBACK, args)) {
        return 1;
    }

    tap::sr::async::config cfg;
    cfg.sample_rate_hz        = static_cast<double>(args.rate);
    cfg.channels              = args.channels;
    cfg.target_latency_frames = args.latency;
    // Per the servo_config guidance: the unlock threshold must sit
    // comfortably above half the transfer block, or block-quantized
    // occupancy excursions can demote the servo stage spuriously.
    cfg.servo.unlock_threshold_frames =
        std::max(cfg.servo.unlock_threshold_frames, 1.5 * static_cast<double>(args.period));
    tap::sr::async::converter asrc(cfg);
    std::printf("designed latency: %.2f ms%s\n", asrc.designed_latency_seconds() * 1e3,
                args.tone_hz > 0.0 ? "  (tone mode: captured samples discarded)" : "");

    std::FILE* csv = nullptr;
    if (args.csv_path != nullptr) {
        csv = std::fopen(args.csv_path, "w");
        if (csv == nullptr) {
            std::fprintf(stderr, "cannot open CSV file '%s'\n", args.csv_path);
            return 1;
        }
        std::fprintf(csv, "time_s,state,ppm,fill,underruns,overruns,resyncs,mono_raw_ns,in_frames,out_frames,"
                          "in_xruns,out_xruns,in_errors,out_errors,in_hw_frames,in_hw_ns,out_hw_frames,"
                          "out_hw_ns\n");
    }
    std::FILE* dump = nullptr;
    if (args.dump_path != nullptr) {
        dump = std::fopen(args.dump_path, "wb");
        if (dump == nullptr) {
            std::fprintf(stderr, "cannot open dump file '%s'\n", args.dump_path);
            return 1;
        }
    }

    std::signal(SIGINT, on_sigint);

    std::thread capture([&] {
        if (args.rt && !make_realtime("capture")) {
            return;
        }
        const std::size_t         ch     = args.channels;
        const snd_pcm_uframes_t   period = in.period_frames;
        std::vector<std::int16_t> raw(period * ch);
        std::vector<float>        buf(period * ch);
        double                    phase = 0.0;
        const double              dphi  = 2.0 * std::numbers::pi * args.tone_hz / static_cast<double>(args.rate);
        while (!g_stop.load(std::memory_order_relaxed)) {
            void* dst =
                in.format == SND_PCM_FORMAT_S16_LE ? static_cast<void*>(raw.data()) : static_cast<void*>(buf.data());
            const snd_pcm_sframes_t n = snd_pcm_readi(in.pcm, dst, period);
            if (n < 0) {
                (n == -EPIPE ? g_in_tel.xruns : g_in_tel.errors).fetch_add(1, std::memory_order_relaxed);
                if (snd_pcm_recover(in.pcm, static_cast<int>(n), 1) < 0) {
                    std::fprintf(stderr, "capture failed: %s\n", snd_strerror(static_cast<int>(n)));
                    g_stop.store(true, std::memory_order_relaxed);
                    return;
                }
                continue;
            }
            const auto frames = static_cast<std::size_t>(n);
            if (frames == 0) {
                continue;
            }
            g_in_tel.frames.fetch_add(frames, std::memory_order_relaxed);
            record_hw_position(in, g_in_tel, true);
            if (args.tone_hz > 0.0) { // tone mode: keep the device pacing, swap the payload
                for (std::size_t f = 0; f < frames; ++f) {
                    const auto v = static_cast<float>(0.5 * std::sin(phase));
                    phase += dphi;
                    if (phase >= 2.0 * std::numbers::pi) {
                        phase -= 2.0 * std::numbers::pi;
                    }
                    for (std::size_t c = 0; c < ch; ++c) {
                        buf[f * ch + c] = v;
                    }
                }
            }
            else if (in.format == SND_PCM_FORMAT_S16_LE) {
                s16_to_float(raw.data(), buf.data(), frames * ch);
            }
            asrc.push(buf.data(), frames); // overruns counted by the converter
        }
    });

    std::thread playback([&] {
        if (args.rt && !make_realtime("playback")) {
            return;
        }
        const std::size_t         ch     = args.channels;
        const snd_pcm_uframes_t   period = out.period_frames;
        std::vector<float>        buf(period * ch);
        std::vector<std::int16_t> raw(period * ch);
        bool                      dump_failed = false;
        while (!g_stop.load(std::memory_order_relaxed)) {
            asrc.pull(buf.data(), period); // silence-pads while filling/underrun
            if (dump != nullptr && !dump_failed
                && std::fwrite(buf.data(), sizeof(float), period * ch, dump) != period * ch) {
                std::fprintf(stderr, "dump write failed; disabling --dump\n");
                dump_failed = true;
            }
            if (out.format == SND_PCM_FORMAT_S16_LE) {
                float_to_s16(buf.data(), raw.data(), period * ch);
            }
            snd_pcm_uframes_t done = 0;
            while (done < period && !g_stop.load(std::memory_order_relaxed)) {
                const void*             src = out.format == SND_PCM_FORMAT_S16_LE
                                                  ? static_cast<const void*>(raw.data() + done * ch)
                                                  : static_cast<const void*>(buf.data() + done * ch);
                const snd_pcm_sframes_t n   = snd_pcm_writei(out.pcm, src, period - done);
                if (n < 0) {
                    (n == -EPIPE ? g_out_tel.xruns : g_out_tel.errors).fetch_add(1, std::memory_order_relaxed);
                    if (snd_pcm_recover(out.pcm, static_cast<int>(n), 1) < 0) {
                        std::fprintf(stderr, "playback failed: %s\n", snd_strerror(static_cast<int>(n)));
                        g_stop.store(true, std::memory_order_relaxed);
                        return;
                    }
                    continue;
                }
                done += static_cast<snd_pcm_uframes_t>(n);
                g_out_tel.frames.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
                record_hw_position(out, g_out_tel, false);
            }
        }
        snd_pcm_drain(out.pcm);
    });

    using clock   = std::chrono::steady_clock;
    const auto t0 = clock::now();
    for (unsigned long sec = 1; !g_stop.load(std::memory_order_relaxed); ++sec) {
        std::this_thread::sleep_until(t0 + std::chrono::seconds(sec));
        if (g_stop.load(std::memory_order_relaxed)) {
            break;
        }
        const auto         st        = asrc.status();
        const std::int64_t now_ns    = mono_raw_ns();
        const auto         in_xruns  = g_in_tel.xruns.load(std::memory_order_relaxed);
        const auto         out_xruns = g_out_tel.xruns.load(std::memory_order_relaxed);
        std::printf("t=%6lus  state=%-9s  ppm=%+8.2f  fill=%8.1f  under=%llu over=%llu "
                    "resync=%llu  xrun in=%llu out=%llu\n",
                    sec, state_name(st.state), st.ppm, st.fifo_fill_frames,
                    static_cast<unsigned long long>(st.underruns), static_cast<unsigned long long>(st.overruns),
                    static_cast<unsigned long long>(st.resyncs), static_cast<unsigned long long>(in_xruns),
                    static_cast<unsigned long long>(out_xruns));
        std::fflush(stdout);
        if (csv != nullptr) {
            std::fprintf(csv, "%lu,%s,%.3f,%.2f,%llu,%llu,%llu,%lld,%llu,%llu,%llu,%llu,%llu,%llu", sec,
                         state_name(st.state), st.ppm, st.fifo_fill_frames,
                         static_cast<unsigned long long>(st.underruns), static_cast<unsigned long long>(st.overruns),
                         static_cast<unsigned long long>(st.resyncs), static_cast<long long>(now_ns),
                         static_cast<unsigned long long>(g_in_tel.frames.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(g_out_tel.frames.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(in_xruns), static_cast<unsigned long long>(out_xruns),
                         static_cast<unsigned long long>(g_in_tel.errors.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(g_out_tel.errors.load(std::memory_order_relaxed)));
            // The hardware pairs; empty fields where the driver gave none.
            for (const device_telemetry* tel : {&g_in_tel, &g_out_tel}) {
                std::uint64_t hw_frames = 0;
                std::int64_t  hw_ns     = 0;
                if (tel->read_hw(hw_frames, hw_ns)) {
                    std::fprintf(csv, ",%llu,%lld", static_cast<unsigned long long>(hw_frames),
                                 static_cast<long long>(hw_ns));
                }
                else {
                    std::fprintf(csv, ",,");
                }
            }
            std::fprintf(csv, "\n");
            std::fflush(csv);
        }
        if (args.seconds != 0 && sec >= args.seconds) {
            break;
        }
    }
    g_stop.store(true, std::memory_order_relaxed);

    capture.join();
    playback.join();
    if (csv != nullptr) {
        std::fclose(csv);
    }
    if (dump != nullptr) {
        std::fclose(dump);
    }
    std::printf("done.\n");
    return g_failed.load(std::memory_order_relaxed) ? 1 : 0;
}
