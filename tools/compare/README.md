# tools/compare — the family's resampler-comparison tooling

What every engine's comparison (`async/docs/COMPARISON.md`,
`bridge/docs/COMPARISON.md`) measures the competitors with, once, so the host
benchmarks, the embedded instruction counts and the executed notebooks of
every engine measure the same pinned engines:

- `r8brain.cmake`, `speexdsp.cmake`, `libsamplerate.cmake` — the competitor
  fetch recipes at commit (or release-digest) pins, as `tap_sr_cmp_*` targets.
  libsamplerate and soxr come from the system packages on the host
  (pkg-config); the libsamplerate recipe serves the cross-compiled counts.
- `shim/` — the ctypes shims the notebooks load (`TAP_SR_BUILD_COMPARE_SHIM`):
  r8brain and SpeexDSP have no maintained Python binding, and python-soxr
  exposes only soxr's named recipes, not the custom quality spec the matched
  rows need.
- `r8b_single_thread_mutex.h` — the no-op `std::mutex` the bare-metal r8brain
  workloads force-include (the header says why that is exact).
- `warnings.cmake` — the family's warning policy for this tooling's own
  sources.

Comparison-only, every piece of it: nothing here is ever linked into an
engine or its tests, and the engines never depend on each other through it
(PLAN.md 4.2).
