# The proof, the ABI, and the levers that lost

> The first principle is that you must not fool yourself — and you are the easiest person to fool.
>
> — Richard Feynman, "Cargo Cult Science", 1974

Part II described the ASRC's proof system: deterministic simulation,
sine-fit metrology, the instruction-count ratchet. `rational` inherits
the ratchet and the measure-first culture whole, and replaces the
simulation with something a synchronous converter can have and an
asynchronous one cannot — a golden reference that is *exact*. This closing
chapter walks the three legs the plan requires before any optimization,
the C boundary the notebooks measure through, and the four levers that
were pulled after the plan was complete, two of which the numbers sent
back.

## Three independent legs

**Leg one: scipy, sample for sample.** For every single-stage ratio at
`economy`, and the by-2 pair at `transparent`, a committed reference
vector: `scipy.signal.upfirdn` in float64 over the same per-branch-
normalized Nyquist design, cast to float32, with the generator's
provenance in the header. The float stage must match from n = 0,
transient included (`golden_test.MatchesScipyEveryRatioEconomy`, 6 × 10⁻⁸
measured against a 3 × 10⁻⁵ tolerance inherited from `bridge`). Chains are
then pinned against *sequenced* upfirdn — the stages' references applied
one after another — so the chain leg is independent of `chain<>`'s
bookkeeping. The ASRC could never have this leg: there is no closed-form
reference for a servo-tracked ratio, which is why Part II needed a
two-clock simulator and a sine fit. A ratio that is a type has a reference
that is a function call.

**Leg two: exhaustive sweeps and cross-precision.** Every phase of every
stage — L phases for ↑L, M input phases for ↓M, the L schedule entries of a
mixed stage: the impulse reproduces the table bit for bit from every input
phase in every format, accounting is exact from every superblock position,
float / Q15 / Q31 against double at pinned numbers. The sweeps are
exhaustive rather than sampled because L and M are small enough to make
exhaustive cheap, and a sampled sweep of a 3-phase schedule would be a
strange economy. DspTap's `decimate.h` serves as a second golden at ↓2,
↓3 and ↓6: a different design on the same substrate, so a disagreement
larger than the two designs' documented difference is a finding about the
substrate.

**Leg three: the coverage matrix**, the previous chapter's 182 rows with
their tone battery. It is the leg that answers the question a user
actually asks — *does 192 → 22.05 at `economy` meet 70 dB with an 8.269 kHz
passband?* — rather than a question about a stage.

Plus the family's own tests, run per engine under the `rational` label:
the link interface is exactly `tap::dsp`, every public header compiles in
isolation, no header includes a sibling, and the header count is pinned at
six. That last one is a drift alarm: a seventh header is a plan change.

## The embedded legs and the ratchet

The battery runs on three emulated targets on every push — Cortex-M33 and
M55 under `qemu-system-arm` with a one-shot `bare_metal_main.cpp` whose
baked filter excludes the measurement suites (the 16,384-point sweeps, the
unpinned searches, the matrix), and Hexagon under `qemu-hexagon`. The
fixed-point chapter showed what the M33 leg is for: a second libm in CI.

The instruction-count ratchet is Part II's, with sixteen fixed workloads:
the by-2 and by-3 stages both ways in float and Q15 at `economy`, the Q15
by-4 chain and, since the 4th-band stage shipped, the Q15 by-8 chain
(↓4·↓2), the by-2 pair in float at `transparent` and, since the matrix
chose a chain per tier, the 1/16 chain it runs there (↓8·↓2), the 2/3
stage in both formats, and construction alone — two seconds of stereo in
32-frame blocks, counted whole, construction included, gated two-sided at ±3 %
against `rational/bench/baselines.json` on all three targets. Construction
is a ratcheted scenario from the first baseline because the ASRC's plan
carries a debt for not having done that, and a new engine does not inherit
debts. The guest marker `RATIONAL_ICOUNT_DONE ok=1 checksum=…` is part of
the counted binary and never changes; the checksum defeats dead-code
elimination and pins cross-run determinism in the same line.

