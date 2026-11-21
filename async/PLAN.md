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

- Header-only C++20 over `tap::dsp` only. `basic_converter<Sample, Ch>` with the
  `converter<Ch>` (float), `converter_q15<Ch>` and `converter_q31<Ch>` profiles; float
  is the golden model, the fixed-point profiles carry their numbers as
  contracts (`sample_traits.h`).
- **The channel count is the type's (since 0.7.0, the family's A13 and D18;
  `../docs/CHANNELS_PLAN.md`).** `Ch`, a compile-time constant of at least
  1, is `basic_converter<S, Ch>`'s and `fractional_resampler<S, Ch>`'s
  second parameter and the aliases' only one; zero fails to compile
  (`async.Converter.ZeroChannelsFailToCompileWithTheMessage`). `config` has
  no channel count, and the precheck is the converter type's,
  `basic_converter<S, Ch>::validate(cfg)` (K4: its checks need the count).
  The configuration, bank, ring, servo, setpoint state and telemetry, with
  their construction and restart, are `converter_base<S>`'s, built once per
  format (K12); the mono branch, the history layout and the divisions by
  the count are compile-time (K5), and the histories are a `std::array` of
  lines (K6). Before 0.7.0 the count was `config::channels`, a run-time
  value.
- Real-time safe by construction: all allocation and filter design in the
  constructor (which may throw on a bad `config`); `push()`/`pull()` are
  `noexcept`, lock-free and allocation-free, one producer thread and one
  consumer thread.
- Designed latency ≈ 1.5 ms at 48 kHz with the defaults (filter group delay
  + FIFO setpoint), reported by `designed_latency_seconds()`; telemetry
  through `status()`.
- Measured quality is a number, not a claim: the README's table and the
  executed notebooks (`notebooks/`, through the C ABI) pin the default
  profiles' SNR figures; `bench/icount/` pins the instruction counts on
  M33, M55 and Hexagon two-sided at ±3 %.
- The C ABI (`capi/`, `libtap_sr_async_capi`): fourteen `tap_sr_async_*`
  functions in all — the float surface, `create_format` / `format`, the
  `_q15` / `_q31` push and pull (the family's shape since `rational`'s ABI
  set it, each format pinned bit for bit against `basic_converter<S, Ch>`;
  one and two channels served natively, any other count NULL, A13's K8,
  since a fan-out would give each channel a servo of its own) and
  `tap_sr_async_version()`, the family's bit-packed version (D13); the
  symbol set is pinned by `async.Family.ExportedSymbolsArePinned`.

## 3. Status

**v0.8.0** — the family version (D13), in the SampleRateTap tree since the
2026 migration; 0.8.0 is `rational`'s release (`docs/X4_X8_PLAN.md`,
`CHANGELOG.md`) and changes nothing here but the version; 0.7.0 moved the
channel count into the type (A13, the
record in section 6); before it, SampleRateTap v0.1.0 (the campaign's PR A–C6 in
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
   *Done* (2026-10-09, 43f3e29): `construct_q15` is a gated scenario
   beside the pipelines, so `pipeline_q15 − construct_q15` is the streaming
   loop alone (`docs/PERFORMANCE.md`, "`construct_q15` — construction as a
   gated scenario").
2. **Timestamp clock recovery.** Accept per-block timestamps for sub-sample
   phase observation, so the servo no longer depends on FIFO counts alone
   and block-quantized transfer stops wobbling the ppm estimate (README,
   "Limitations").
3. **Tail latency.** The p99/max per-call `pull(128)` benchmark the
   metrics table promises. *In part* (the roadmap's A6): the worst single
   `pull()` is gated in the QEMU ratchet (`callmax_pull_float`,
   `callmax_pull_q15`); the host p99/max is measured on the bench in the
   roadmap's Block B and recorded as a trend, never gated on CI.
4. **MSVC `/W4` triage**, then `TAP_SR_ASYNC_WERROR=ON` on the Windows leg.
   *Done* (the roadmap's A2): the one warning was C4324 on `spsc_ring`'s
   cache-line padding, off for async's own targets with its reason.
5. **Hexagon exceptions.** The static-musl toolchain terminates on a
   constructor throw instead of propagating (`ConfigValidation` is excluded
   there). *Done* (the roadmap's A2): the static link lacked
   `--eh-frame-hdr`, not an unwinder; with it in
   `cmake/hexagon-linux-musl.cmake` throws propagate and `ConfigValidation`
   runs on the leg (`docs/PERFORMANCE.md`, "Resolved"). A3's precheck,
   `basic_converter<S, Ch>::validate()` since A13, stays the answer wherever
   a throw cannot propagate.
6. **Housekeeping from the migration.** Bring `bench/icount/` under the
   clang-tidy gate (excluded by path in `style.yml`). *Done* (the roadmap's
   A2): `icount_main.cpp` is tidy-clean and the exclusion is gone. *Also done:* the Hexagon
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

## 6. Records

- **A13 — the channel count in the type (A13d, 2026-11-18; the family's D18,
  `../docs/CHANNELS_PLAN.md`).** `basic_converter<S, Ch>` and
  `fractional_resampler<S, Ch>`: the count is the second parameter, at least 1
  and with no default, and the aliases take it alone (`converter<2>`). The
  configuration, bank, ring, servo, setpoint state and telemetry are
  `converter_base<S>`'s, its constructor and restart out of line (K12: built
  once per format); `config` loses `channels`, and `config::validate` is
  retired for `basic_converter<S, Ch>::validate` (K4, no alias: D7); the mono
  branch, the history layout and the divisions by the count are
  compile-time (K5), and the histories are a `std::array` of lines (K6, one
  allocation fewer: the footprint's `H sizeof(std::vector)` term is gone,
  48 B per stereo converter on a 64-bit host). The C ABI serves one and two
  channels natively and returns NULL for any other count (K8). Measured
  against gate 0's same-job reference (2ed199a; `main` before A13d counted
  identically on every row) on every target in both configurations, every
  checksum equal and no row rising: M55 −10.82 … +0.00 % (streaming sum
  −1.94 % with exceptions, −1.95 % without), M33 −3.07 … +0.00 % (−0.20 %,
  −0.18 %), Hexagon −2.03 … +0.00 % (−0.25 %, −0.26 %), host −4.21 % at 2
  channels and −2.51 % at 12 (−2.91 %); the kernel rows, which construct no
  converter, do not move. At 16 channels, the plan's check that a large
  constant count does not regress, the host pipeline counts −1.68 %
  (443,393,957 against 450,976,436, the same checksum). Two M55 rows widen
  the gap between the two configurations past gate 0's 0.1-point margin:
  `pipeline_float` (the reference's builds −0.084 % apart, A13d's −0.190 %)
  and `pipeline_q31` (+0.008 %, +0.124 %), both builds of each row below the
  reference by 2 … 3.4 %. The maintainer accepted both (2026-11-18) with one baseline each, recorded from the exceptions build as every other row: the build without exceptions sits −0.19 % (float) and +0.12 % (Q31) from it, inside ±3 % with no midpoint. The baselines are re-recorded from
  the exceptions build on every target. The ceilings fell on all three
  targets: engine `.text` −1.5 … −4.8 %, the probes' images −0.1 … −1.5 %,
  engine `.rodata` −8 B on the Arm targets, the M33's `pull()` stacks −24 B
  (float) and −32 B (Q15). The numbers are in
  `../docs/channels/a13d_measurements.json`.
