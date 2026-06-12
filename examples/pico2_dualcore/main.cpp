// Dual-core deployment of the ASRC on the RP2350 (docs/HARDWARE_TESTING.md,
// Setup 2, "Dual-core deployment"): the converter's two ends on the two
// Cortex-M33 cores, one core per clock domain — the shape the README
// prescribes for configurations that are tight on a single 150 MHz core
// (Q15 stereo balanced(); 12-channel).
//
//   core0  producer: push(32) paced at rate * (1 + 200e-6), plus USB telemetry
//   core1  consumer: pull(32) paced at exactly the nominal rate, every call
//          timed with the core-local DWT.CYCCNT
//
// Cross-core safety, stated explicitly: the library's runtime contract is
// one producer agent and one consumer agent around a lock-free SPSC ring
// with acquire/release atomics (srt/spsc_ring.hpp; "one producer thread and
// one consumer thread" in the README's Limitations). The contract is about
// agents and memory ordering, not about std::thread: the RP2350's cores
// share coherent SRAM (no data caches in front of it), so two CORES satisfy
// it exactly as two threads do. push() stays core0-only, pull() stays
// core1-only, status() is documented any-thread. Everything else that
// crosses cores is the explicit Shared block of 32-bit atomics below — kept
// 32-bit for the same reason the library keeps its telemetry 32-bit: on the
// M33, 64-bit std::atomic is not lock-free and would route through a
// library lock (see the footnote in asrc.hpp).
//
// Both pacing schedules derive from the same 64-bit microsecond timebase
// (the RP2350 timer is one shared block read by both cores), so the
// +200 ppm offset is exact by construction and the servo's converged
// estimate has a known truth value to be judged against — the one thing
// genuinely independent oscillators cannot provide. Due times are absolute,
// t0 + (b * num) / den in integer microseconds, so a stall (a USB telemetry
// write on core0) is followed by catch-up pushes, not permanent schedule
// slip.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <numbers>
#include <vector>

#include "RP2350.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "srt/asrc.hpp"

namespace {

using Asrc = srt::AsyncSampleRateConverterQ15;

constexpr std::size_t kBlockFrames = 32;
constexpr std::size_t kMaxChannels = 12;
constexpr std::size_t kInputFrames = 4800; // cycled producer buffer (0.1 s at 48 kHz)
constexpr double kOffsetPpm = 200.0;
constexpr double kPpmTolerance = 5.0;

// FIFO setpoint budget: the producer core also writes telemetry, and
// stdio_usb may stall the writer for up to PICO_STDIO_USB_STDOUT_TIMEOUT_US
// (capped to 2 ms in CMakeLists.txt) when the host stops draining the CDC
// buffer. During such a stall the consumer keeps pulling — 2 ms is 96 frames
// at 48 kHz — so the setpoint must exceed it with margin: 144 frames (3 ms
// at 48 kHz, 9 ms at 16 kHz). This is the README's latency-section rule
// (the setpoint must exceed the peak occupancy excursion of push/pull
// jitter) applied to a producer that shares its core with logging.
constexpr std::size_t kTargetLatencyFrames = 144;

// ---------------------------------------------------------------------------
// Cross-core shared state. The converter object itself is shared only
// through the pointer handoff below; this block carries phase control and
// the consumer's cycle statistics. All payload fields are lock-free 32-bit
// atomics; wider accumulators (the cycle sum) stay core1-private.
struct Shared {
    // Phase handoff, core0 -> core1. The release store of the converter
    // pointer publishes every plain write the constructor performed (filter
    // table, ring, servo state) plus the relaxed parameter stores preceding
    // it; core1's acquire load synchronizes-with that store. The SDK
    // multicore FIFO is left to the launch protocol — an atomic pointer
    // makes the C++ happens-before explicit instead of relying on hardware
    // FIFO side effects.
    std::atomic<Asrc*> asrc{nullptr};
    std::atomic<std::uint32_t> consNumUs{0}; // consumer due(b) = t0 + (b*num)/den us
    std::atomic<std::uint32_t> consDen{1};
    std::atomic<std::uint32_t> statsSkipBlocks{0}; // exclude fill/acquire from stats
    std::atomic<bool> stop{false};                 // core0 -> core1: end of phase
    std::atomic<bool> consumerDone{false};         // core1 -> core0: final stats published
    std::atomic<std::uint32_t> cyccnt{0};          // core1 -> core0: 0 unknown, 1 ok, 2 absent

