# SampleRateTap — Synchronous Engine Handoff

> **Status (2026-07-23): superseded in part by [PLAN.md](PLAN.md).**
>
> This is the original design brief that motivated RatioTap, preserved as
> provenance. Its DSP content (§3–§6) remains the working reference. The
> following decisions were revised during planning — where this document and
> PLAN.md disagree, PLAN.md wins:
>
> 1. **Separate repo** (reverses §2 "Not a separate repo"). The rejection
>    rested on duplicating the measurement harness; the Tap family already
>    shares code via DspTap submodules, so the shared design/kernel/measurement
>    layer moves to DspTap and both converters consume it as true siblings.
>    RatioTap's production dependency is DspTap only; SampleRateTap is a
>    test-only dependency (cross-validation).
> 2. **Cross-validation redesigned** (§2 "within servo ripple" is unworkable).
>    SampleRateTap is a *near-unity* ASRC; its occupancy servo cannot acquire
>    an 8% ratio offset. The golden-reference test instead pins
>    eps = L/M − 1 directly at the `fractional_resampler` level (no servo),
>    exhaustive over all phases, with the agreement bound set by the ASRC's
>    phase-table interpolation floor — a sharper, deterministic bound.
> 3. **"Precomputed at build time" → construction time** (§3). Follows the
>    family philosophy documented in the kaiser design note: runtime design in
>    the constructor, off the audio path. Generated/committed tables are a
>    later, measured optimization (see PLAN.md lever ladder).
> 4. **Factory / one API dropped** (§2 diagram). The engines' natural APIs are
>    irreconcilable (two-thread push/pull vs single-thread process). Clock
>    topology is routed by *type choice*, documented at the API surface; the
>    async-at-44.1↔48 case is served by composition (see PLAN.md,
>    `bluetooth_bridge`).
> 5. **Scope narrowed further** (§1). RatioTap is 44.1↔48 *speed-first*:
>    direction is compile-time, generality is deliberately boxed out, and the
>    optimization budget goes to this one ratio.
> 6. **Lever §5.2 (channel vectorization) already exists** in SampleRateTap
>    (`dot_rows_frame_major`, hypothesis C6) and is inherited via DspTap
>    rather than built.
> 7. **Profiles map to the family vocabulary**: the ~70 dB/19 kHz default is
>    `economy`, the 120 dB/20 kHz profile is `transparent`.

---

**Goal:** Add a dedicated *synchronous* fixed-ratio 44.1↔48 kHz converter to SampleRateTap as a sibling engine to the existing asynchronous ASRC. Same repo, shared design/kernel layer, distinct hot loop.

This doc is the design brief. It records *why* each decision was made so implementation choices can be re-derived rather than guessed at.

---

## 1. Scope

- Convert **44.1 kHz ↔ 48 kHz only**. No other rates, no arbitrary ratios.
- **Synchronous**: input and output share one clock. The ratio is exactly rational and fixed.
  - 44.1→48 : L=160, M=147
  - 48→44.1 : L=147, M=160
- This is the *degenerate case* of the ASRC: a known rational ratio that never drifts. The whole point is to exploit that to drop everything the async engine needs for a moving ratio.

**Out of scope / common trap:** nominal-44.1-into-48-from-a-different-crystal (e.g. S/PDIF into a device on its own clock) is still the *async* problem at the same nominal ratio. Which engine applies is a property of the **clock topology, not the number**. Do not route by rate.

---

## 2. Relationship to the async engine

Three rejected alternatives and the chosen structure:

**Not a mode of the ASRC.** Pinning the async ratio to 147/160 works but pays the coefficient-interpolation tax on every sample forever and never yields the phase-table inner loop, straight-line superblock, or bit-exact repeatability. The two engines have genuinely different hot loops.

**Not a separate repo.** Everything above the inner loop is shared: prototype design, polyphase decomposition, coefficient layout, SIMD dot-product kernels, and — most importantly — the measurement harness. Duplicating the SNR/alias test infra across two repos is pure liability.

**Chosen shape:**

