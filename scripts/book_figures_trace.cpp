// Trace dumper for the book's measured figures (scripts/book_figures.py).
//
// Runs the converter in deterministic virtual time — the same event-driven
// two-clock scheme as tests/support/two_clock_sim.hpp — and prints one CSV
// row per pull: t,fill,state,ppm,underruns. book_figures.py compiles this
// file twice, once against the current include/ tree and once against the
// tree of the last pre-feasibility-fix commit, so the before/after figure
// in the composition chapter is measured on both sides of the fix, not
// modeled. Only Status fields that exist in both versions are printed.
//
// Usage: trace pullBlock pushBlock ppm seconds [dropStart dropDur]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>

#include <srt/asrc.hpp>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s pullBlock pushBlock ppm seconds [dropStart dropDur]\n",
                     argv[0]);
        return 2;
    }
    const std::size_t pullBlock = static_cast<std::size_t>(std::atol(argv[1]));
    const std::size_t pushBlock = static_cast<std::size_t>(std::atol(argv[2]));
    const double ppm = std::atof(argv[3]);
    const double seconds = std::atof(argv[4]);
    const double dropStart = argc > 5 ? std::atof(argv[5]) : -1.0;
    const double dropDur = argc > 6 ? std::atof(argv[6]) : 0.0;

    srt::Config cfg;
    cfg.channels = 1;
    srt::AsyncSampleRateConverter conv(cfg);

    const double fsOut = cfg.sampleRateHz;
    const double fsIn = fsOut * (1.0 + ppm * 1e-6); // producer's crystal
    std::vector<float> in(pushBlock), out(pullBlock);

    double tPush = 0.0, tPull = 0.0, phase = 0.0;
    const double dPhase = 2.0 * std::numbers::pi * 997.0 / fsIn;
    std::puts("t,fill,state,ppm,underruns");
    while (tPull < seconds) {
        if (tPush <= tPull) {
            if (!(tPush >= dropStart && tPush < dropStart + dropDur)) {
                for (auto& v : in) {
                    v = 0.5f * static_cast<float>(std::sin(phase));
                    phase += dPhase;
                }
                conv.push(in.data(), pushBlock);
            }
            tPush += static_cast<double>(pushBlock) / fsIn;
            continue;
        }
        conv.pull(out.data(), pullBlock);
        tPull += static_cast<double>(pullBlock) / fsOut;
        const srt::Status s = conv.status();
        std::printf("%.6f,%.2f,%d,%.2f,%llu\n", tPull, s.fifoFillFrames, static_cast<int>(s.state),
                    s.ppm, static_cast<unsigned long long>(s.underruns));
    }
    return 0;
}