    // Consumer stats snapshot, seqlock-style: seq is odd while the writer is
    // mid-update; the reader retries until the same even value brackets the
    // payload. The payload fields are themselves relaxed atomics (no torn
    // reads, no UB); the seqlock only adds mutual coherence, so one printed
    // line describes one instant.
    std::atomic<std::uint32_t> seq{0};
    std::atomic<std::uint32_t> blocks{0};  // measured pull() calls
    std::atomic<std::uint32_t> meanCyc{0}; // cycles per pull(32)
    std::atomic<std::uint32_t> p99Cyc{0};
    std::atomic<std::uint32_t> maxCyc{0};
    std::atomic<std::uint32_t> lateMaxUs{0}; // worst consumer schedule slip
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free &&
                  std::atomic<Asrc*>::is_always_lock_free && std::atomic<bool>::is_always_lock_free,
              "cross-core state must be lock-free on the M33");

Shared g;

struct Snapshot {
    std::uint32_t blocks = 0;
    std::uint32_t meanCyc = 0;
    std::uint32_t p99Cyc = 0;
    std::uint32_t maxCyc = 0;
    std::uint32_t lateMaxUs = 0;
};

// Seqlock writer (core1 only). The release fence orders the odd mark before
// the payload stores; the final release store orders the payload before the
// even mark.
void publishSnapshot(const Snapshot& s) {
    const std::uint32_t q = g.seq.load(std::memory_order_relaxed);
    g.seq.store(q + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    g.blocks.store(s.blocks, std::memory_order_relaxed);
    g.meanCyc.store(s.meanCyc, std::memory_order_relaxed);
    g.p99Cyc.store(s.p99Cyc, std::memory_order_relaxed);
    g.maxCyc.store(s.maxCyc, std::memory_order_relaxed);
    g.lateMaxUs.store(s.lateMaxUs, std::memory_order_relaxed);
    g.seq.store(q + 2, std::memory_order_release);
}

// Seqlock reader (core0). The acquire fence pairs with the writer's final
// release store; a retry costs nothing at the 1 Hz read rate.
Snapshot readSnapshot() {
    for (;;) {
        const std::uint32_t q0 = g.seq.load(std::memory_order_acquire);
        if (q0 & 1u)
            continue;
        Snapshot s;
        s.blocks = g.blocks.load(std::memory_order_relaxed);
        s.meanCyc = g.meanCyc.load(std::memory_order_relaxed);
        s.p99Cyc = g.p99Cyc.load(std::memory_order_relaxed);
        s.maxCyc = g.maxCyc.load(std::memory_order_relaxed);
        s.lateMaxUs = g.lateMaxUs.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g.seq.load(std::memory_order_relaxed) == q0)
            return s;
    }
}

// ---------------------------------------------------------------------------
// core1: the consumer / output clock domain.

// TRCENA gates the whole DWT block; CYCCNTENA starts the free-running 32-bit
// cycle counter. CMSIS names from the SDK's core_cm33.h; the firmware runs in
// the secure state (rp2350-arm-s) so the registers are directly writable.
// 32-bit wrap is ~28.6 s at 150 MHz — per-block unsigned deltas are safe.
//
// Per-core, verified in the SDK headers: DWT_BASE 0xE0001000 (core_cm33.h)
// sits inside the PPB (PPB_BASE 0xe0000000, hardware/regs/addressmap.h),
// and hardware/structs/m33.h maps the whole PPB — dwt_ctrl/dwt_cyccnt
// included — as `m33_hw` at that one fixed address: whichever core
// dereferences it reaches its OWN block (the device header marks PPB
// registers such as NMI_MASK0 "core-local"). So this must run ON core1;
// enabling CYCCNT from core0 would only start core0's counter. One header
// caveat: the SVD-derived regs/m33.h gives a DWT_CTRL reset value with
// NOCYCCNT=1 — but that value (NUMCOMP=7; an M33 has at most 4 DWT
// comparators) is Arm's generic ARMv8-M template, contradicted by the RW
// DWT_CYCCNT register the same SVD defines; the runtime check below is the
// authoritative gate.
bool enableCycleCounter() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk)
        return false; // implementation without a cycle counter
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return true;
}

// Histogram of cycles per pull(32) in 512-cycle buckets (covers 1M cycles
// per block — several times the heaviest expected case) so a running p99 is
// available without storing per-block samples.
constexpr unsigned kHistShift = 9;
constexpr std::size_t kHistBuckets = 2048;
std::uint32_t gHist[kHistBuckets];

