# Hexagon: a DSP that keeps secrets

> Trust, but verify.
>
> — Russian proverb

Every portability chapter in this part answers the same question: what did
the target force the library to learn that no amount of host testing could
have taught it? Hexagon — Qualcomm's DSP architecture, the kind of core
that audio actually ships on inside a phone — answered it four times, and
three of the four answers contradicted a reasonable engineer's prior. This
chapter walks through the port itself (which is small) and then the four
lessons (which are the point), in the order the project learned them.

First, the ground rules the target sets. Hexagon here is
`hexagon-unknown-linux-musl`: a 32-bit `size_t` (the ring chapter's
wraparound proof stops being theoretical), musl instead of glibc, clang
instead of GCC, and — the fact that ends up organizing half of Part III —
**no double-precision FPU**. Every `double` the library touches on this
target is a call into soft-float routines. The library's float datapath
accumulates in `double` deliberately (that decision is defended in the
polyphase chapter); on Hexagon that choice has a price tag, and this
chapter contains the receipts.

## The whole port is one file

Here is everything SampleRateTap needed to run its test suite on a
Qualcomm DSP:

```cmake
{{#include ../../../cmake/hexagon-linux-musl.cmake}}
```

Thirty lines, and most of them are comments. Two decisions carry the file.

**`CMAKE_CROSSCOMPILING_EMULATOR qemu-hexagon`.** This single line is what
makes the port *routine* instead of a parallel test infrastructure. CMake
prepends the emulator to every test command it registers, so `ctest` runs
each cross-compiled binary under `qemu-hexagon` user-mode emulation without
knowing it is doing anything unusual. It goes further than the obvious
case: `gtest_discover_tests()` needs to *execute* the test binary at build
time to enumerate its tests, and the emulator prefix makes discovery work
too — which is why `tests/CMakeLists.txt` raises `DISCOVERY_TIMEOUT` to
120 seconds and the per-test timeout to 900. Instruction-set emulation is
slow, roughly an order of magnitude or two; the timeouts are the only
place the build system admits it.

The same pattern is deliberately generic. The commented-out HiFi4/HiFi5
job template in `.github/workflows/ci.yml` is this toolchain file with the
names changed (`xt-clang++`, `xt-run`): any target with a cross-compiler
and an instruction-set emulator drops into the same shape, and the test
suite — the project's real asset — transfers unmodified.