```
                 ┌─────────────────────────────┐
                 │   shared design + kernels    │
                 │  - prototype design          │
                 │  - polyphase decomposition   │
                 │  - coefficient layout        │
                 │  - SIMD dot-product kernels  │
                 │  - measurement harness       │
                 └──────────────┬──────────────┘
                        ┌───────┴───────┐
                 ┌──────┴──────┐ ┌──────┴──────┐
                 │ AsyncEngine │ │ SyncEngine  │
                 │ servo +     │ │ phase table │
                 │ interpolated│ │ (new)       │
                 │ polyphase   │ │             │
                 └─────────────┘ └─────────────┘
                        └───────┬───────┘
                          factory / one API
```

**API:** the caller declares clock topology explicitly — `SharedClock` selects Sync, `IndependentClocks` selects Async. Do **not** infer rationality from a float ratio.

**The relationship that pays for itself:** the sync engine is a **golden reference** for the ASRC. At a pinned rational ratio the async output should converge to the sync output within servo ripple — a sharp, automatable cross-validation test neither engine yields alone. The period-147 phase structure means every phase can be covered *exhaustively*, not statistically. This also becomes the "degenerate case" chapter of the white paper (the cleanest way to explain an ASRC: here's the exact rational machine; async is what you build when the ratio won't hold still).

---

## 3. Core algorithm

Single-stage polyphase FIR, fixed rational ratio, **all coefficients precomputed at build time**. No coefficient interpolation, no fractional-delay machinery, no servo. The phase sequence has period 147 (or 160) and repeats forever.

**Build two prototypes, not one** — the directions are asymmetric:

| Direction | L/M | Transition band | Relative cost |
|---|---|---|---|
| 44.1→48 | 160/147 | 20 → 24 kHz | cheaper (~½) |
| 48→44.1 | 147/160 | 20 → 22.05 kHz | dominant |

The 48→44.1 stopband edge is forced to 22.05 kHz by aliasing; it's roughly twice the filter. Do not share one prototype run transposed — you'd pay ~2× in the cheap direction for nothing.

**Coefficient layout:** phase-major, so each output is one contiguous dot product of taps-per-phase length — straight into MVE/HVX with no gather.

**Schedule:** precompute a 147- or 160-entry table of `(phase, input_advance)`. Advance is 0/1 going up, 1/2 going down.

**Numerics:** float32 accumulation is fine at ~200 taps (error floor ~−138 dB). Fixed point wants int32 coefficients with int64 accumulate.

---

## 4. Spec relaxation — do this FIRST (biggest lever)

**The spec is protecting ultrasound.** Going 48→44.1, a 48k source holds nothing above 24 kHz, and aliasing maps f → 44100−f. So the entire possible alias landing zone is **20.1–22.05 kHz**. Nothing can fold below 20.1 kHz — arithmetically impossible. Upward, images land at 44100−f ≥ 22.05 kHz.

So a 120 dB stopband here buys *ultrasonic* cleanliness, not audible transparency. At 60–70 dB every alias product sits above 20 kHz at ≤ −60 dBFS.

This is the cheapest 2× available; take it before any structural change.

| Stopband | MACs/output (48→44.1) |
|---|---|
| 120 dB | ~183 |
| 100 dB | ~150 |
| 80 dB | ~117 |
| 60 dB | ~85 |

Passband edge 20 kHz → 19 kHz gives another 1.49× on top, zero phase cost.

**Recommendation:** target ~70 dB stopband, 19 kHz passband edge as the default profile. Keep 120 dB available as a "pristine/offline" profile behind the same design path.

---

## 5. Optimization levers, in priority order