std::int16_t gOut[kBlockFrames * kMaxChannels]; // consumer output block

// Derive mean and p99 from core1-private accumulators and publish. p99 is
// the upper edge of the histogram bucket containing the 99th percentile.
void finalizeAndPublish(Snapshot s, std::uint64_t cycSum) {
    if (s.blocks != 0) {
        s.meanCyc = static_cast<std::uint32_t>(cycSum / s.blocks);
        const std::uint64_t target = static_cast<std::uint64_t>(s.blocks) * 99 / 100;
        std::uint64_t cum = 0;
        for (std::size_t i = 0; i < kHistBuckets; ++i) {
            cum += gHist[i];
            if (cum > target) {
                s.p99Cyc = static_cast<std::uint32_t>((i + 1) << kHistShift);
                break;
            }
        }
        s.p99Cyc = std::min(s.p99Cyc, s.maxCyc);
    }
    publishSnapshot(s);
}

// Consumer loop: wait for a phase (converter pointer), pull at the exact
// nominal output rate until told to stop, publish stats once per second.
// Never prints — stdio stays a core0 concern, both for the FIFO budget and
// because contending on the stdio mutex from the paced core would put USB
// stalls on the output clock domain.
[[noreturn]] void core1Main() {
    g.cyccnt.store(enableCycleCounter() ? 1 : 2, std::memory_order_release);

    for (;;) {
        Asrc* asrc;
        while ((asrc = g.asrc.load(std::memory_order_acquire)) == nullptr)
            tight_loop_contents();

        const std::uint32_t num = g.consNumUs.load(std::memory_order_relaxed);
        const std::uint32_t den = g.consDen.load(std::memory_order_relaxed);
        const std::uint32_t skip = g.statsSkipBlocks.load(std::memory_order_relaxed);
        const bool timed = g.cyccnt.load(std::memory_order_relaxed) == 1;

        std::fill(std::begin(gHist), std::end(gHist), 0u);
        std::uint64_t cycSum = 0; // core1-private; only 32-bit digests cross cores
        Snapshot s;
        publishSnapshot(s); // zero the previous phase's numbers

        const std::uint64_t t0 = time_us_64() + 1000;
        std::uint64_t nextPubUs = t0 + 1000000;
        for (std::uint64_t b = 0; !g.stop.load(std::memory_order_acquire); ++b) {
            const std::uint64_t due = t0 + (b * num) / den;
            std::uint64_t now = time_us_64();
            while (now < due) {
                tight_loop_contents();
                now = time_us_64();
            }

            // Timed region is pull() alone: the per-block datapath + servo
            // cost the deployment budget cares about.
            const std::uint32_t c0 = DWT->CYCCNT;
            asrc->pull(gOut, kBlockFrames);
            const std::uint32_t cyc = DWT->CYCCNT - c0;

            if (b >= skip) {
                if (timed) {
                    cycSum += cyc;
                    ++s.blocks;
                    s.maxCyc = std::max(s.maxCyc, cyc);
                    ++gHist[std::min<std::uint32_t>(cyc >> kHistShift, kHistBuckets - 1)];
                }
                // Schedule slip: if pull() ever exceeded the block period,
                // lateness accumulates here long before the FIFO notices.
                const std::uint64_t late = now - due;
                if (late > s.lateMaxUs)
                    s.lateMaxUs = static_cast<std::uint32_t>(std::min<std::uint64_t>(late, ~0u));
            }
            if (now >= nextPubUs) {
                nextPubUs += 1000000;
                finalizeAndPublish(s, cycSum);
            }
        }

        finalizeAndPublish(s, cycSum); // final numbers for the summary line
        g.consumerDone.store(true, std::memory_order_release);
        // Wait out the teardown so a stale pointer cannot restart the phase.
        while (g.asrc.load(std::memory_order_acquire) != nullptr)
            tight_loop_contents();
    }
}

// Explicit core1 stack (the SDK default is 2 KB scratch RAM; pull() plus the
// servo's soft-double helpers fit, but the margin is not worth proving).
std::uint32_t gCore1Stack[1024];

// ---------------------------------------------------------------------------
// core0: the producer / input clock domain, plus telemetry and verdicts.