**`-static`.** A dynamically linked musl binary needs the emulator to be
told where the target's loader and shared libraries live (`qemu-hexagon
-L /path/to/sysroot`), and that path would have to thread through CMake,
CTest, CI, and every developer's shell. Static linking deletes the whole
problem: the binary is self-contained, the emulator invocation is just
`qemu-hexagon ./srt_tests`, and nothing about the sysroot can drift out of
sync. For a test rig this is the right trade without much argument — the
binaries are throwaway artifacts, nobody cares that they are megabytes
instead of kilobytes. Keep this decision in mind, though. It comes back at
the end of the chapter with teeth.

The CI leg (`hexagon-qemu` in `ci.yml`) runs the suite with an exclusion
list: the multi-minute quality and lock simulations, the 10-million-element
thread stress, and a few others. The reasoning is stated in the workflow
and worth internalizing: those tests prove target-independent control
mathematics and host concurrency, which emulation neither speeds up nor
measures meaningfully. What stays *in* is exactly what the target can
falsify — kernel accuracy, fixed-point arithmetic, 32-bit `size_t`
behavior, atomics lowering, musl's corners. An emulated test leg should
run only the tests that target can fail.

One boundary must be drawn before any number in this chapter is quoted,
and the toolchain file draws it in its own header comment: user-mode
emulation validates ISA-level *correctness*, never performance. QEMU
translates guest instructions to host instructions and runs them as fast
as it can; nothing about its timing resembles the DSP's. What emulation
*can* produce deterministically is the count of guest instructions
executed — the metric Part II's ratchet chapter is built on — and that
count is a good proxy for scalar-code cost while remaining a proxy.
Cycle-accurate Hexagon numbers require the proprietary Hexagon SDK
simulator, which this project does not have; the documentation says so
rather than letting instruction counts impersonate cycles. Every Hexagon
figure below is therefore an instruction count, exact to the instruction,
reproducible on your machine, and honest about what it is not.

## Lesson one: the genuinely FP64-less target

The first thing Hexagon did was refuse to be impressed by an optimization
that worked everywhere else.

C1 — the blended-row precompute, Part III's opening win — cut the M55
pipeline instruction counts by 15–30% and host stereo wall-clock by 36%.
The same change on Hexagon: **−3.6% float, −3.3% Q15, −0.2% Q31**. Not
wrong, not a regression — just strangely small, and "strangely small" is
the most informative result an instruction counter can produce. If a
change that halves the inner-loop arithmetic barely moves the total, the
total is not made of inner-loop arithmetic. The diagnosis, recorded in
the C1 entry of `docs/PERFORMANCE.md`: Hexagon's pipelines were dominated
by the per-sample phase bookkeeping, done in `double` and therefore
soft-floated — every phase increment, wrap and blend-factor conversion
expanding into library-call arithmetic that dwarfed the MACs the
optimization had so carefully thinned.

That diagnosis did two things. It motivated C3, the Q0.64 integer phase
accumulator, whose design you have already seen in Part III. And it
forced a correction that is preserved in `docs/PERFORMANCE.md`'s
hypothesis list: the project had been assuming the Cortex-M55 was also in
this soft-double class, and it is not — the M55's *scalar* FPU executes
FP64 in hardware (only the MVE vector unit is fp16/fp32). The M55's
float numbers had never been soft-double-bound. **Hexagon is the
genuinely FP64-less target**, the only one in CI where "the phase math is
done in doubles" translates to "the phase math is done in subroutines."

Which is why C3's Hexagon column is the loudest in the whole optimization
campaign. Eliminating soft-double phase math from the per-sample path
bought, from the PR's gating run:

| Scenario | Hexagon instructions |
|---|---:|
| `pipeline_q31` | **−15.5%** |
| `pipeline_q15` | **−10.3%** |
| `pipeline_float` | −2.6% |
| kernels | count-identical (control) |

The kernels-identical row deserves its footnote: the change touched only
the converter's per-sample phase path, so the isolated-kernel workloads
*must* not move. They didn't, to the instruction. That is what a control
group looks like in this project's methodology, and the deterministic
QEMU counts are what make a control group meaningful at all — a
wall-clock benchmark can certify "similar," never "identical."

## Lesson two: hexagon-clang wants aliasing proven, not promised

C2, the vectorization audit, restrict-qualified the kernel hot-loop
pointers after `-fopt-info-vec` showed GCC vectorizing the blend loop
only behind a runtime aliasing check ("loop versioned for
vectorization"). On the M55 the payoff was real but narrow:
`pipeline_float` −1.35%, every other scenario exactly 0.00%.

The same one-line annotation on Hexagon, from the PR's gating run:

| Scenario | arm-gcc (M55) | hexagon-clang |
|---|---:|---:|
| `pipeline_float` | −1.35% | −1.6% |
| `pipeline_q15` | 0.00% | **−6.2%** |
| `pipeline_q31` | 0.00% | **−12.3%** |
| kernels | 0.00% | 0.00% (control) |

Same source, same semantics, wildly different sensitivity. The commit
that pinned the new Hexagon baselines states the finding plainly:
*hexagon-clang benefits from provable no-aliasing far more than arm-gcc
did* — once aliasing is provable it schedules the dot loops
substantially better. That is consistent with what Hexagon is: a VLIW
machine whose compiler packs multiple operations per issue packet and
therefore lives or dies by how freely it may reorder memory operations.
A `restrict` that merely deletes one runtime check on an in-order ARM
core instead unlocks the scheduler on a DSP.

The portable lesson is about division of labor: `SRT_RESTRICT` was added
for a measured GCC reason, and the *same annotation* paid a much larger,
unlooked-for dividend on the DSP compiler. Aliasing facts belong in the
source, stated once, precisely — because you cannot predict which
backend will be able to spend them.

## Lesson three: the ISA already had the trick (C5)

By C5 the project had a pattern that worked: the C4 packed dual-MAC Q15
kernel had just bought −3.1% on the Cortex-M33 with a small block of
intrinsics. Hexagon has a directly analogous instruction, `vrmpyh` —
four exact 16×16 products summed into a 64-bit accumulator per
instruction, C4's argument at twice the width. The hypothesis practically
wrote itself.

It was implemented properly: a `vrmpyh` intrinsic loop for the Q15 dot
product, bit-exact against the portable path, full suite green on Hexagon
QEMU. Then it was measured, and the ratchet reported:

> `pipeline_q15`: 119,847,854 → 119,478,758. **−0.31%.**

A result that small demands an explanation before it demands a decision,
because there are two very different ways to earn −0.31%: either the
compiler was already emitting wide MACs (making the intrinsics
redundant), or the wide MACs genuinely don't matter here. The two imply
opposite things about future work, so the project pulled disassembly from
CI (`llvm-objdump`, pre and post): the baseline binary contains **zero**
wide-MAC instructions; the intrinsic build contains **10**. The compiler
had not already done it. The instructions landed, executed — and saved
almost nothing.

The explanation is in the scalar ISA. Hexagon already issues
single-instruction 64-bit multiply-accumulates (`Rxx += mpy`) and 64-bit
loads, so the portable C++ loop was already running close to one MAC per
instruction, with none of the per-element overheads the M33's baseline
loop had been paying. And what a 4-wide reduce could still have saved,
the fix-up work ate: the history window is 2-byte aligned by nature (it
is a stream of Q15 samples), so feeding `vrmpyh` requires combine and
alignment work that costs nearly what the wider multiply saves. C4 won on
the M33 because there was fat to cut; Hexagon's baseline had none.

You can see the same fact from the committed baselines, without any
intrinsics experiment at all. The README's instruction-count table has
`kernel_q15` at 102,819,852 on Hexagon against 181,994,196 on the
Cortex-M55 — the scalar DSP executes *fewer* instructions than the core
whose Q15 loops GCC vectorizes with Helium. Cross-ISA instruction counts
must be read with care (an instruction is not a unit of work, and fewer
instructions is not the same claim as faster), but as a measure of *MAC
density* the comparison is legitimate: Hexagon's ISA packs so much of
this workload into each instruction that there was structurally little
for a wider multiply to remove. C5's failure was, in hindsight, already
sitting in the baseline table. The experiment's value was turning "in
hindsight" into a checked fact with disassembly attached.

So the code was deleted. Not shelved, not flag-gated: reverted, per the
stop rule in `docs/PERFORMANCE.md` — per-architecture complexity must
justify itself, and −0.31% does not justify a permanent intrinsic code
path that every future refactor must keep bit-exact. The C5 entry in
`docs/PERFORMANCE.md` *is* the deliverable: the numbers, the disassembly
evidence, and the reasoning, recorded so that nobody re-derives this dead
end in two years when the file looks temptingly scalar again.

The entry also pre-empts the obvious follow-up — "fine, scalar `vrmpyh`
is redundant, but what about HVX, the 128-byte vector unit?" — with
arithmetic instead of enthusiasm. A 48–80-tap dot product doesn't fill
one HVX vector; worse, HVX 16-bit MACs accumulate in 32-bit lanes, and
the library's exact-int64 accumulation invariant overflows 32 bits after
about 24 worst-case taps. Per-channel tap-axis dots are simply the wrong
*shape* for HVX. The shape that fits — one 64-bit lane pair per channel,
16 channels filling a vector exactly — is the channel-parallel form, and
that observation, recorded as the successor hypothesis, became C6.

Negative results are worth exactly what you write down about them.

## Lesson four: the exception secret

For months the Hexagon leg was the quiet one. Then a hardening PR added
the library's first `EXPECT_THROW` tests — constructor validation,
`config::validated()` throwing on nonsense configurations — and the
Hexagon leg turned red in a way no other platform did. The constructor
throws correctly. The `EXPECT_THROW` machinery is standing by to catch.
And the exception never arrives: **this static-musl toolchain
configuration cannot unwind the stack.** The throw reaches the runtime,
the unwinder that should walk the frames is not part of the link, and
`libc++abi` does the only honest thing left — terminate. Every other
platform passed; main was red on exactly one leg, because that leg was
the first place a C++ exception had ever actually been *thrown* in this
project's CI history.

Remember `-static`, the convenience decision from the top of the chapter?
This is its bill arriving. The configuration had silently shipped without
a working unwind path, and nothing in months of green CI could have said
so, because exception propagation is invisible until the first frame
needs unwinding. A capability you never exercise is a capability you do
not have — you merely have no evidence yet.

The response is a case study in how this project metabolizes a
limitation, three moves in one commit:

1. **Quarantine precisely.** `ConfigValidation` is excluded from the
   Hexagon `ctest` invocation — that suite and nothing else, with a
   comment in `ci.yml` explaining why. Validation logic is
   target-independent and still covered on every other leg; what Hexagon
   cannot test is the *unwinding*, not the *validating*.
2. **Record it where deployers look.** The Known-debt ledger in
   `docs/PERFORMANCE.md` gets an entry with the deployment rule stated as
   a rule: on this toolchain configuration, an invalid `config` is
   **fatal** — validate inputs *before* constructing, because the
   constructor's throw will take the process down rather than propagate.
   The toolchain file itself carries the same caveat, so the next person
   to cross-compile inherits the warning at the point of use.
3. **Name the candidate fix without pretending it is done.** Linking an
   unwinder (`-unwindlib=libunwind`) in the toolchain file would likely
   restore propagation; it stays a recorded candidate until someone
   verifies it, because "probably fixable" and "fixed" are different
   ledger states.

The library's API already leaned the right way — `validated()` exists
precisely so callers can validate before constructing — so the rule
costs a deployer one line. But the general finding stands, and it is the
chapter's title: a target can keep a secret like this indefinitely, and
the only way to surface it is to route every kind of behavior through the
target. The first `EXPECT_THROW` to reach the leg was, in effect, the
first test of a claim the toolchain had been silently making all along.

## The CI craft: trusting your emulator and your compiler

Two pieces of infrastructure make the Hexagon numbers in this book
reproducible rather than anecdotal, and both are about supply chain more
than about DSP.

**The emulator is built from source, on purpose.** The instruction-count
ratchet needs a `qemu-hexagon` with TCG plugin support — the counting
plugin is how "executed instructions" becomes a number at all. Neither
Debian's `qemu-user` package nor the qemu bundled with the Hexagon
toolchain enables plugins. So the `icount-ratchet` job compiles its own:
the pinned QEMU 8.2.2 source tarball, verified against a hard-pinned
SHA256, configured minimally —

```sh
./configure --target-list=hexagon-linux-user --enable-plugins \
  --disable-docs --disable-tools --disable-system
