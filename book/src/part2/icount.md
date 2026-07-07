# Counting instructions, deterministically

> When you can measure what you are speaking about, and express it in numbers, you know something about it; but when you cannot measure it, when you cannot express it in numbers, your knowledge is of a meagre and unsatisfactory kind.
>
> — Lord Kelvin

The optimization campaign of Part III makes claims like "−5.3% on the M55
Q15 pipeline" and expects you to believe the decimal point. This chapter is
about the machinery that makes such a decimal point *mean* something — and
about why the obvious metric, time, had to be fired from that job first.

## Wall-clock cannot hold a gate

The project's benchmarks run in CI on shared, virtualized runners: machines
whose actual delivered performance depends on what every other tenant is
doing, what frequency the host decided on, and which physical box the job
landed on today. `docs/PERFORMANCE.md` states the resulting policy without
hedging: *wall-clock benches are never a hard gate on shared runners; they
run as a smoke test and produce trend artifacts only.*

That policy was not adopted from theory. During the C2 vectorization audit
(Part III), the README's wall-clock table was deliberately *not*
regenerated, because the shared machine was measurably in a different state
than the annotated session that produced the table — about **20% slower
across the board on unchanged code**. Sit with that number for a moment: the
optimization being evaluated in that PR was worth 3.7% on the same metric.
A gate that must detect 3% shifts through 20% ambient swings is not a gate;
it is a random number generator with a pass rate. You can fight the noise
statistically — pin runners, repeat runs, compare medians — and projects
do, but every mitigation buys precision with CI minutes and still cannot
promise that a 1% regression fails *deterministically*.

The library's answer is to gate a different quantity entirely: **executed
instructions**. Run a fixed workload under an emulator, count every guest
instruction that retires, and the result is a property of the *binary*, not
the weather — bit-identical across runs (the project verified this before
trusting it), independent of host load, and, for the scalar code these
embedded targets run, well correlated with real cost. The metrics table in
`docs/PERFORMANCE.md` is careful about that last clause, and so is the end
of this chapter; but first, the machinery.

## Forty lines of plugin

QEMU's TCG (Tiny Code Generator) translates guest instructions into host
code one *translation block* at a time, and since QEMU 4.2 it exposes a
plugin API that lets you hook that translation. The project's entire
counting instrument is `tools/qemu_insn_plugin/insn_count.c` — small enough
that its two working functions fit here:

```c
{{#include ../../../tools/qemu_insn_plugin/insn_count.c:pf_hooks}}
```

The design point that matters is `qemu_plugin_register_vcpu_insn_exec_inline`
with `QEMU_PLUGIN_INLINE_ADD_U64`. There are two ways a TCG plugin can count
executions: register a *callback* per instruction — a host function call
every time the guest retires one instruction — or register an *inline
operation*, which asks QEMU to plant a bare 64-bit add into the generated
host code itself. The callback form would multiply the emulation time of a
billion-instruction workload by a large constant; the inline form costs one
host add per guest instruction and no calls. `tb_trans` fires once per
translation block *translation* (not execution), walks the block's
instructions, and attaches an inline `+1` to each — after which counting
proceeds at essentially full emulation speed forever, because translated
blocks are cached and re-executed.

The header comment is candid about the accuracy contract this buys:
"the single counter is exact for our single-vCPU deterministic workloads."
A plain `uint64_t` incremented from generated code would be a data race on
an SMP guest; every target this ratchet gates is a single emulated core
running a single-threaded workload, so the simple counter is exact — and
the precondition is written down where the next porter will read it.

The second function is the entire output interface: an `atexit` callback
prints one line, `SRT_INSN_COUNT <n>`, through `qemu_plugin_outs()`. That
choice has a trap the driver script had to learn about:

```python
def qemu_cmd(target: str, plugin: str, binary: str) -> list[str]:
    # "-d plugin" routes qemu_plugin_outs() to stderr; without it the count
    # line is silently dropped.
    if target == "hexagon":
        return ["qemu-hexagon", "-d", "plugin", "-plugin", plugin, binary]
```

