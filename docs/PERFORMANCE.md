# Performance plan

How SampleRateTap is benchmarked, optimized, regression-gated, and how the
published numbers stay honest. This is the working agreement; PRs that touch
the hot path follow it.

## Metrics

| Metric | What | Where measured |
|---|---|---|
| Throughput | ns per output frame, steady-state `pull()`+`push()`, reported as ×realtime at 48 kHz | host (Google Benchmark) |
| Tail latency | p99/max per-call time for `pull(128)` over long runs — the RT budget lives in the tail, not the mean | host |
| Kernel cost | `srt::interpolate()` in isolation (≈ all datapath cycles: taps × channels MACs) | host |
| Embedded cost | **executed instructions** per output frame via QEMU TCG plugins — deterministic to the instruction, noise-free, well-correlated with real cost for scalar code | Hexagon (qemu-user), Cortex-M55 and Cortex-M33 (qemu-system) |

Cycle-accurate embedded numbers require vendor simulators (Hexagon SDK
simulator, Cadence xt-run) or hardware counters (DWT.CYCCNT on M-class silicon —
`examples/pico2_cyccnt/` is a flashable RP2350 harness for exactly that);
the instruction metric is what CI can gate deterministically.

The benchmark matrix: sample type (float / Q15 / Q31) × filter preset
(fast / balanced / transparent) × channels (1 / 2 / 8 / 12 / 16 — 12 is
the 7.1.4 deployment shape, 16 the AVB-with-reference-mics one), trimmed
to the combinations that change the answer.

## The loop

1. **Baseline** on the benchmarks below.
2. **Profile** (`perf record` + flamegraph; `-fopt-info-vec` /
   `-Rpass=loop-vectorize` for vectorization claims).
3. **One hypothesis, one change, one PR** — each optimization PR carries its
   before/after numbers in the description.
4. **A/B**: benchmarks for speed, the full test suite for correctness — the
   pinned SNR thresholds are the quality guardrail; an optimization that
   costs dB fails CI by design.
5. Repeat until **done** (below).

### Known hypotheses, in expected ROI order

1. **Per-channel blend redundancy** (done as C1; see status below):
   `interpolate()` runs per channel with
   the same μ, so the coefficient blend is recomputed per channel.
   Precompute the blended row once per output frame (≤ 80 entries of
   scratch), dot-product per channel. Roughly halves inner-loop work for
   stereo; scales with channel count; makes the loop SIMD-friendlier.
2. **Auto-vectorization quality** (done as C2; see status below):
   contiguity, aliasing, alignment of the history window and coefficient
   rows. Verify, don't assume.
3. **Fixed-point phase accumulator** (done as Q0.64; see status below).
   Correction discovered while measuring: Cortex-M55's *scalar* FPU does
   support FP64 (only MVE is fp16/fp32), so the M55 float path was never
   soft-double-bound — Hexagon is the genuinely double-less target.
4. **Explicit SIMD kernels** — partially moot for M55: objdump confirms
   GCC already auto-vectorizes the Q15/Q31 kernels with Helium at -O2
   (the M55's ~4× Q15 advantage over the scalar M33 in the baselines is
   MVE at work). The packed dual-MAC Q15 kernel for M33/Pico-class parts
   shipped as C4 (SMLALD; those binaries now carry it); the host float
   channel axis shipped as C6. Remaining: NEON/AVX2 tap-axis work and
   embedded channel-parallel (HVX/Helium) — only if budgets demand.

## "Done" criteria

Optimization stops by budget, not by exhaustion. Targets are set after the
baseline lands and revised deliberately. Stop when any of:

- targets met;
- profile flat (no single hotspot ≥ 10%);
- the next win requires per-arch complexity the budget does not justify.

## Regression prevention

