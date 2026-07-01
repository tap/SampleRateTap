# Cortex-M: bare metal, two ways

The Hexagon port ran the library on a strange ISA under a familiar OS.
The Cortex-M ports remove the OS. No loader, no threads, no filesystem,
no `argv`, no reliable way to even return an exit code — and the library
must still build, run its test suite, and hold its instruction budgets,
because MCU-class parts are where a $5 deployment actually lives.

The project runs two of them, and the pairing is deliberate. Each board
exists to prove something the other cannot:

- **Cortex-M55**, on QEMU's MPS3 AN547 board model. The M55 has Helium
  (MVE, the M-profile vector extension) and a full scalar FPU. It proves
  the library survives *bare metal itself* — the startup, the memory
  map, the missing runtime — and it turned out to be hiding the single
  most surprising compiler discovery in the project's history.
- **Cortex-M33**, on QEMU's MPS2+ AN505 board model. The M33 is the
  Raspberry Pi Pico 2 / RP2350 class of core: single-precision FPU only,
  no Helium, DSP extension present. It proves what deployment on a cheap
  part actually costs, in numbers concrete enough to be budgets.

Both share one startup file and one CTest strategy; they differ in linker
script and in what their instruction counts taught the project. This
chapter covers the shared anatomy first, then the two boards' discoveries.

## What `-nostartfiles` obligates you to

The toolchain files (`cmake/arm-cortex-m55-mps3.cmake`,
`cmake/arm-cortex-m33-mps2.cmake`) link with `--specs=rdimon.specs
-nostartfiles`: newlib with semihosted I/O, and *no* toolchain crt0. From
that moment the project owes the CPU everything crt0 used to provide, and
the debt is paid in one C file, `platform/armv8m_startup.c`, shared by
both targets.

It starts where the core starts — the vector table:

```c
{{#include ../../../platform/armv8m_startup.c:pt_vectors}}
```

An Armv8-M core fetches its initial stack pointer from word 0 and its
reset address from word 1; the linker scripts pin this array at the
address the core will look (`KEEP(*(.vectors))`, first section — ITCM
address 0 on the AN547, the secure-alias base on the AN505, where VTOR
points at reset). The `used` attribute stops the compiler discarding an
array nothing references; `KEEP` stops `--gc-sections` doing the same at
link time. Belt and suspenders, because the failure mode — a garbage
vector table — doesn't diagnose itself; the core simply jumps into
nothing.

There is a subtlety in how this file reaches the link, and it is the kind
of decision this book exists to record. The toolchain files pass the
startup source *on the linker command line*, from
`cmake/arm-cortex-m55-mps3.cmake`:

```cmake
{{#include ../../../cmake/arm-cortex-m55-mps3.cmake:pt_linkline}}
```

The `g++` driver would otherwise compile a `.c` link input as C++, and C++
is allowed to lower those `(uintptr_t)&Reset_Handler` initializers to
*dynamic* initialization — code that runs at startup, initializing the
table that decides where startup begins. C guarantees address-constant
initializers are link-time constants. The table must be constant for the
same reason a ladder's bottom rung must not be attached to the top of the
ladder. (The `extern "C"` guards keep the file well-defined if someone
ever does compile it as C++; the `-x c` makes sure nobody has to find out
the hard way.)

### Reset, in the only order that works

```c
{{#include ../../../platform/armv8m_startup.c:pt_reset}}
```

Four moves, each ordered by a hazard:

**MSPLIM first.** Armv8-M Mainline gives the main stack a hardware floor:
write an address to `MSPLIM` and any stack-pointer excursion below it
faults immediately, instead of the stack silently growing down into
whatever data lives below it. Why does this matter enough to be
instruction one? Because the alternative failure is the worst kind:
a deep call chain during one test overwrites the heap's top, the
corruption surfaces ten allocations later in an unrelated structure, and
the emulated target has no debugger attached and no memory protection
unit configured. A stack limit register converts that archaeology into a
HardFault at the exact instruction that crossed the line — and the
startup file gives HardFault its own handler (a `bkpt` and a park loop,
distinct from `Default_Handler`) precisely so the fault is identifiable.
This wasn't in the first version of the file; it was added by the same
infrastructure audit that hardened the Hexagon toolchain cache, and it
cost two linker-script symbols and one instruction. Insurance is rarely
priced this low.

