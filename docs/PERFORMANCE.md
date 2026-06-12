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
| Embedded cost | **executed instructions** per output frame via QEMU TCG plugins — deterministic to the instruction, noise-free, well-correlated with real cost for scalar code | Hexagon (qemu-user), Cortex-M55 (qemu-system) |

Cycle-accurate embedded numbers require vendor simulators (Hexagon SDK
simulator, Cadence xt-run) or hardware counters (DWT.CYCCNT on M55 silicon);
the instruction metric is what CI can gate deterministically.

The benchmark matrix: sample type (float / Q15 / Q31) × filter preset
(fast / balanced / transparent) × channels (1 / 2 / 8), trimmed to the
combinations that change the answer.

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

1. **Per-channel blend redundancy**: `interpolate()` runs per channel with
   the same μ, so the coefficient blend is recomputed per channel.
   Precompute the blended row once per output frame (≤ 80 entries of
   scratch), dot-product per channel. Roughly halves inner-loop work for
   stereo; scales with channel count; makes the loop SIMD-friendlier.
2. **Auto-vectorization quality**: contiguity, aliasing, alignment of the
   history window and coefficient rows. Verify, don't assume.
3. **Fixed-point phase accumulator** (Q32.32): removes the ~10 per-sample
   double operations — invisible in host numbers, significant on
   double-less embedded FPUs. This is what the instruction metric is for.
4. **Explicit SIMD kernels** (NEON / AVX2 / Helium MVE for Q15 on M55) —
   only if budgets still demand it after 1–3.

## "Done" criteria

Optimization stops by budget, not by exhaustion. Targets are set after the
baseline lands and revised deliberately. Stop when any of:

- targets met;
- profile flat (no single hotspot ≥ 10%);
- the next win requires per-arch complexity the budget does not justify.

## Regression prevention

- **Deterministic ratchet (CI-gated)**: the QEMU instruction-count benches
  compare against a checked-in `bench/baselines.json`; a PR fails if any
  metric regresses > 3%. Improvements update the file *in the diff* —
  reviewable, with history in git.

  Mechanics: `bench/icount/` builds one fixed-workload binary per scenario
  (no argv on bare metal); `tools/qemu_insn_plugin/` is the counting
  plugin; `scripts/icount.py --target {m55,hexagon} --build-dir D --plugin
  P [--update]` runs and compares. Counts are exact across runs (verified),
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
  exactly 0.00%; x86 same-state A/B −3.7% wall-clock; Hexagon
  pipeline_float −1.6%, Q15 −6.2%, Q31 −12.3% (hexagon-clang schedules
  the dot loops substantially better once aliasing is provable).
- [ ] **PR C3…** — remaining hypotheses in ROI order, one per PR, each
  with numbers. Hypothesis 5 (deferred): explicit 4-way double
  accumulation for the float dot product — est. 2–3× float kernel on
  AVX2-class SIMD, but bit-changing; take only if budgets demand it.