- **Deterministic ratchet (CI-gated, two-sided)**: the QEMU
  instruction-count benches compare against a checked-in
  `bench/baselines.json`; a PR fails if any metric moves more than 3% in
  *either* direction. Regressions are rejected; improvements beyond
  tolerance also fail until the baseline is re-recorded (`icount.py
  --update`) *in the diff* — otherwise the stale slack would let later
  regressions hide. Reviewable, with history in git.

  Mechanics: `bench/icount/` builds one fixed-workload binary per scenario
  (no argv on bare metal); `tools/qemu_insn_plugin/` is the counting
  plugin; `scripts/icount.py --target {m55,m33,hexagon} --build-dir D --plugin
  P [--update]` runs and compares; targets are m55, m33 (mps2-an505) and
  hexagon. Counts are exact across runs (verified),
  but they are a function of the **compiler version**: when the CI
  toolchain package updates, the ratchet job fails and the baselines get
  re-recorded in a reviewed commit — that is working as intended, not a
  flake. Hexagon counting needs a plugin-capable `qemu-hexagon` — neither
  Debian's nor the CodeLinaro toolchain's build enables TCG plugins — so
  the CI job compiles one from the pinned QEMU release (linux-user target
  only, cached).
- **Wall-clock benches are never a hard gate on shared runners** (noise);
  they run as a smoke test in CI and produce trend artifacts only.

## Docs freshness

`scripts/update_perf_docs.py` runs the host benchmarks and rewrites the
README between `<!-- PERF:BEGIN -->` / `<!-- PERF:END -->`, annotated with
machine + date. The instruction-count table
(`scripts/update_icount_docs.py`, `<!-- ICOUNT:BEGIN/END -->`) derives 1:1
from `bench/baselines.json`, and the icount-ratchet CI job regenerates it
and fails on any diff — those published numbers cannot go stale. The SNR
table is already enforced by test thresholds.

## Known debt

- **MSVC /W4 triage outstanding**: the Windows CI leg builds with
  `SRT_WERROR=OFF` until the /W4 output has been triaged (ci.yml carries
  the matching comment).
- **Tail-latency benchmark not implemented**: the Metrics table promises
  p99/max per-call `pull(128)` timing; no benchmark measures it yet.

## Sequencing & status

- [x] **PR A** — this document, Google Benchmark infrastructure
  (`SRT_BUILD_BENCHMARKS`), host baselines, README perf section + update
  script, CI bench smoke job.
- [x] **PR B** — QEMU instruction-count harness, `bench/baselines.json`
  ratchet job in CI. M55 leg gating.
- [x] **PR B2** — Hexagon leg promoted to gating, running on a
  from-source plugin-enabled qemu-hexagon (cached in CI).
- [x] **PR C1** — hypothesis 1 (per-frame blended-row precompute for
  multichannel): stereo pipeline −36% wall-clock on x86, −15/−30/−21%
  instructions (float/Q15/Q31) on M55, 8-channel −52% wall-clock;
  Hexagon −3.6/−3.3/−0.2% (its pipelines are dominated by per-sample
  soft-double phase math — the C3 motivation); mono kernels
  count-identical on both targets (control). Outputs unchanged
  bit-for-bit.
- [x] **PR C2** — vectorization audit (hypothesis 2). Verified with
  -fopt-info-vec: blendRow vectorizes (was alias-versioned; SRT_RESTRICT
  removes the runtime check), Q15 dotRow auto-vectorizes, float dotRow is
  scalar **by design** (strict double accumulation forbids reassociation;
  vectorizing requires explicit multi-accumulator partial sums, which
  changes output bits — recorded below as deferred hypothesis 5), Q31
  dotRow is scalar (no packed 64-bit multiply in baseline ISAs). restrict
  measured: M55 pipeline_float −1.35% instructions, all other scenarios
  exactly 0.00%; x86 same-state A/B −3.7% wall-clock.