`qemu_plugin_outs` writes to QEMU's *log*, and unless `-d plugin` enables
the plugin log channel, the write goes nowhere — no error, no warning, no
line. The comment in `scripts/icount.py` preserves the discovery so nobody
re-makes it, and the script's parser treats a missing count line as a hard
failure ("plugin not loaded?") rather than a zero, so the silent-drop
failure mode cannot masquerade as a measurement.

## One binary per scenario

What gets counted matters as much as how. The counted workloads live in
`bench/icount/` — and they are *not* the Google Benchmark suite, which
auto-tunes its iteration counts to the machine's speed and would therefore
execute a different number of instructions on every run. A countable
workload must be **fixed**: same work, same iteration counts, same
everything, decided at compile time.

`bench/icount/icount_main.cpp` defines seven scenarios — `interpolate()` in
isolation and the full push/pull pipeline, each in float/Q15/Q31, plus a
12-channel Q15 pipeline for the 7.1.4 deployment shape — selected by
preprocessor definitions (`SRT_SC_KIND`, `SRT_SC_TYPE`, `SRT_SC_CH`) into
one binary each, because the bare-metal targets have no argv to select with
at runtime. Each binary runs a deterministic loop (two virtual seconds of
audio through the pipeline; 200 000 interpolations for the kernels),
accumulates a checksum, and ends with:

```cpp
    const bool ok = checksum == checksum; // NaN check
    std::printf("SRT_ICOUNT_DONE ok=%d checksum=%.17g\n", ok ? 1 : 0, checksum);
```

The three gated targets each run under the QEMU mode that matches their
deployment reality. Hexagon binaries are Linux user-space processes, so
`qemu-hexagon` (user-mode emulation) runs them directly. The two Cortex-M
targets are bare metal: `qemu-system-arm` boots each binary as a kernel on
a full board model — MPS3 AN547 for the M55, MPS2 AN505 for the M33 — with
semihosting for the printf. That fidelity matters for the metric: a
system-mode count includes the startup code, vector table dance, and
runtime the deployed firmware will actually execute, which is why the
plugin counts the whole run and the workloads are sized so the measured
loop dominates.

The checksum earns its place three times over: it defeats dead-code
elimination (a compiler that deleted the unobserved workload would produce
a spectacular "improvement"); printed to 17 significant digits, it pins
cross-run determinism — if two runs of one binary ever printed different
checksums, the instruction counts would be incomparable and something would
be deeply wrong; and the pipeline workload deliberately poisons it with a
NaN if the converter ever underruns, so a broken configuration cannot
produce a plausible count. `icount.py` refuses to record anything unless
`SRT_ICOUNT_DONE ok=1` appeared.

## The ratchet, and why it is two-sided

`scripts/icount.py` glues plugin to workloads: find every `srt_icount_*`
binary in the build directory, run each under the target's QEMU with the
plugin, and compare against the committed `bench/baselines.json` at a
tolerance of ±3%. A scenario with no recorded baseline fails. A recorded
baseline of zero fails. A regression beyond tolerance fails. And — the
clause that makes this a *ratchet* rather than a mere alarm — an
**improvement** beyond tolerance fails too:

```python
            elif delta < -args.tolerance:
                # Two-sided: a stale (too-high) baseline would let future
                # regressions hide inside the slack, so improvements must be
                # committed too.
                verdict = ("IMPROVED beyond tolerance — run icount.py --update "
                           "and commit bench/baselines.json")
                failures.append(scenario)
```

The two-sidedness was not in the original design. The first version of the
ratchet failed only on regression, which sounds like the point — until an
infrastructure audit traced the incentive structure. Suppose your PR makes
`pipeline_q15` 10% cheaper and you don't update the baseline. CI passes;
everyone is happy; the baseline is now 10% stale. The *next* PR can regress
the same scenario by 9% — undoing nearly all of your win — and CI passes
again, because measured-vs-baseline is still inside the slack. Improvements
that go unclaimed become a hiding place for regressions exactly their size.
The audit's fix (the same infrastructure-hardening pass that added the
bare-metal empty-run guard of the previous chapter) makes the gate
symmetric: if you made it faster, you must *say so*, in the same PR, by
re-recording the baseline — `icount.py --update` — and committing the diff.
The improvement becomes reviewable history, the gate snaps tight around the
new value, and there is never slack for anything to hide in.