**FPU enable before any FP instruction, with `DSB; ISB`.** At reset,
coprocessors CP10/CP11 — the scalar FPU and MVE — are disabled; the
first FP instruction would fault. The CPACR write grants access, and the
barrier pair is not decoration: `DSB` forces the write to complete, `ISB`
flushes instructions already fetched under the old permissions. Omit the
barriers and the enable *usually* works — until an instruction prefetched
before the write faults on a real pipeline. The startup does this before
touching newlib because newlib code may legitimately use FP registers.

**Zero `.bss`, but do not copy `.data`.** C guarantees zero-initialized
statics; nobody has provided that guarantee yet, so `memset` over the
linker-defined `__bss_start__..__bss_end__` does. The conspicuous absence
is the traditional `.data` copy loop — see the linker scripts below,
because that absence is a documented dependency on QEMU, not an
oversight.

**Then the runtime, in dependency order:** semihosting file handles
(`initialise_monitor_handles`) so `printf` works, `__libc_init_array` so
C++ static constructors run, then `exit(main(0, NULL))` — `exit`, not a
bare return, so `atexit` handlers and stream flushes happen before the
semihosting exit call. `main` receives no arguments. There is no one to
pass any; that fact shapes the whole test harness below.

### The runtime pieces the toolchain didn't bring

Two more gaps get filled in the same file. First, the heap.
`librdimon`'s weak `_sbrk` sizes the heap by asking the host, via the
semihosting `SYS_HEAPINFO` call, where the heap should live — an answer
that depends on the emulator's mood for a given board model. The startup
overrides it with the boring, deterministic version:

```c
{{#include ../../../platform/armv8m_startup.c:pt_sbrk}}
```

The heap is exactly the region the linker script says, ends exactly where
the script says, and `malloc` fails with `ENOMEM` — a *testable*
condition — rather than wandering into memory the map never granted.

Second, 64-bit atomics. The library's telemetry counters are
`std::atomic<uint64_t>`; M-profile has no 64-bit exclusive-access
instructions, GCC lowers those operations to `__atomic_*_8` library
calls, and the bare-metal toolchain ships no libatomic. The startup
provides the four helpers the link actually needs, built on the classic
single-core primitive — mask interrupts, do the plain 64-bit access,
restore:

```c
{{#include ../../../platform/armv8m_startup.c:pt_irqlock}}
```

```c
{{#include ../../../platform/armv8m_startup.c:pt_atomic_rmw}}
```

Why is PRIMASK sufficient where a mutex or an exclusive-access loop would
be required elsewhere? Because on a single-core part, the only agent that
can interleave with a sequence of instructions is an interrupt handler on
the same core — there is no second observer, no other cache, no store
buffer visible from elsewhere. `cpsid i` makes the critical section
literally uninterruptible, so the load-modify-store is atomic with
respect to everything that exists on the machine. The reasoning is sound
*only* single-core, which is why the dual-core RP2350 firmware at the end
of this chapter pointedly refuses to rely on it, and shares nothing
across cores except 32-bit atomics. Note also what the file does *not*
do: it implements only the helpers currently linked, and deliberately
omits the rest (compare-exchange and friends), so any future need
surfaces as a link error instead of as a silently wrong fallback.

## Two linker scripts, two philosophies of stack

The memory maps mirror each board model. The AN547:

```ld
{{#include ../../../platform/mps3_an547/mps3_an547.ld:pt_memory}}
```

Four regions, four jobs: vectors in ITCM (address 0, where VTOR resets),
code in SRAM, **the stack owning all of DTCM**, data/bss/heap in ISRAM.
Giving the stack a private 512 KB region is a luxury the board offers and
the script accepts gratefully — the stack limit is simply the region's
base, and stack and heap physically cannot collide because they do not
share a region.