```

— about four minutes to build the one binary needed, cached thereafter.
The job then *probes* the result (`qemu-hexagon -plugin help`, judged by
the error text because qemu exits nonzero either way when given no guest
binary) rather than assuming the cache returned what was put in. The
plugin header is pinned to the commit the v8.2.2 tag pointed at, by
commit SHA — tags are movable; commits are not.

**The toolchain is verified twice, against two different threats.** The
cross-compiler is the prebuilt open-source release from
`quic/toolchain_for_hexagon` (clang 19.1.5, hosted on CodeLinaro). On
download, CI checks it against the *published* `SHA256SUMS` file — which
catches corruption and cache poisoning — and against a *hard pin* baked
into the workflow, which is the only check that catches an origin
compromise, since an attacker who can replace the tarball can replace the
SUMS file beside it. The cache key is derived from the pinned digest
itself, so no job that has not verified the pin can ever write the cache
entry a trusting job will read. That last detail was not free: an audit
found two other jobs sharing the trusted cache key while downloading
without verification — a classic poisoning window — and the fix (verify
everywhere, key on the digest) is part of the same hardening commit that
gave the Cortex-M targets their stack-limit register in the next chapter.

None of this is DSP knowledge. All of it is what "the Hexagon numbers are
CI-gated" has to mean if the phrase is to carry weight: the compiler
whose output is being counted and the emulator doing the counting are
both pinned, verified artifacts, not whatever the package manager felt
like resolving that morning.

## What the port did not require

It is worth pausing on the dog that didn't bark. Running a modern C++20
template library on a Qualcomm DSP required: one 30-line toolchain file,
a test-filter list, and zero changes to library code. No `#ifdef
__hexagon__` exists in any header. The 32-bit `size_t` was already
handled by the ring's wraparound arithmetic (proved, then tested, in the
ring chapter); the absence of threads never came up because the library
never spawns one; the atomics lowered correctly because the ring asserts
`is_always_lock_free` at compile time and would have refused to build
otherwise. The port was boring precisely to the degree that the library's
portability claims were already true — and interesting precisely where
the *toolchain*, not the library, had been making claims nobody had
tested. Both halves of that sentence are the reason to port early: the
boring half is regression-proofed for free from then on, and the
interesting half you want to hear about from CI, not from a customer.

## Verify it yourself

```sh
# The port, end to end (hexagon-unknown-linux-musl-clang++ and qemu-hexagon
# on PATH; .github/workflows/ci.yml "hexagon-qemu" has the toolchain URLs):
cmake -B build-hex -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/hexagon-linux-musl.cmake \
      -DSRT_BUILD_EXAMPLES=OFF