## The C boundary

Part IV's C ABI chapter made the case for a shim: the notebooks must
measure the shipping C++, and Python can only call C. `rational`'s shim
follows `bridge`'s shape with two decisions of its own.

The first is the family rule at the C boundary. `tap_sr_rational_create`
takes a *chain constant* — one of the 33 named within-family chains, the
eight one-stage ratios and the twenty-five multi-stage names from the
previous chapter — a profile, and a channel count. The values are stable:
the ten chains the 4th-band stage displaced in 0.8.0 keep their values,
retired and refused, so the live ones run from 0 to 42 with gaps and
`TAP_SR_RATIONAL_CHAIN_END`, not a count, bounds them. There is no function that takes
two rates. The one escape hatch, `tap_sr_rational_create_stage(L, M,
profile, divisor_num, divisor_den, channels)`, builds a single stage of the
vocabulary at a stated design divisor: the piece a cross-family chain
through `bridge` is composed from, by a caller who writes the chain — which
is exactly how the matrix notebook builds the cross rows, through
`bridge`'s own C ABI. The channel count stays a run-time argument at the C
boundary, where in C++ it is the type's (A13): one and two channels run the
native types, and any other count that many one-channel converters behind
the handle, bit for bit the native type of that count
(`CApi.FanOutIsTheNativeConverterBitForBit`).

The second was a departure from the siblings when it landed, and the plan
flagged it as one. `async`'s and `bridge`'s ABIs were float-only, on the
argument Part IV gave — the notebooks are metrology instruments, float is
what they measure with, and every function in an ABI is a promise kept
forever. `rational`'s ABI also carries Q15 and Q31, through
`tap_sr_rational_create_format` and `_process_q15` / `_q31`, because the
fixed-point profiles exist for a reason that is not metrology: the
Bluetooth-adjacent M33 / M55 deployments that are the Q15 profile's whole
purpose are FFI consumers too. A call in another format than the
converter's returns 0 and touches nothing; every format is pinned bit for
bit against the C++ `basic_chain` of that sample type. Twenty-three
exported symbols, twenty-seven since 0.9.0, the library built with hidden
visibility so that no C++ leaks into the table, and the symbol set is a test
(`rational.Family.ExportedSymbolsArePinned`, `nm` against a committed
list, on every ELF and Mach-O host). The departure did
not last: faced with one engine in this shape and two in the other, the
family chose the one, and the siblings adopted it — `create_format`,
`format`, the `_q15` / `_q31` entry points, hidden visibility, the same
three format values — so the three ABIs now differ only in what each
engine names.

The recursive stages and the guard band filter joined in 0.9.0 the same
way, beside the entry points that were there, so no existing symbol
changed. `tap_sr_rational_create_iir_format(direction, factor, phase,
profile, format, channels)` names a recursive half-band by 2 or its cascade
by 4, 8 or 16, and `tap_sr_rational_create_guard_band_format(form,
half_band_kind, profile, format, channels)` the filter at a named setting:
named settings only, as the chains are named. Every generic call works on
them, and the one that cannot says so. A recursive stage has no exact
latency in C++, so `tap_sr_rational_latency_output_frames` writes 0 / 0
for it rather than a number it does not have; two double getters, the
nominal latency and the group delay at a frequency, answer for every
handle. The near-linear variant is refused too, although its delay path is
exact: its passband's delay strays from that path by a hundredth of a
frame, so the path is its nominal, not its latency. The linear guard band
form, whose latency is exact, reports it. Two notebooks measure them through
this ABI: `iir_comparison.ipynb`, the FIR half-band against both recursive
variants with and without each form at every profile, and
`guard_band_demo.ipynb`, the previous chapter's demonstration.