The AN505 has only the two big SRAMs, so stack and heap must cohabit,
and the script makes the boundary explicit rather than hopeful:

```ld
{{#include ../../../platform/mps2_an505/mps2_an505.ld:pt_heap_stack}}
```

The stack descends from the top of DATA; the heap is *capped* 64 KB below
the top; `__stack_limit` is set exactly at the cap. Between `_sbrk`
refusing to grow past `__heap_end__` and MSPLIM faulting below
`__stack_limit`, the classic bare-metal failure — stack and heap growing
silently into each other — is fenced from both sides. One side returns
`ENOMEM`; the other side HardFaults. Neither corrupts.

And the honesty clause, stated in both scripts' headers: **QEMU's
`-kernel` loader places the ELF directly into RAM, so VMA == LMA and
`.data` needs no load-time copy.** On real silicon booting from flash,
initialized data must be linked with a load address in flash and copied
to RAM by the startup — the loop this startup deliberately does not
have. The scripts say so in as many words. This is the same discipline
as the performance documentation: the artifact records what it is
validated for, and the boundary of that validation, in the place the next
user will actually look. A linker script that works under QEMU while
*looking* like a flash-boot script would be a trap; one that documents
"QEMU-only, here's why" is a foundation.

## CTest without an operating system

The toolchain files end with `set(SRT_BARE_METAL ON)`, and
`tests/CMakeLists.txt` branches on it. The problem it solves: CTest's
contract with a test binary is "run it with arguments, read its exit
code," and bare metal breaks both halves. There is no `argv` to pass a
`--gtest_filter`, and semihosting does not reliably propagate the guest's
exit status through `qemu-system-arm`.

The replacement is a one-shot protocol. A dedicated `main` bakes the
filter in at compile time, and the *pass criterion is a printed string*:

```cpp
{{#include ../../../tests/bare_metal_main.cpp}}
```

CTest registers a single test whose `PASS_REGULAR_EXPRESSION` is
`SRT_TESTS_COMPLETE rc=0` and whose `FAIL_REGULAR_EXPRESSION` is gtest's
`[  FAILED  ]` marker: the run passes only if the summary line is printed
*and* no failure marker ever appears. The completion line is printed at
the last possible moment, so a crash, fault, or park-loop after the tests
cannot masquerade as success — the harness times out instead (the
`Default_Handler` comment in the startup file closes this loop: faults
park, parking times out, timeouts fail).

Three details in that file repay attention:

- **The filter excludes by category, not by taste.** What is cut is
  minutes of soft-float virtual audio proving target-independent control
  math already proven on every host leg; what stays is everything only
  the target can falsify — datapath arithmetic, ring behavior on 32-bit
  `size_t`, the end-to-end converter. The comment about `AsrcQuality*`
  versus `AsrcQuality.*` records a real trap: in gtest filters the dot is
  a literal, and the wrong spelling silently *narrows* the exclusion.
- **The empty-run guard.** A filter typo can select zero tests, and
  `RUN_ALL_TESTS()` cheerfully returns 0 for an empty run — a green CI
  leg testing nothing, forever. The guard fails the run if fewer than 15
  tests were selected (the real selection is ~20; the slack allows
  legitimate removals). It must be checked *after* `RUN_ALL_TESTS()`,
  because gtest applies the filter inside it — the count reads zero
  before. This guard, like MSPLIM, arrived via audit: the theme of that
  audit was hunting for ways a passing signal could be vacuous.
- **GoogleTest itself needs the bare-metal treatment.** Newlib ships stub
  `pthread.h`/`regex.h` headers that make POSIX feature *detection*
  succeed spuriously, so the build doesn't probe for threads at all on
  bare metal and pins the feature macros (`GTEST_HAS_POSIX_RE=0`, stream
  redirection and filesystem off) — value-checked macros only, since
  gtest tests `GTEST_HAS_DEATH_TEST` with `#ifdef` and defining it to 0
  would *enable* what it names.

