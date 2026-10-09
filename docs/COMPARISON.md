# The family, compared

Each `tap::sr` engine answers a different question, so each is compared on
its own terms, against the general-purpose resampler libraries
(libsamplerate, soxr, r8brain-free-src, SpeexDSP) under one measurement
discipline: the same AES17-style instrument applied identically to every
subject through the shipping C++, host throughput measured in one session
and labelled indicative, and executed instructions per output frame on
Cortex-M55, Cortex-M33 and Hexagon from committed records that CI keeps in
step with the published tables. This page is the index; the numbers live
with their engine.

| Engine | The question | Compared against | Where |
|---|---|---|---|
| [`async`](../async/README.md) — absorbs the clock | What does clock recovery cost, at near-unity, against libraries that must be handed the ratio? | libsamplerate, soxr, r8brain, SpeexDSP (both builds); hardware ASRCs and OS engines in the landscape table | [`async/docs/COMPARISON.md`](../async/docs/COMPARISON.md), [`async/notebooks/asrc_comparison.ipynb`](../async/notebooks/asrc_comparison.ipynb) |
| [`bridge`](../bridge/README.md) — converts the number | At exactly 44.1 ↔ 48 kHz on one clock, what does each library need to reach `economy` and `transparent`, and what does it pay for it? | the same four, at matched spec and at their frontier, both directions | [`bridge/docs/COMPARISON.md`](../bridge/docs/COMPARISON.md), [`bridge/notebooks/bridge_comparison.ipynb`](../bridge/notebooks/bridge_comparison.ipynb) |
| [`rational`](../rational/README.md) — L/M inside a family | (planned: ↑2, ↓2, 3/2 and 2/3 against the same libraries) | | |

## Headline numbers

- **async.** SampleRateTap measures −134 dB THD+N / 149 dB DR at the 24-bit
  interface with the servo in the loop, ~10 dB from the oracle-fed
  libraries' format ceiling: the measured price of clock recovery. On the
  Cortex-M33 its Q15 datapath costs 879 instructions per stereo frame in
  steady state; SpeexDSP's fixed-point build, the one competitor with a
  fixed-point path, 2.0× that at its passband knee, and the float-only
  libraries 30–56×.
- **bridge.** `economy` measures its 70 dB design (−91 / −86 dB THD+N by
  direction) at 29 / 19 input frames of latency; r8brain is the one library
  that offers that design point, the others overshoot it by 15–50 dB to keep
  the 18 kHz passband. `transparent` and every frontier setting measure at
  the 24-bit ceiling. The cost tables are in the engine's document.

## The shared tooling

The competitors are fetched at commit pins and built once for every engine
by [`tools/compare/`](../tools/compare/README.md): the fetch recipes, the C
shims the notebooks load (r8brain and SpeexDSP have no maintained Python
binding; python-soxr lacks the custom quality spec), and the bare-metal
mutex stub r8brain needs. Comparison-only, never linked into an engine or
its tests. The embedded counts come from one manual workflow,
[`compare.yml`](../.github/workflows/compare.yml), harvested into one record
per engine (`<engine>/bench/compare_counts.json`) that
`scripts/update_compare_docs.py` derives the tables from and the
`compare-docs` CI job gates.
