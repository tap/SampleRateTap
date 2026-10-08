# The converter, and the campaign that made it fast

> We should forget about small efficiencies, say about 97% of the time: premature optimization is the root of all evil. Yet we should not pass up our opportunities in that critical 3%.
>
> — Donald Knuth, "Structured Programming with go to Statements"

`basic_converter<S, D, K>` is the table, the schedule, planar delay lines
per channel, and four entry points: `process`, `pull`, `flush`, `reset`.
This chapter reads the contract first, then the three measured codegen
levers that took the hot path from a correct loop to the numbers in the
README — and it reads the one lever that measured at zero and the one
finding that measured something nobody expected, because those are the
pages the ratchet earned.

## The contract

```cpp
{{#include ../../../bridge/include/tap/sr/bridge/converter.h:rt_converter_doc}}
```

Zero-primed and causal: output n is Σₖ x[⌊nM/L⌋ − k] · h[phase(n) + kL]
with x[< 0] = 0, which is `scipy.signal.upfirdn`'s streaming prefix, sample
for sample from n = 0 with the transient included. Latency is the
prototype's linear-phase group delay, (L·T − 1)/(2L) ≈ T/2 input samples:
29 going down at `economy` (0.60 ms) and 19 going up (0.43 ms). Every
channel of an instance shares the coefficient row per frame, so
inter-channel phase coherence is exact by construction — Part V's channel
rule, which this engine states the same way.

The two call shapes exist because the composition at the end of this part
needs one on each side of the ASRC:

```cpp
{{#include ../../../bridge/include/tap/sr/bridge/converter.h:rt_pull}}
```

`pull()` produces exactly N frames, drawing input through a callback that
may deliver fewer than asked; a dry source returns short with the partial
consumption retained, so delivering more input later resumes exactly where
the stream left off (`Converter.PullShortReturnsOnDryThenResumes`), and
the result is bit-identical to `process()` on the same stream. The
`static_assert` on the callback's `noexcept` is the family's pattern: a
`noexcept` function that calls a throwing callback could only terminate,
so the compiler is asked to refuse the combination.

```cpp
{{#include ../../../bridge/include/tap/sr/bridge/converter.h:rt_frames_needed}}
```

`outputs_for` is the sizing companion to `process` and `frames_needed` the
companion to `pull`, and both are closed-form from the current position —
the line that solves ⌊(pos + N − 1)M/L⌋ ≤ base + a for N is the kind of
arithmetic the ASRC's moving μ never permits. `Converter.AccountingExactFromEveryPosition*`
checks them from every one of the L positions of each superblock.

## Lever 1: the superblock walk

The first `process()` was the obvious loop — per frame, append; if the
schedule says emit, dot the row — and it was correct, pinned against the
scipy vectors, and slow in a way the instruction counter could see. The
state machine lived in member variables that the compiler had to reload
and store around every call into the dot kernel; every frame did an index
multiply for its channel offset; the channel loop ran even for mono.

```cpp
{{#include ../../../bridge/include/tap/sr/bridge/converter.h:rt_superblock_walk}}
```

The walk settles the trip count by `outputs_for()` arithmetic before the
first sample moves, so the loop has no exhaustion checks; the schedule
cursor, history end and pending gap live in locals the compiler keeps in
registers; input and output advance by pointer bumps; and the mono and
stereo shapes — the deployment shapes — are stamped out as their own
specializations with the planar history pointers hoisted, so the channel
loop vanishes. The dots are the unchanged `tap::dsp` kernels and the
append/emit order is identical, so the outputs are bit-exact, and the
scipy-vector tests are the proof: a change that moved a bit would fail
them before the ratchet ever ran.

Measured, with the baselines re-recorded in the same PR: **Cortex-M55
−31 … −60 %** — `up_q15` from 118.8 M to 58.5 M instructions, at which
point Q15 beat float on the M55 (58.5 M against 62.2 M) for the first time,
resolving half of an anomaly the measurement harness had named a day
earlier (on the M55, with float in hardware, the fixed-point paths had
been *slower* than float, and the cause was a loop Helium could not see
into); **M33 fixed point −12 … −25 %**, float only −2.8 %, because the
M33's soft-double accumulation dominates its float path by design;
**Hexagon flat**, −0.1 … −3.8 %, because hexagon-clang had already
generated tight code around the old loop. Three targets, three different
answers to the same change: the overhead was an Arm codegen story. `pull()`
keeps the generic per-frame loop deliberately; its granularity is the pop
callback, a different lever.

## Lever 2: committed trip counts

```cpp
{{#include ../../../bridge/include/tap/sr/bridge/converter.h:rt_process}}
```

The canonical profiles are `constexpr`, so their taps-per-phase are
compile-time facts, and `process()` dispatches once per call onto four
instantiations of the walk — one per pinned count — each handing the
inlined dot kernels a *constant* trip count instead of a runtime bound. A
custom-taps profile takes the runtime-length instantiation, `T = 0`,
pinned by its own impulse test so it cannot rot while nobody uses it.

Measured: **Hexagon fixed point −7 … −12 %** on all four scenarios —
hexagon-clang software-pipelines an exact-count scalar loop and will not
pipeline a bounded one; **M55 `up_q15` −15 %**, because 44 taps (the count
at the time) fully unrolls under Helium, while the 78-, 96- and 184-tap
scenarios stay loop-shaped and flat; **M33 Q15 −2.6 / −3.4 %**; float flat
everywhere, within the noise of flat, bound by soft-double on the M33 and
by the FP64 accumulation chain on the M55. This is the codegen half of
"baked tables". The other half — the coefficients themselves committed to
rodata — is still not pulled, and the reason it was first deferred turned
out to be wrong, which is the last section of this chapter.

