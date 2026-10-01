# `async` — Plan

The plan of the `tap::sr::async` engine. The family plan,
[`../PLAN.md`](../PLAN.md), settles what is shared: the engine names and
charters (section 2), the coverage rule that places this engine among its
siblings, the dependency rule (4.2) and the decisions D1–D16. This file is
the engine's own charter, contract and roadmap. The design itself is
written up at length in [the book](https://tap.github.io/SampleRateTap/)
(`../book/`), and the measurement campaign that shaped the code in
[`docs/PERFORMANCE.md`](docs/PERFORMANCE.md).

## 1. Charter

**`async` absorbs the clock.** Two audio clock domains at nominally the
same rate, sourced from independent oscillators, each within
`max_deviation_ppm` (default 1000 ppm) of nominal and drifting slowly: one
thread pushes at the input clock, one pulls at the output clock, and the
converter tracks and absorbs the rate mismatch, whole-sample slips
included.

The boundaries are identity, not policy:

- **Near-unity only.** No 44.1 ↔ 48 kHz, no 2:1, no rational L/M. Those
  are the synchronous engines' charters (`bridge` today; `rational` next),
  and 44.1 ↔ 48 across independent clocks is their *composition* with this
  engine (`../bridge/examples/bluetooth_bridge.cpp`): the sibling converts
  the *number*, `async` absorbs the *clock*.
- **Never routed to by rate (D12).** The 1000/1001 pull-down rates sit
  inside ±1000 ppm and must still never be served by this engine: they are
  synchronous ratios. The caller chooses this type because the clocks are
  independent, not because the numbers are close.
- **Timestamp clock recovery is a feature of this engine**, when it comes,
  not a new engine (family plan, section 2).

## 2. Contract

- Header-only C++20 over `tap::dsp` only. `basic_converter<Sample>` with the
  `converter` (float), `converter_q15` and `converter_q31` profiles; float
  is the golden model, the fixed-point profiles carry their numbers as
  contracts (`sample_traits.h`).
- Real-time safe by construction: all allocation and filter design in the
  constructor (which may throw on a bad `config`); `push()`/`pull()` are
  `noexcept`, lock-free and allocation-free, one producer thread and one
  consumer thread.
- Designed latency ≈ 1.5 ms at 48 kHz with the defaults (filter group delay
  + FIFO setpoint), reported by `designed_latency_seconds()`; telemetry
  through `status()`.
- Measured quality is a number, not a claim: the README's table and the
  executed notebooks (`notebooks/`, through the C ABI) pin the default
  presets' SNR figures; `bench/icount/` pins the instruction counts on
  M33, M55 and Hexagon two-sided at ±3 %.
- The C ABI (`capi/`, `libtap_sr_async_capi`): eight `tap_sr_async_*`
  functions and `tap_sr_async_version()`, the family's bit-packed version
  (D13).

## 3. Status

**v0.4.0** — the family version (D13), in the SampleRateTap tree since the
2026 migration; before it, SampleRateTap v0.1.0 (the campaign's PR A–C6 in
`docs/PERFORMANCE.md` "Sequencing & status"). The DspTap adoption
(`bridge/PLAN.md` Appendix B) moved the FIR substrate out; the
engine-owned datapaths — `fractional_resampler`, the polyphase bank, the
blend stratum — stay here (D11).

## 4. Roadmap

In expected order; each item lands as its own PR with its measurements.

1. **Construction as a ratcheted scenario.** Split the constructor's
   design cost (≈ 7 M double flops; ≈ 0.9 G instructions on the soft-double
   QEMU M33) out of the pipeline workloads — a construct-only scenario, or
   the two-length differencing the comparison workloads already use — so
   the ±3 % gate regains per-frame sensitivity on M33 and construction
   becomes a first-class number (`docs/PERFORMANCE.md`, "Known debt").
2. **Timestamp clock recovery.** Accept per-block timestamps for sub-sample
   phase observation, so the servo no longer depends on FIFO counts alone
   and block-quantized transfer stops wobbling the ppm estimate (README,
   "Limitations").
3. **Tail latency.** The p99/max per-call `pull(128)` benchmark the
   metrics table promises.
4. **MSVC `/W4` triage**, then `TAP_SR_ASYNC_WERROR=ON` on the Windows leg.
5. **Hexagon exceptions.** The static-musl toolchain terminates on a
   constructor throw instead of propagating (`ConfigValidation` is excluded
   there); candidate fix `-unwindlib=libunwind` in
   `cmake/hexagon-linux-musl.cmake`.
6. **Housekeeping from the migration.** Bring `bench/icount/` under the
   clang-tidy gate (excluded by path in `style.yml`). *Done:* the Hexagon
   baselines, which the family migration left 47–87 instructions high per
   pipeline workload (the renamed literals moved the marker's format
   string; `../PLAN.md` section 7), are re-recorded, and the marker
   print's cost is layout-independent: the format string is 64-byte
   aligned (`docs/PERFORMANCE.md`, "Hexagon marker alignment").

## 5. Non-goals

- Any rate pair other than near-unity (see §1).
- A `(in_hz, out_hz)` lookup, here or anywhere in the family (D12).
- Absorbing DspTap's `decimate.h`, or any building block that belongs in
  the substrate (D11): shared code lands in DspTap first.