struct PhaseSpec {
    char tag;
    const char* desc;
    std::size_t channels;
    double rateHz;
    bool scaledTo16k;        // scale balanced() band edges + servo by 16/48
    std::uint32_t prodNumUs; // producer due(b) = t0 + (b*num)/den us, +200 ppm baked in
    std::uint32_t prodDen;
    std::uint32_t consNumUs; // consumer schedule, exact nominal rate
    std::uint32_t consDen;
    std::uint32_t statsSkip;   // consumer blocks excluded from cycle stats (~5 s)
    std::uint32_t lockLimitMs; // PASS: Locked within this
    std::uint32_t ppmSettleMs; // PASS: |ppm - 200| < 5 at every 1 Hz sample after this
    std::uint32_t runMs;
};

struct PhaseResult {
    bool ran = false;
    bool pass = false;
};

// balanced() with band edges scaled to 16 kHz: identical L/T — same table
// size and same per-frame cycle cost — with pass/stop at the same normalized
// frequencies (README "Measured performance"; tests/test_asrc_quality_16k.cpp).
srt::FilterSpec balanced16k() {
    srt::FilterSpec f = srt::FilterSpec::balanced();
    f.passbandHz = 20000.0 * 16.0 / 48.0;
    f.stopbandHz = 28000.0 * 16.0 / 48.0;
    return f;
}

const char* stateName(srt::State s) {
    switch (s) {
    case srt::State::Filling:
        return "Filling";
    case srt::State::Acquiring:
        return "Acquiring";
    default:
        return "Locked";
    }
}

// 997 Hz at 0.5 FS, replicated to every channel: lock dynamics and cycle
// cost are content-independent, and one shared fractional position per
// frame is the multichannel design anyway. The cycled buffer's wrap seam is
// not phase-continuous; irrelevant for the same reason (same note as
// pico2_cyccnt).
std::vector<std::int16_t> sineInput(std::size_t channels, double rateHz) {
    std::vector<std::int16_t> out(kInputFrames * channels);
    const double w = 2.0 * std::numbers::pi * 997.0 / rateHz;
    for (std::size_t f = 0; f < kInputFrames; ++f) {
        const auto v = srt::detail::roundSat<std::int16_t>(
            0.5 * std::sin(w * static_cast<double>(f)) * 32767.0);
        for (std::size_t c = 0; c < channels; ++c)
            out[f * channels + c] = v;
    }
    return out;
}