The result: `ctest --test-dir build` on a developer machine runs ~20
tests on an emulated Cortex-M55 exactly as transparently as the Hexagon
chapter's suite — `CMAKE_CROSSCOMPILING_EMULATOR` is doing the same work,
with `qemu-system-arm -M mps3-an547 -nographic -semihosting -kernel`
prefixed to the binary instead of `qemu-hexagon`.

## What the M55 was hiding: Helium at plain `-O2`

The M55 port existed for correctness. Its instruction baselines then sat
quietly in `bench/baselines.json` until the M33 arrived and gave them a
comparison point — and the comparison didn't add up. Identical source,
identical flags, same GCC: the M33's Q15 pipeline count came in at
roughly **4× the M55's**. Slower silicon-for-silicon was expected;
4× in *executed instructions* was not, because instruction counts don't
care about clock speed or memory latency. Something was executing
different instructions.

`objdump` answered in one line of shell: the M55 binary contained **71
MVE instructions**. The M33 binary contained **zero** (it has no MVE to
contain). Nobody had written a line of SIMD — **GCC auto-vectorizes the
Q15/Q31 kernels with Helium at plain `-O2`** when targeting
`-mcpu=cortex-m55`. The M55's numbers had been MVE-accelerated from the
day the target landed, and the project's own performance plan — which
listed "explicit Helium kernels for the M55" as future optimization
headroom — was describing work the compiler had already done. The
hypothesis list in `docs/PERFORMANCE.md` was rewritten the same day:
explicit M55 SIMD is *moot*; the real headroom was on the cores without
MVE, which became C4.

The M55 also supplied the project's most instructive documentation bug,
told in this book's introduction: the C3 integer-phase change showed
`pipeline_float` **+1.4%** on the M55, contradicting the expectation that
removing double math must help a core documented (in the project's own
notes) as having no FP64. The measurement was right and the notes were
wrong: the M55's *scalar* FPU executes FP64 in hardware — only the MVE
vector unit is fp16/fp32. C3 had traded cheap hardware doubles for int64
arithmetic on that one target, a fair price for the large cross-target
wins (Q15 −5.3%, Q31 −4.6% on the same core), and the correction is
recorded in the plan's hypothesis list. A 1.4% anomaly in a deterministic
metric was enough to falsify a "fact" everyone involved would have sworn
to. Noisy metrics don't generate that kind of pressure; this is why the
ratchet gates on instructions and not on milliseconds.

## What the M33 exists to say about the Pico 2

The M33 leg is the deployment-realism target, and its numbers are meant
to be read as a datasheet for the Raspberry Pi Pico 2 class of part.

**Float is not a datapath here.** The committed baselines put
`kernel_float` at 1,897,321,329 instructions against the M55's 99,468,474
for the same workload — the README's "~19×" — because every `double`
accumulation in the float kernel is soft-float library calls on a core
with a single-precision-only FPU. The consequence is stated as guidance,
not lament: on Pico-class parts, use Q15 or Q31, the formats the
fixed-point traits chapter built for exactly this moment.

**The DSP extension was idle until C4.** Disassembly of the original M33
binaries found barely any use of the DSP extension (two `smlal`s). The
C4 kernel fixed that with `SMLALD` — packed dual 16×16 MAC into a 64-bit
accumulator — gated on `__ARM_FEATURE_DSP && !__ARM_FEATURE_MVE` so the
M55 keeps its auto-vectorized loop (verified: 0.00% change on every M55
and Hexagon scenario), bit-exact by construction because the products are
exact in int32 and int64 accumulation is associative. It bought −3.1% on
`pipeline_q15`, and the C4 entry keeps honest books about why the win is
bounded: the M33's Q15 frame cost is dominated by the coefficient blend's
64-bit products and transport, not by the dot product the intrinsic
accelerates.

**Budgets, stated as instructions, pending cycles.** Dividing the
baselines out: `pipeline_q15` is 484,146,844 instructions per 96,000
frames ≈ **5,043 instructions per stereo frame**; the 12-channel shape is
≈ 10,027. A 150 MHz core at 48 kHz has 3,125 *cycles* per frame. The
README draws the honest conclusion in instruction-space — Q15 mono fits
a 150 MHz core, stereo wants the `fast()` preset or the RP2350's second
core — and then refuses to pretend the units match: instructions are not
cycles, the ratio between them is an empirical property of real silicon,
and the guidance is explicitly a budget *pending real-silicon
validation*.