`--update` has its own small discipline: it rewrites the target's entry to
*exactly* the measured scenarios, so a renamed or deleted workload cannot
linger in the JSON as a dead gate entry that never fails and never means
anything.

One boundary of the ratchet is drawn in a CMake naming convention. The
cross-resampler comparison workloads (`docs/COMPARISON.md` runs the same
fixed task through this library and through libsamplerate, per target) are
built as `cmp_icount_*` precisely so that `icount.py`'s `srt_icount_*` glob
never picks them up: competitor counts are *recorded* in the docs with
their date and toolchain, but not *gated*. The distinction is deliberate.
A gate on someone else's code would fail on their releases, punish this
project for their regressions, and pressure nobody who can act on it; a
gate is a promise, and you can only promise about code you maintain.

The tolerance deserves a sentence, because "±3% on a deterministic count"
sounds contradictory. Counts are bit-identical across runs *of one binary*;
the slack absorbs a different variation: innocuous recompilation effects.
Code layout, inlining decisions, and register allocation shift by fractions
of a percent when unrelated code changes; the C6 work measured its embedded
control scenarios at exactly 0.00% only because nothing in their path
changed. Three percent is wide enough that touching a comment never fails
the gate, and narrow enough that the +6–8% cost of a runtime flag in a hot
loop — a real mistake, caught by this exact gate during C6 and fixed with a
compile-time gate before merge — cannot pass it.

## Baselines are compiler-dependent, by design

An instruction count is a property of the binary, and the binary is a
product of the compiler. When the CI image's `gcc-arm-none-eabi` or
hexagon-clang package updates, every count moves a little, and the ratchet
job fails on unchanged library code. `docs/PERFORMANCE.md` is explicit that
this is **working as intended, not a flake**: the response is to re-record
the baselines in a reviewed commit whose diff *is* the record of what the
toolchain update did to the library's cost. The alternative — normalizing
counts, or pinning tolerances wide enough to ride out compiler churn —
would trade an occasional, explainable, reviewable failure for permanent
blindness to exactly the kind of shift a performance-conscious project most
wants to see.

The same philosophy shows up in how the tools themselves are provisioned.
The plugin compiles against a `qemu-plugin.h` pinned to the exact commit
QEMU 8.2.2's tag pointed at, checksum-verified on download. And the Hexagon
leg builds its own emulator: neither Debian's `qemu-hexagon` nor the one
bundled with the CodeLinaro toolchain enables TCG plugins, so CI compiles a
plugin-capable `qemu-hexagon` from the pinned QEMU source (linux-user
target only, cached thereafter). A measurement gate whose instruments are
unpinned is a gate whose meaning can change without a diff.

## What instructions do and do not predict

Time to honor the caveat. An instruction count is not a cycle count, and
the project's documentation never claims otherwise — the metrics table
says "well-correlated with real cost **for scalar code**," and
cycle-accurate numbers are explicitly delegated to vendor simulators or
hardware counters.

Where the correlation is good: in-order scalar cores running out of
tightly-coupled memory, which describes the Cortex-M33 and M55 targets
closely. Most instructions are single-cycle, there is no cache hierarchy to
miss in, and a 5% instruction reduction is a real, similar-sized cycle
reduction.

Where it bends: anything that changes the *mix* rather than the count.
The C3 fixed-point phase accumulator made the M55 float pipeline count
**+1.4%** worse — it replaced hardware-double operations with int64
sequences, more instructions of cheaper mix — and the project accepted the
regression for the cross-target win, with the reasoning in the PR rather
than hidden in an average.