PhaseResult runPhase(const PhaseSpec& ph) {
    PhaseResult r;

    srt::Config cfg;
    cfg.sampleRateHz = ph.rateHz;
    cfg.channels = ph.channels;
    cfg.targetLatencyFrames = kTargetLatencyFrames;
    if (ph.scaledTo16k) {
        // FilterSpec band edges and ServoConfig bandwidths are absolute Hz
        // designed for ~48 kHz; both scale with the rate (README).
        cfg.filter = balanced16k();
        const double sc = ph.rateHz / 48000.0;
        cfg.servo.acquireBandwidthHz *= sc;
        cfg.servo.trackBandwidthHz *= sc;
        cfg.servo.quietBandwidthHz *= sc;
        cfg.servo.acquireSmootherHz *= sc;
        cfg.servo.trackSmootherHz *= sc;
        cfg.servo.quietSmootherHz *= sc;
    }

    // Heap-constructed so allocation failure (the 12-channel phase on a
    // tighter build) degrades to a printed SKIP instead of a hard fault.
    std::unique_ptr<Asrc> asrc;
    std::vector<std::int16_t> input;
    try {
        asrc = std::make_unique<Asrc>(cfg);
        input = sineInput(ph.channels, ph.rateHz);
    } catch (const std::exception& e) {
        std::printf("PHASE %c %s: SKIP (%s)\n", ph.tag, ph.desc, e.what());
        return r;
    }
    r.ran = true;

    std::printf("PHASE %c %s: %lu s run, lock limit %.1f s, ppm gate +/-%.0f after %.0f s\n",
                ph.tag, ph.desc, static_cast<unsigned long>(ph.runMs / 1000),
                static_cast<double>(ph.lockLimitMs) / 1000.0, kPpmTolerance,
                static_cast<double>(ph.ppmSettleMs) / 1000.0);

    // Hand the phase to core1: parameters first (relaxed), then the pointer
    // with release — the store core1's acquire load synchronizes with.
    g.consumerDone.store(false, std::memory_order_relaxed);
    g.stop.store(false, std::memory_order_relaxed);
    g.consNumUs.store(ph.consNumUs, std::memory_order_relaxed);
    g.consDen.store(ph.consDen, std::memory_order_relaxed);
    g.statsSkipBlocks.store(ph.statsSkip, std::memory_order_relaxed);
    g.asrc.store(asrc.get(), std::memory_order_release);

    const std::uint64_t tStart = time_us_64();
    const std::uint64_t tEnd = tStart + static_cast<std::uint64_t>(ph.runMs) * 1000;
    const std::uint64_t t0 = tStart + 1000;
    std::uint64_t nextTelemetryUs = tStart + 1000000;

    bool locked = false;
    std::uint64_t lockUs = 0;
    std::uint64_t undAtLock = 0, ovrAtLock = 0, rsyAtLock = 0;
    bool ppmOk = true;
    bool ppmSampled = false;
    double ppmFinal = 0.0;

    std::size_t off = 0;
    for (std::uint64_t b = 0;; ++b) {
        const std::uint64_t due = t0 + (b * ph.prodNumUs) / ph.prodDen;
        if (due >= tEnd)
            break;
        std::uint64_t now = time_us_64();
        while (now < due) {
            tight_loop_contents();
            now = time_us_64();
        }

        asrc->push(input.data() + off, kBlockFrames);
        off += kBlockFrames * ph.channels;
        if (off + kBlockFrames * ph.channels > input.size())
            off = 0;

        const srt::Status st = asrc->status();
        if (!locked && st.state == srt::State::Locked) {
            locked = true;
            lockUs = time_us_64() - tStart;
            undAtLock = st.underruns;
            ovrAtLock = st.overruns;
            rsyAtLock = st.resyncs;
        }

        // 1 Hz telemetry. The printf may stall up to the 2 ms stdio cap;
        // the absolute push schedule catches up immediately afterwards and
        // the FIFO setpoint absorbs the dip (see kTargetLatencyFrames).
        if (now >= nextTelemetryUs) {
            nextTelemetryUs += 1000000;
            const std::uint64_t tMs = (now - tStart) / 1000;
            const Snapshot sn = readSnapshot();
            const double cycFrame =
                static_cast<double>(sn.meanCyc) / static_cast<double>(kBlockFrames);
            const double pctCore =
                cycFrame * ph.rateHz / static_cast<double>(clock_get_hz(clk_sys)) * 100.0;
            std::printf(
                "[%c t=%2lus] %-9s ppm=%+7.2f fill=%6.1f und=%lu ovr=%lu rsy=%lu | "
                "pull/blk mean=%lu p99=%lu max=%lu (%4.1f%% core) late<=%luus\n",
                ph.tag, static_cast<unsigned long>(tMs / 1000), stateName(st.state), st.ppm,
                st.fifoFillFrames, static_cast<unsigned long>(st.underruns),
                static_cast<unsigned long>(st.overruns), static_cast<unsigned long>(st.resyncs),
                static_cast<unsigned long>(sn.meanCyc), static_cast<unsigned long>(sn.p99Cyc),
                static_cast<unsigned long>(sn.maxCyc), pctCore,
                static_cast<unsigned long>(sn.lateMaxUs));
            if (tMs >= ph.ppmSettleMs) {
                ppmSampled = true;
                if (std::fabs(st.ppm - kOffsetPpm) >= kPpmTolerance)
                    ppmOk = false;
            }
        }
    }

    // Teardown: stop core1, wait for its final stats. consumerDone's
    // release/acquire pair orders core1's last pull() before this point, so
    // destroying the converter afterwards is safe.
    g.stop.store(true, std::memory_order_release);
    while (!g.consumerDone.load(std::memory_order_acquire))
        tight_loop_contents();
    const Snapshot fin = readSnapshot();
    const srt::Status st = asrc->status();
    ppmFinal = st.ppm;
    g.asrc.store(nullptr, std::memory_order_release);

    // PASS = the deployment-shape claims, made falsifiable:
    //   1. servo Locked within lockLimitMs of a cold start;
    //   2. every 1 Hz ppm sample after ppmSettleMs within +/-5 of the
    //      synthesized +200 ppm truth (and at least one such sample);
    //   3. zero underruns/overruns/resyncs after first lock — the
    //      both-cores-keeping-real-time criterion (overruns/resyncs are the
    //      signature of a consumer that cannot keep up, so they gate too).
    const std::uint64_t und = st.underruns - undAtLock;
    const std::uint64_t ovr = st.overruns - ovrAtLock;
    const std::uint64_t rsy = st.resyncs - rsyAtLock;
    const bool lockOk = locked && lockUs <= static_cast<std::uint64_t>(ph.lockLimitMs) * 1000;
    const bool cleanOk = locked && und == 0 && ovr == 0 && rsy == 0;
    r.pass = lockOk && ppmOk && ppmSampled && cleanOk;

    const double cycFrame = static_cast<double>(fin.meanCyc) / static_cast<double>(kBlockFrames);
    const double pctCore =
        cycFrame * ph.rateHz / static_cast<double>(clock_get_hz(clk_sys)) * 100.0;
    std::printf("SUMMARY %c %s: %s lock_ms=%lu ppm_final=%+.2f post_lock_und=%lu ovr=%lu "
                "rsy=%lu pull_cyc_blk mean=%lu p99=%lu max=%lu cyc_frame=%.1f core_pct=%.1f "
                "late_max_us=%lu\n",
                ph.tag, ph.desc, r.pass ? "PASS" : "FAIL",
                static_cast<unsigned long>(lockUs / 1000), ppmFinal,
                static_cast<unsigned long>(und), static_cast<unsigned long>(ovr),
                static_cast<unsigned long>(rsy), static_cast<unsigned long>(fin.meanCyc),
                static_cast<unsigned long>(fin.p99Cyc), static_cast<unsigned long>(fin.maxCyc),
                cycFrame, pctCore, static_cast<unsigned long>(fin.lateMaxUs));
    return r;
}

} // namespace