Two flashable firmwares exist to close exactly that loop, and they are
the bridge from this chapter's emulated world to Part V's hardware:

- **`examples/pico2_cyccnt`** runs the same fixed pipeline workloads on a
  real Pico 2 and times each 32-frame block with the M33's DWT.CYCCNT
  hardware cycle counter. Its output divided by the committed baselines
  (5,043 and 10,027 instructions per frame) yields the
  cycles-per-QEMU-instruction calibration constant that turns *every*
  M33 baseline, current and future, into a real cycle budget.
- **`examples/pico2_dualcore`** is the "second core" clause made
  literal — and it is the library's concurrency story passing its
  sternest exam. The `push()`/`pull()` contract names one producer agent
  and one consumer agent around the lock-free ring; it never says
  *threads*. On the RP2350, core 0 becomes the producer clock domain
  (pushing at a synthesized +200 ppm offset, so the servo's estimate has
  an exact truth value to be judged against — the one thing two real
  crystals can never give you) and core 1 becomes the consumer, timing
  every `pull()` with its own per-core DWT. Two cores over coherent SRAM
  satisfy the acquire/release contract exactly as two threads do.
  Everything else crossing cores is 32-bit atomics only — because on the
  M33, 64-bit `std::atomic` is not lock-free, the same fact the startup
  file's PRIMASK helpers exist to paper over on *one* core and which no
  single-core trick can fix across two. Even the firmware's 12-channel
  phase runs at 16 kHz *by arithmetic, not caution*: 10,027
  instructions per frame against a 3,125-cycle budget cannot fit at
  48 kHz on one core, and `pull()` of one converter instance is one
  consumer by contract — a second core buys one clock domain per core,
  not more datapath than one core has.

## Verify it yourself

```sh
# Both bare-metal legs, end to end (arm-none-eabi-g++ and qemu-system-arm
# on PATH — exactly what CI installs):
cmake -B build-m55 -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake -DSRT_BUILD_EXAMPLES=OFF
cmake --build build-m55 -j && ctest --test-dir build-m55 --output-on-failure

cmake -B build-m33 -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake -DSRT_BUILD_EXAMPLES=OFF
cmake --build build-m33 -j && ctest --test-dir build-m33 --output-on-failure

# The Helium discovery, on today's binaries: MVE loads/MACs present in the
# M55 build, absent in the M33 build. (The recorded count at discovery was
# 71 vs 0; the exact number moves with the compiler — the zero does not.)
arm-none-eabi-objdump -d build-m55/tests/srt_tests | grep -cE 'vldr|vmlaldav'
arm-none-eabi-objdump -d build-m33/tests/srt_tests | grep -cE 'vldr|vmlaldav'

# The empty-run guard, demonstrated: break the filter in
# tests/bare_metal_main.cpp (e.g. filter = "NoSuchTest*"), rebuild, and the
# run fails with "filter is broken" instead of passing green.

# The instruction budgets (counting-plugin build is in ci.yml icount-ratchet;
# same configure for m33 with the other toolchain file):
cmake -B build-m55-ic -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
      -DSRT_BUILD_TESTS=OFF -DSRT_BUILD_EXAMPLES=OFF -DSRT_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55-ic -j
python3 scripts/icount.py --target m55 --build-dir build-m55-ic --plugin /tmp/libinsncount.so

# The budgets on real silicon (a Raspberry Pi Pico 2 and a USB cable):
#   examples/pico2_cyccnt/README.md   — cycles per frame, DWT.CYCCNT
#   examples/pico2_dualcore/README.md — one clock domain per core, self-judging
```

The two `objdump` lines are this chapter compressed: the same source, the
same compiler, the same flags — and the difference between the binaries
is a discovery you can grep for. Bare metal did not make the library
different; it made what the library was already doing *visible*, one
instruction at a time.