Where it bends furthest is Hexagon, and the reason is architectural:
Hexagon is a VLIW machine that issues *packets* of up to four instructions
per cycle. Two versions of a loop with identical instruction counts can
differ meaningfully in cycles depending on how well their instructions pack
into packets — and conversely, removing instructions that packed for free
saves nothing. The C5 experiment (Part III) is the cautionary tale: a
hand-written `vrmpyh` wide-MAC kernel, proven bit-exact, verified by
disassembly to contain ten wide MACs where the baseline had zero, measured
**−0.31%** — 119,847,854 to 119,478,758 instructions on `pipeline_q15`. The
instruction metric faithfully reported that the change barely mattered; on
a VLIW machine it takes packet-level analysis (or silicon) to know whether
even that number survives translation to time.

The project's calibration path for the gap is hardware, and it ships in the
repository: `examples/pico2_cyccnt/` is a flashable RP2350 firmware that
runs the *same* `run_pipeline` workload as the icount scenarios — 32-frame
push/pull blocks, 997 Hz sine, 1 000 warm-up and 2 000 measured iterations
— timed per block with the Cortex-M33's DWT.CYCCNT cycle counter, printing
mean/p99/max cycles per block, cycles per frame, and the fraction of a
150 MHz core one 48 kHz stream costs. Correlating those cycle figures
against the committed M33 instruction baselines yields the
cycles-per-instruction ratio for exactly this code on exactly that silicon
— after which the deterministic, CI-friendly instruction gate can be read
in real-time units. Until that correlation is run on hardware you own, the
documentation deliberately states the M33 figures as instruction *budgets*,
not cycle claims; the truth-sweep audit that enforced that wording appears
again in the next chapter.

## The last mile: numbers that cannot go stale

A gated number that is hand-copied into a README is a number waiting to
rot. The published instruction-count table is therefore not written by
anyone: `scripts/update_icount_docs.py` regenerates it **1:1 from
`bench/baselines.json`** — every row, every comma — between
`<!-- ICOUNT:BEGIN -->` and `<!-- ICOUNT:END -->` markers, and the CI
ratchet job's final step is:

```sh
python3 scripts/update_icount_docs.py
git diff --exit-code README.md || {
  echo "::error::README icount table is stale; run scripts/update_icount_docs.py"; exit 1; }
```

Regenerate and diff. If the committed README does not match the committed
baselines exactly, the build fails — so the numbers a visitor reads are, by
construction, the numbers the gate enforces. It is the same commitment this
book makes with live-included code, applied to a table: *derived artifacts
must be derived, in CI, every time, or they are testimony rather than
evidence.*

## Verify it yourself

```sh
# Build the counting plugin (fetch qemu-plugin.h for QEMU 8.2.x first;
# ci.yml pins the exact URL and checksum):
gcc -shared -fPIC $(pkg-config --cflags glib-2.0) -I/path/to/plugin-header \
    tools/qemu_insn_plugin/insn_count.c -o /tmp/libinsncount.so

# Cross-build the fixed workloads and run the ratchet (arm-none-eabi-gcc):
cmake -B build-m55 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
      -DSRT_BUILD_TESTS=OFF -DSRT_BUILD_EXAMPLES=OFF -DSRT_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55 -j
python3 scripts/icount.py --target m55 --build-dir build-m55 \
        --plugin /tmp/libinsncount.so

# Determinism: run any one binary twice and compare the counts exactly.
qemu-system-arm -M mps3-an547 -nographic -semihosting -d plugin \
    -plugin /tmp/libinsncount.so -kernel build-m55/bench/icount/srt_icount_pipeline_q15

# See the two-sided gate work: re-run icount.py with --tolerance 0.0001
# and watch benign recompilation deltas fail in *both* directions.

# The docs-freshness gate:
python3 scripts/update_icount_docs.py && git diff --exit-code README.md
```

And the experiment that motivates the whole chapter: run any wall-clock
benchmark from `bench/` twice on a shared machine, an hour apart, and
compare. The instruction counts you just produced will not have moved by a
single instruction; the nanoseconds will tell you about the machine's day.