- [x] **PR C3** — Q0.64 fixed-point phase accumulator: per-sample path is
  integer-only (slips via 64-bit wraparound; blend factor from phase bits;
  eps converted once per block). M55: Q15 −5.3%, Q31 −4.6%, float +1.4%
  (M55 has scalar FP64 hardware — see corrected hypothesis 3 — so its
  float path traded cheap HW doubles for int64 ops; accepted for the
  cross-target win). x86 same-minute A/B: float −5.4%, Q15 −12.0%.
  Quality *improved*: 135.0 dB at 997 Hz (2^-64 phase vs 2^-52).
  Hexagon (from the PR's gating run): pipeline Q31 −15.5%, Q15 −10.3%,
  float −2.6% — the per-sample soft-double phase math C1 identified as
  dominating Hexagon's pipelines is now gone; kernels count-identical
  (control).
- [x] **PR C4** — SMLALD Q15 dot product for DSP-extension cores without
  Helium (M33/M4/M7 class; gated on `__ARM_FEATURE_DSP &&
  !__ARM_FEATURE_MVE` so the M55 keeps its auto-vectorized loop — verified
  0.00% on every M55 and Hexagon scenario). Bit-exact by construction:
  exact int32 products, associative int64 accumulation; Q15 mono routes
  through blendRow+dotRow on these targets to reach the dual-MAC loop.
  M33 pipeline_q15 −3.1%. Honest accounting: the win is bounded because
  the M33 Q15 frame cost is dominated by the coefficient blend's 64-bit
  products (`fr * diff >> 15`, one smull each) and transport, not the dot
  product; a packed blend would change the documented int64 blend
  invariant and is not worth it at current budgets. The kernel_q15
  scenario measures the fused `interpolate()` call, which is intentionally
  unchanged (the converter no longer uses it on these targets).
- [x] **PR C5** — Hexagon wide-MAC Q15 dot product: **measured negative
  result, code intentionally not kept.** A `vrmpyh` intrinsic loop (four
  exact 16x16 products per instruction into the int64 accumulator — the
  C4 argument, 4-wide) was implemented, passed the full suite on Hexagon
  QEMU bit-exactly, and measured only **−0.31%** on pipeline_q15
  (119,847,854 → 119,478,758). Disassembly comparison (CI llvm-objdump,
  pre/post): the baseline binary contains **zero** wide MACs and the
  intrinsic build contains 10 — so the compiler had not already done
  this; the gain is genuinely tiny because Hexagon's scalar ISA already
  has single-instruction 64-bit MACs (`Rxx += mpy`) and 64-bit loads,
  and the 2-byte-aligned history window costs combine work that eats the
  rest. Per the stop rule (per-arch complexity must justify itself), the
  intrinsic was reverted and this note is the deliverable. Note for HVX
  proper: a 48–80-tap dot does not fill one 128-byte vector and HVX
  16-bit MACs accumulate in 32-bit lanes, overflowing the exact-int64
  invariant after ~24 worst-case taps — per-channel dots are the wrong
  shape for HVX. The HVX-compatible shape is **channel-parallel** (one
  64-bit lane-pair per channel; 16 channels fill one vector exactly),
  recorded as hypothesis C6 below.
- [x] **PR C6** — channel-parallel dot for high channel counts
  (frame-major history + register-blocked 8/4/2/1 channel tiles,
  `SRT_CP_MIN_CHANNELS` = 4, hosts only). Profile first (callgrind,
  12ch Q15): per-channel dot MACs ≈ 85% of instructions, deinterleave
  ~2% — the dots were the target. Results, same-minute A/B:
  **float 8/12/16-channel −38/−38/−42% wall-clock with AVX2+FMA**
  (`-march=native`; −4–5% on baseline SSE2 builds — gains scale with
  SIMD width because the channel axis is the *only* axis the float path
  may vectorize on: per-channel double accumulation order is unchanged,
  so it is bit-exact, hash-verified against planar over 30k blocks ×
  4 configs). **Fixed-point: negative result, planar kept** — the
  channel-parallel form measured ~1.5× SLOWER than planar Q15 on hosts
  (planar already auto-vectorizes over taps; integer reduction is
  exactly reassociable, so that axis was never blocked). Two
  implementation lessons recorded: a naive channels-inner loop with
  memory accumulators is 2.8× slower than planar (register-block or
  don't bother), and the mode gate must be compile-time — a runtime
  bool in the hot loops cost +6–8% on the M55 ratchet before the
  constexpr gate restored every embedded scenario to 0.00%.
  Embedded channel-parallel (HVX 16×int64-lane, Helium) remains a
  follow-up candidate if DSP budgets demand it. Hypothesis 5
  (deferred): explicit 4-way double accumulation for the float dot —
  bit-changing; superseded for N≥4 by C6's bit-exact channel axis, only
  relevant for mono/stereo float if ever needed.
