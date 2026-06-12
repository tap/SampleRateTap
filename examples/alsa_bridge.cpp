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

#include <algorithm>
#include <atomic>
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

#include "srt/srt.hpp"

namespace {

std::atomic<bool> gStop{false};

void onSigint(int) {
    gStop.store(true, std::memory_order_relaxed);
}

const char* stateName(srt::State s) {
    switch (s) {
    case srt::State::Filling:
        return "Filling";
    case srt::State::Acquiring:
        return "Acquiring";
    case srt::State::Locked:
        return "Locked";
    }
    return "?";
}

struct Args {
    const char* inDev = "default";
    const char* outDev = "default";
    unsigned rate = 48000;
    unsigned channels = 2;
    snd_pcm_uframes_t period = 128;
    std::size_t latency = 96;
    const char* csvPath = nullptr;
    const char* dumpPath = nullptr;
    unsigned long seconds = 0; // 0 = run until SIGINT
    double toneHz = 0.0;       // 0 = pass captured audio through
};

void usage(const char* prog) {
    std::printf("usage: %s [options]\n"
                "  --in <dev>       ALSA capture device (default \"default\")\n"
                "  --out <dev>      ALSA playback device (default \"default\")\n"
                "  --rate <hz>      nominal sample rate of both devices (default 48000)\n"
                "  --channels <n>   channel count (default 2)\n"
                "  --period <n>     frames per ALSA period (default 128)\n"
                "  --latency <n>    converter targetLatencyFrames (default 96)\n"
                "  --csv <path>     append per-second telemetry CSV\n"
                "  --dump <path>    write post-ASRC interleaved float stream raw\n"
                "  --seconds <n>    run time in seconds (default 0 = until SIGINT)\n"
                "  --tone <hz>      push a synthetic sine paced by the input device's\n"
                "                   clock instead of the captured samples\n",
                prog);
}

bool parseArgs(int argc, char** argv, Args& a) {
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
        const char* v = value(i);
        if (v == nullptr)
            return false;
        char* end = nullptr;
        if (std::strcmp(flag, "--in") == 0)
            a.inDev = v;
        else if (std::strcmp(flag, "--out") == 0)
            a.outDev = v;
        else if (std::strcmp(flag, "--rate") == 0)
            a.rate = static_cast<unsigned>(std::strtoul(v, &end, 10));
        else if (std::strcmp(flag, "--channels") == 0)
            a.channels = static_cast<unsigned>(std::strtoul(v, &end, 10));
        else if (std::strcmp(flag, "--period") == 0)
            a.period = static_cast<snd_pcm_uframes_t>(std::strtoul(v, &end, 10));
        else if (std::strcmp(flag, "--latency") == 0)
            a.latency = static_cast<std::size_t>(std::strtoul(v, &end, 10));
        else if (std::strcmp(flag, "--csv") == 0)
            a.csvPath = v;
        else if (std::strcmp(flag, "--dump") == 0)
            a.dumpPath = v;
        else if (std::strcmp(flag, "--seconds") == 0)
            a.seconds = std::strtoul(v, &end, 10);
        else if (std::strcmp(flag, "--tone") == 0)
            a.toneHz = std::strtod(v, &end);
        else {
            std::fprintf(stderr, "%s: unknown option %s\n", argv[0], flag);
            return false;
        }
        if (end != nullptr && (end == v || *end != '\0')) {
            std::fprintf(stderr, "%s: bad numeric value '%s' for %s\n", argv[0], v, flag);
            return false;
        }
    }
    return true;
}

struct AlsaDevice {
    snd_pcm_t* pcm = nullptr;
    snd_pcm_format_t format = SND_PCM_FORMAT_UNKNOWN;
    snd_pcm_uframes_t periodFrames = 0;

    AlsaDevice() = default;
    AlsaDevice(const AlsaDevice&) = delete;
    AlsaDevice& operator=(const AlsaDevice&) = delete;
    ~AlsaDevice() {
        if (pcm != nullptr)
            snd_pcm_close(pcm);
    }
};