cmake --build build-hex -j
ctest --test-dir build-hex --output-on-failure \
      -E 'AsrcQuality|AsrcLock|TwoThreadStress|TransparentPrototypeMeetsSpec|MultiChannel\.|Feasibility|Reset\.|ConfigValidation'

# The exception secret, demonstrated: remove ConfigValidation from the -E
# list above and watch libc++abi terminate instead of EXPECT_THROW passing.

# The instruction counts (needs the plugin-enabled qemu-hexagon; the
# icount-ratchet job in ci.yml shows the 4-minute from-source build):
cmake -B build-hex-ic -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/hexagon-linux-musl.cmake \
      -DSRT_BUILD_TESTS=OFF -DSRT_BUILD_EXAMPLES=OFF -DSRT_BUILD_ICOUNT_BENCH=ON
cmake --build build-hex-ic -j
python3 scripts/icount.py --target hexagon --build-dir build-hex-ic \
        --plugin /path/to/libinsncount.so

# The C5 negative result's disassembly evidence, reproduced on today's
# binary (the count should be zero — the intrinsics were reverted):
llvm-objdump -d build-hex-ic/bench/icount/srt_icount_pipeline_q15 | grep -c vrmpy
```

The last command is this chapter's thesis in one line. The claim "the
wide-MAC intrinsics were deliberately not kept" is not a story in a
design document; it is a property of the shipped binary that you can
count, and the C5 entry in `docs/PERFORMANCE.md` is the record of why
counting it settled the question.