The notebook, `rational/notebooks/matrix.ipynb`, is committed executed:
all 182 rows at four profiles through the C ABIs, reproducing every one
of the 728 MAC-per-output and latency pins exactly, and running the tone
battery to the same worst numbers the C++ test reports. Where Part II's
notebooks drew the money plot, this one draws a table, because a
synchronous converter's claim is a table.

## Four levers, measured

The plan's milestones ended at M6. After them, one lever per change,
each measured against the ratchet and the pins, as `bridge`'s campaign
was run:

| Lever | Outcome | The number that decided it |
|---|---|---|
| Helium Q15 dot (DspTap) | **shipped** | M55 Q15 workloads −21 … −48 %, every output bit and every other count unchanged |
| Q15 decimators quantized per branch (DspTap `finalize_divided`) | **shipped** | ↓6 / ↓8 at `economy` −64.7 / −62.9 → −71.5 / −71.7 dB; the table becomes the interpolator's |
| Sparse rows for 2/3, 3/4, 3/8 | **declined** | float −22 / +13 / −25 % on M33 / M55 / Hexagon; Q15 +22 / +60 / +24 % |
| Symmetry-halved tables | **declined unbuilt** | saves memory only; the largest table is 1.6 KB in float; the reversed Q15 dot on the M55 reads through a gather |

Then the Q15 mixed-down gain, which was not a lever but a limit lifted
(every Q15 stage at the 70 dB tier), and the nyquist.h mirror, which was
not planned at all.

Two of the four were declined, and both declines are recorded with the
numbers that made them. The sparse-row lever is the more instructive: it
did precisely what it promised — a third fewer MACs, bit-identical output
— and lost, because MACs are a model of cost and the ratchet measures
cost. The ASRC's Hexagon chapter told the same story about a
vectorization that measured 0.31 % and was deleted. The repository keeps
such results on purpose: a lever that is declined with its numbers cannot
be re-proposed without new numbers, and the plan's section 6 is where the
next reader finds them before spending the week.

## What this part was about

Strip away the DSP and the shape of `rational` is this: a converter whose
every degree of freedom is a compile-time constant, built on a filter
structure that gives away half its taps for free, composed by a helper
that knows nothing about the engines composing it, and measured by a
reference that is a function call. The asynchronous converter of Parts
0–V had to earn each of its numbers against a moving clock; this engine
gets to pin them with `==`. Which is a good place to end a book that began
by asking you to believe a decimal point: here, finally, are the ones that
are exact.

## Verify it yourself

```sh
# The whole engine, every leg that runs on a host:
ctest --test-dir build --output-on-failure -L '^rational$'

# The family's dependency rule for this engine:
ctest --test-dir build -R 'rational\.Family\.' --output-on-failure

# The C ABI, pinned against basic_chain in every format, and the symbol count:
cmake -S rational/capi -B build_capi && cmake --build build_capi -j
ctest --test-dir build -R 'rational\.CApi\.' --output-on-failure
ctest --test-dir build -R 'rational\.Family\.ExportedSymbolsArePinned' --output-on-failure
nm -D --defined-only build_capi/libtap_sr_rational_capi.so | grep -c ' T tap_sr_rational_'   # 27, by hand

# The ratchet on one target (the plugin and toolchain as in bench/README):
cmake -S . -B build-m55 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
      -DTAP_SR_BUILD_TESTS=OFF -DTAP_SR_BUILD_EXAMPLES=OFF -DTAP_SR_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55 -j
python3 scripts/icount.py --engine rational --target m55 --build-dir build-m55 --plugin libinsncount.so

# The matrix through the C ABIs, re-executed, and the recursive stages' notebooks:
jupyter nbconvert --to notebook --execute --inplace rational/notebooks/matrix.ipynb
jupyter nbconvert --to notebook --execute --inplace rational/notebooks/iir_comparison.ipynb
jupyter nbconvert --to notebook --execute --inplace rational/notebooks/guard_band_demo.ipynb
```