## Lever 3: the halving's compute cost, and what the ratchet caught

The previous chapter's symmetry halving stores half the rows and dots the
mirrored phases backwards. Its compute cost was expected to be zero, and
on all three targets the baselines moved by zero — eventually. The first
attempt inlined both dot arms, forward and reversed, in the walk's loop
body, and the M55 `up_q15` count went **+3.3 %**: two inlined dot
expansions in one loop broke Arm's unrolled codegen of the forward arm.
Outlining the mirrored arm as a call fixed Arm and broke Hexagon, whose
`down_q31` went +3.3 % called and +2.7 % inlined. So the mirrored arm's
out-lining is gated per target — `TAP_SR_BRIDGE_MIRRORED_DOT_ATTR` is a
`noinline` attribute on Arm and empty on Hexagon — the same
measured-per-target pattern as the `tap::dsp` kernel gates, and the worst
residual rides inside the ±3 % gate. Three percent on a workload would
have been invisible to a wall-clock benchmark on a shared runner; the
counter saw it, and the comment above `dot_mirrored` records the numbers
so the next person to simplify the code knows what they are about to
un-fix.

Cumulative, against the campaign's starting baselines: **M55 Q15 −59 /
−60 % and float −35 / −37 %; M33 Q31 −26 / −27 %, Q15 −15 / −16 %;
Hexagon Q15 −13 / −10 %, Q31 −15 / −7 %**; table storage halved; every
output bit identical throughout. The remaining levers in the brief —
multistage decomposition, a minimum-phase `economy`, an IIR pre-filter, an
FFT-domain path for files — all change the output contract or serve a
pressure nobody has applied, and the plan leaves them with the condition
that pulls each: a latency need pulls minimum-phase, a storage need pulls
multistage.

## The finding: the constructor was in the workload

Lever 2's deferral of coefficient baking originally said "construction is
under 0.3 % of every workload". Two months later, re-pinning DspTap to a
version whose Kaiser designer evaluates the Bessel series for half the
taps and mirrors it, every count moved — by the same amount in float, Q15
and Q31, which is the signature of construction, since the three formats
share the double-precision design. That prompted a direct measurement: a
construct-only build of every scenario. Construction was **3–5 % of the
M55 workloads, 5–37 % on Hexagon, and on the M33 42–45 % of the Q31
workloads and 55–63 % of the Q15 ones** — 67–74 % before the Bessel
change. On the M33 the fixed-point baselines were mostly pricing the
constructor, and a hot-path regression there was being diluted two- to
threefold before the ±3 % gate could see it; every per-lever M33
percentage above was measured through that dilution, so the audio-path
improvements were correspondingly larger than stated.

The finding is recorded in the plan with the two remedies it suggests —
a construct-only ratchet scenario, or measuring two workload lengths and
differencing, as the ASRC's ratchet now does — and the `rational` engine
of the next part ratchets construction as its own scenario from its first
baseline, because a new engine does not inherit a known debt. It is also
why "construction is cheap" is not a sentence that appears in this book
without a number next to it.

## Why the converter looks the way it does

| Decision | Alternative rejected | Reason |
|---|---|---|
| Trip count settled by arithmetic before the walk | per-frame exhaustion checks | the loop body has no branches the compiler cannot resolve; `outputs_for` is exact |
| State machine in locals, pointer bumps | member variables, index multiplies | M55 −31 … −60 %, M33 fixed point −12 … −25 %; Hexagon flat, which located the overhead |
| Mono and stereo as stamped-out specializations | one generic channel loop | the deployment shapes; the channel loop vanishes |
| Four `constexpr` trip counts, one dispatch per call | a runtime dot length | Hexagon pipelines exact counts (−7 … −12 %); 44 taps unrolls under Helium (−15 %) |
| Mirrored dot out of line on Arm, inline on Hexagon | one choice for all targets | two inlined arms cost +3.3 % on the M55; a call costs +3.3 % on Hexagon; gated per target with the numbers in the comment |
| `pull()` keeps the per-frame loop | walk it too | its granularity is the callback; a different lever |
| Baking deferred, with the corrected reason | commit tables now | construction is up to 63 % of the M33 Q15 workloads — baking would move the counts a lot while leaving the audio path alone |

## Verify it yourself

```sh
# The contract: scipy vectors in both directions at every tier, pull ==
# process, the dry-source resume, accounting from every position, the
# impulse reproducing the table (including the custom-taps runtime walk),
# flush, reset, two channels, the rate-scaled pairs:
ctest --test-dir build -R 'bridge\.Converter\.' --output-on-failure

# The ratchet on one target (plugin and toolchains as in bench/README):
cmake -S . -B build-m55 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
      -DTAP_SR_BUILD_TESTS=OFF -DTAP_SR_BUILD_EXAMPLES=OFF -DTAP_SR_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55 -j
python3 scripts/icount.py --engine bridge --target m55 --build-dir build-m55 --plugin libinsncount.so

# Break it on purpose: make TAP_SR_BRIDGE_MIRRORED_DOT_ATTR empty on Arm
# and re-run the m55 ratchet. Every test still passes; up_q15_eco moves
# past +3 % and the gate fails, which is the whole point of the gate.
```