1. **Relax the spec (§4).** ~2× MACs and ~2× storage. Free.
2. **Vectorize across channels, not taps.** For multichannel/HOA (AmbiTap B-format), broadcast each coefficient and run N channels in parallel lanes — no horizontal reduction, perfect lane utilization independent of taps-per-phase, coefficient load amortized across the set. On HVX (32 float lanes) a 16-ch conversion is nearly free vs mono. Usually the biggest real-world win.
3. **Multistage decomposition.** 147/160 = (6/5)(7/8)(7/8) → 48 → 57.6 → 50.4 → 44.1 kHz. Early stages get wide transition bands (their aliasing lands in don't-care regions the final stage cleans up); only the last stage carries the sharp filter, at small L. Typically **5–10× less coefficient storage** for similar/better MAC count. IFIR on the sharp stage is a good additional fit (genuinely narrow transition).
4. **Elliptic/Cheby-II pre-filter** (see §6). ~15 IIR MACs buys back ~100 FIR MACs at a given quality.
5. **Polyphase symmetry.** Linear-phase prototype ⇒ subfilter *p* is the time-reverse of *L−1−p*. Store half the phases, index backward. Clean 2× on storage, zero cost. (MAC-folding only works cleanly in the pure-decimator case — don't chase it.)
6. **Superblock codegen.** Phase schedule has period 147/160 — emit straight-line code for one full superblock at build time. No modulo, no branches, perfectly scheduled loads. ~1.5–2× wall-clock on Helium vs a generic indexed loop. This is where the M55/Hexagon builds earn it.
7. **Minimum phase.** Smaller than folklore: spectral factorization needs the linear-phase prototype designed to δs² first, so ~1.25× on MACs (60 dB: 62→52; 120 dB: 156→123 taps). The real payoff is **latency** (~78 → ~18 samples). Composes with the elliptic pre-filter — different slots.
8. **FFT-domain resampling** (offline/file path only). Overlap-save with *different* fwd/inv transform lengths (FFT 147k in, IFFT 160k out). Transition sharpness is free. ~28–50 MAC/out at 6–31 ms latency vs 183 direct. Wants a mixed-radix FFT (1470 = 2·3·5·7² → radix-7). Poor fit for live; compelling for the file-domain tools.

---

## 6. Optional IIR pre-filter

Bilinear prewarping helps a lot: 22.05 kHz is 92% of Nyquist at 48k, stretching the 1.10 analog ratio to ~2.04. Orders for 0.01 dB ripple, 60 dB stop (verified):

| Type | Order |
|---|---|
| Butterworth | 14 |
| Chebyshev II | 8 |
| **Elliptic** | **6** |

Butterworth is the wrong choice (2.3× the poles for a maximally-flat passband nobody needs behind a 0.001 dB FIR). **Chebyshev II (8th order)** is the sensible default — flat passband, zeros on the unit circle. Elliptic (6th) if you want minimum order and can tolerate passband ripple.

- Place at **input rate, pre-upsample** (cheapest slot; for 48→44.1 must be pre-decimation anyway).
- Group delay: ~0.7 samples through most of the band, peaking ~27 samples (0.56 ms) at the corner — localized above 18 kHz, essentially inaudible alone, but it *accumulates* on round-trips.
- Conditioning: poles at 0.83–0.92 of Nyquist. Use **SOS, transposed DF-II**, double precision (or a carefully chosen Q-format before trusting it on M55).
- **Freebie:** fold the inverse of the IIR passband magnitude into the FIR design target — droop cancels exactly, zero runtime cost. Magnitude only; leave phase alone.

---

## 7. Suggested first tasks for Claude Code

1. Introduce the engine split: extract the shared design/decomposition/kernel/measurement layer from the current ASRC; define `AsyncEngine` and `SyncEngine` behind a factory keyed on `SharedClock` / `IndependentClocks`.
2. Implement the offline prototype designer for both directions with a **profile** parameter (default: 70 dB / 19 kHz; pristine: 120 dB / 20 kHz).
3. Implement the single-stage phase-table `SyncEngine` (phase-major layout, `(phase, advance)` schedule). Get it bit-exact and correct before optimizing.
4. Wire the **cross-validation test**: async pinned to 147/160 vs sync, exhaustive over all phases, assert convergence within servo ripple.
5. Then optimize in the §5 order: channel-vectorized kernel → multistage decomposition → superblock codegen for the embedded targets.

**Acceptance for the first pass:** correct output, exhaustive phase coverage in the cross-validation, and the default profile hitting ≤ −60 dBFS on all alias products above 20 kHz. Optimization lands after correctness.

---

## 8. Notes

- Latency budget: linear-phase single-stage ~45–90 input samples (1–2 ms). Go minimum-phase only if that's actually a problem.
- The Max/MSP package angle is weak here — an MSP chain runs at one rate. Expect the real consumers to be the embedded targets (M55 w/ Helium, Hexagon/HVX on QCS8550) and the file-domain tools.
- License/venue consistent with SampleRateTap (MIT, docs at timothy.place/SampleRateTap/).