int main() {
    stdio_init_all();
    // USB CDC drops everything printed before a host terminal attaches.
    while (!stdio_usb_connected())
        sleep_ms(100);
    sleep_ms(250);

    std::printf("SampleRateTap RP2350 dual-core deployment\n");
    std::printf("sys clock %lu Hz; core0 push @ nominal*(1+%.0fe-6), core1 pull @ nominal; "
                "block %u frames\n",
                static_cast<unsigned long>(clock_get_hz(clk_sys)), kOffsetPpm,
                static_cast<unsigned>(kBlockFrames));

    multicore_launch_core1_with_stack(core1Main, gCore1Stack, sizeof(gCore1Stack));
    while (g.cyccnt.load(std::memory_order_acquire) == 0)
        tight_loop_contents(); // doubles as the launch handshake
    if (g.cyccnt.load(std::memory_order_relaxed) == 2)
        std::printf("WARN: core1 DWT has no cycle counter; pull timings will read 0\n");

    // Producer schedules bake in the +200 ppm offset (exact integer rationals):
    //   A: 48 kHz * 1.0002 = 48009.6 Hz; 32 frames = 32e6/48009.6 us = 1e7/15003 us
    //   B: 16 kHz * 1.0002 = 16003.2 Hz; 32 frames = 1e7/5001 us
    // Consumer schedules are the exact nominal rates:
    //   A: 32/48000 s = 2000/3 us;  B: 32/16000 s = 2000/1 us
    //
    // Phase B pins the 12-channel shape at 16 kHz — the README's
    // reference-microphone/AVB deployment rate — not 48 kHz: the M33 QEMU
    // baseline puts pipeline12_q15 at 10,027 insns/frame against a
    // 150 MHz / 48 kHz budget of 3,125 cycles/frame, more than 3x over, and
    // pull() of one instance is a single consumer by contract — no core
    // assignment can split it. At 16 kHz the budget is 9,375 cycles/frame.
    // The measured cycles/block is rate-independent either way, so phase B
    // still yields the real-silicon counterpart of the 12-channel baseline.
    // Its lock/settle gates scale with the servo (bandwidths * 16/48).
    const PhaseSpec phases[] = {
        {'A', "q15 2ch balanced @48000", 2, 48000.0, false, 10000000, 15003, 2000, 3, 7500, 2000,
         10000, 30000},
        {'B', "q15 12ch balanced16k @16000", 12, 16000.0, true, 10000000, 5001, 2000, 1, 2500, 6000,
         15000, 30000},
    };

    PhaseResult res[2];
    for (std::size_t i = 0; i < 2; ++i)
        res[i] = runPhase(phases[i]);

    // A skipped phase B (allocation) is reported but does not fail the
    // deployment verdict — the configuration is optional by RAM budget.
    const bool overall = res[0].ran && res[0].pass && (!res[1].ran || res[1].pass);
    std::printf("OVERALL: %s (A %s, B %s)\n", overall ? "PASS" : "FAIL",
                res[0].ran ? (res[0].pass ? "PASS" : "FAIL") : "SKIP",
                res[1].ran ? (res[1].pass ? "PASS" : "FAIL") : "SKIP");
    std::printf("SRT_PICO2_DUALCORE_DONE\n");
    while (true)
        sleep_ms(1000);
}