bool openDevice(AlsaDevice& dev, const char* name, snd_pcm_stream_t stream, const Args& a) {
    const char* dir = stream == SND_PCM_STREAM_CAPTURE ? "capture" : "playback";
    int err = snd_pcm_open(&dev.pcm, name, stream, 0);
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
    if ((err = snd_pcm_hw_params_any(dev.pcm, hw)) < 0)
        return fail("hw_params_any", err);
    if ((err = snd_pcm_hw_params_set_access(dev.pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
        return fail("set interleaved access", err);
    dev.format = SND_PCM_FORMAT_FLOAT_LE; // native; fall back to S16 + conversion
    if (snd_pcm_hw_params_set_format(dev.pcm, hw, dev.format) < 0) {
        dev.format = SND_PCM_FORMAT_S16_LE;
        if ((err = snd_pcm_hw_params_set_format(dev.pcm, hw, dev.format)) < 0)
            return fail("set format (FLOAT_LE or S16_LE)", err);
    }
    if ((err = snd_pcm_hw_params_set_channels(dev.pcm, hw, a.channels)) < 0)
        return fail("set channels", err);
    unsigned rate = a.rate;
    if ((err = snd_pcm_hw_params_set_rate_near(dev.pcm, hw, &rate, nullptr)) < 0)
        return fail("set rate", err);
    if (rate != a.rate) {
        std::fprintf(stderr, "%s '%s': device cannot do %u Hz (offered %u Hz)\n", dir, name, a.rate,
                     rate);
        return false;
    }
    dev.periodFrames = a.period;
    int sub = 0;
    if ((err = snd_pcm_hw_params_set_period_size_near(dev.pcm, hw, &dev.periodFrames, &sub)) < 0)
        return fail("set period size", err);
    snd_pcm_uframes_t bufFrames = 4 * dev.periodFrames;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(dev.pcm, hw, &bufFrames)) < 0)
        return fail("set buffer size", err);
    if ((err = snd_pcm_hw_params(dev.pcm, hw)) < 0)
        return fail("hw_params commit", err);
    std::printf("%s '%s': %s, %u Hz, %u ch, period %lu, buffer %lu\n", dir, name,
                snd_pcm_format_name(dev.format), rate, a.channels,
                static_cast<unsigned long>(dev.periodFrames),
                static_cast<unsigned long>(bufFrames));
    return true;
}

void s16ToFloat(const std::int16_t* src, float* dst, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        dst[i] = static_cast<float>(src[i]) * (1.0f / 32768.0f);
}

void floatToS16(const float* src, std::int16_t* dst, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const float x = std::clamp(src[i], -1.0f, 1.0f);
        dst[i] = static_cast<std::int16_t>(std::lrintf(x * 32767.0f));
    }
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        usage(argv[0]);
        return 2;
    }
    if (args.channels == 0 || args.rate == 0 || args.period == 0 || args.latency == 0) {
        std::fprintf(stderr, "%s: rate/channels/period/latency must be nonzero\n", argv[0]);
        return 2;
    }

    AlsaDevice in;
    AlsaDevice out;
    if (!openDevice(in, args.inDev, SND_PCM_STREAM_CAPTURE, args) ||
        !openDevice(out, args.outDev, SND_PCM_STREAM_PLAYBACK, args))
        return 1;

    srt::Config cfg;
    cfg.sampleRateHz = static_cast<double>(args.rate);
    cfg.channels = args.channels;
    cfg.targetLatencyFrames = args.latency;
    // Per the ServoConfig guidance: the unlock threshold must sit
    // comfortably above half the transfer block, or block-quantized
    // occupancy excursions can demote the servo stage spuriously.
    cfg.servo.unlockThresholdFrames =
        std::max(cfg.servo.unlockThresholdFrames, 1.5 * static_cast<double>(args.period));
    srt::AsyncSampleRateConverter asrc(cfg);
    std::printf("designed latency: %.2f ms%s\n", asrc.designedLatencySeconds() * 1e3,
                args.toneHz > 0.0 ? "  (tone mode: captured samples discarded)" : "");

    std::FILE* csv = nullptr;
    if (args.csvPath != nullptr) {
        csv = std::fopen(args.csvPath, "a");
        if (csv == nullptr) {
            std::fprintf(stderr, "cannot open CSV file '%s'\n", args.csvPath);
            return 1;
        }
        std::fprintf(csv, "time_s,state,ppm,fill,underruns,overruns,resyncs\n");
    }
    std::FILE* dump = nullptr;
    if (args.dumpPath != nullptr) {
        dump = std::fopen(args.dumpPath, "wb");
        if (dump == nullptr) {
            std::fprintf(stderr, "cannot open dump file '%s'\n", args.dumpPath);
            return 1;
        }
    }

    std::signal(SIGINT, onSigint);

    std::thread capture([&] {
        const std::size_t ch = args.channels;
        const snd_pcm_uframes_t period = in.periodFrames;
        std::vector<std::int16_t> raw(period * ch);
        std::vector<float> buf(period * ch);
        double phase = 0.0;
        const double dphi = 2.0 * std::numbers::pi * args.toneHz / static_cast<double>(args.rate);
        while (!gStop.load(std::memory_order_relaxed)) {
            void* dst = in.format == SND_PCM_FORMAT_S16_LE ? static_cast<void*>(raw.data())
                                                           : static_cast<void*>(buf.data());
            const snd_pcm_sframes_t n = snd_pcm_readi(in.pcm, dst, period);
            if (n < 0) {
                if (snd_pcm_recover(in.pcm, static_cast<int>(n), 1) < 0) {
                    std::fprintf(stderr, "capture failed: %s\n", snd_strerror(static_cast<int>(n)));
                    gStop.store(true, std::memory_order_relaxed);
                    return;
                }
                continue;
            }
            const auto frames = static_cast<std::size_t>(n);
            if (frames == 0)
                continue;
            if (args.toneHz > 0.0) { // tone mode: keep the device pacing, swap the payload
                for (std::size_t f = 0; f < frames; ++f) {
                    const auto v = static_cast<float>(0.5 * std::sin(phase));
                    phase += dphi;
                    if (phase >= 2.0 * std::numbers::pi)
                        phase -= 2.0 * std::numbers::pi;
                    for (std::size_t c = 0; c < ch; ++c)
                        buf[f * ch + c] = v;
                }
            } else if (in.format == SND_PCM_FORMAT_S16_LE) {
                s16ToFloat(raw.data(), buf.data(), frames * ch);
            }
            asrc.push(buf.data(), frames); // overruns counted by the converter
        }
    });

    std::thread playback([&] {
        const std::size_t ch = args.channels;
        const snd_pcm_uframes_t period = out.periodFrames;
        std::vector<float> buf(period * ch);
        std::vector<std::int16_t> raw(period * ch);
        bool dumpFailed = false;
        while (!gStop.load(std::memory_order_relaxed)) {
            asrc.pull(buf.data(), period); // silence-pads while filling/underrun
            if (dump != nullptr && !dumpFailed &&
                std::fwrite(buf.data(), sizeof(float), period * ch, dump) != period * ch) {
                std::fprintf(stderr, "dump write failed; disabling --dump\n");
                dumpFailed = true;
            }
            if (out.format == SND_PCM_FORMAT_S16_LE)
                floatToS16(buf.data(), raw.data(), period * ch);
            snd_pcm_uframes_t done = 0;
            while (done < period && !gStop.load(std::memory_order_relaxed)) {
                const void* src = out.format == SND_PCM_FORMAT_S16_LE
                                      ? static_cast<const void*>(raw.data() + done * ch)
                                      : static_cast<const void*>(buf.data() + done * ch);
                const snd_pcm_sframes_t n = snd_pcm_writei(out.pcm, src, period - done);
                if (n < 0) {
                    if (snd_pcm_recover(out.pcm, static_cast<int>(n), 1) < 0) {
                        std::fprintf(stderr, "playback failed: %s\n",
                                     snd_strerror(static_cast<int>(n)));
                        gStop.store(true, std::memory_order_relaxed);
                        return;
                    }
                    continue;
                }
                done += static_cast<snd_pcm_uframes_t>(n);
            }
        }
        snd_pcm_drain(out.pcm);
    });

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    for (unsigned long sec = 1; !gStop.load(std::memory_order_relaxed); ++sec) {
        std::this_thread::sleep_until(t0 + std::chrono::seconds(sec));
        if (gStop.load(std::memory_order_relaxed))
            break;
        const auto st = asrc.status();
        std::printf("t=%6lus  state=%-9s  ppm=%+8.2f  fill=%8.1f  under=%llu over=%llu "
                    "resync=%llu\n",
                    sec, stateName(st.state), st.ppm, st.fifoFillFrames,
                    static_cast<unsigned long long>(st.underruns),
                    static_cast<unsigned long long>(st.overruns),
                    static_cast<unsigned long long>(st.resyncs));
        std::fflush(stdout);
        if (csv != nullptr) {
            std::fprintf(csv, "%lu,%s,%.3f,%.2f,%llu,%llu,%llu\n", sec, stateName(st.state), st.ppm,
                         st.fifoFillFrames, static_cast<unsigned long long>(st.underruns),
                         static_cast<unsigned long long>(st.overruns),
                         static_cast<unsigned long long>(st.resyncs));
            std::fflush(csv);
        }
        if (args.seconds != 0 && sec >= args.seconds)
            break;
    }
    gStop.store(true, std::memory_order_relaxed);

    capture.join();
    playback.join();
    if (csv != nullptr)
        std::fclose(csv);
    if (dump != nullptr)
        std::fclose(dump);
    std::printf("done.\n");
    return 0;
}
