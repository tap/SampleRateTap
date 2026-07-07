# Appendix A: The C++ decision log

> There are only two kinds of languages: the ones people complain about and the ones nobody uses.
>
> — Bjarne Stroustrup

Every chapter of this book has defended C++ decisions in passing, in the
context that made them necessary. This appendix collects them in one place,
in one format: the decision, what was rejected, why, and where in the
repository the evidence lives — because in this codebase the decisions are
*recorded*, mostly as comments at the point of consequence, and a decision
whose reason you cannot locate is a decision you cannot safely revisit.

A theme will emerge quickly, so it is worth stating up front. Almost every
entry below is the same decision wearing different clothes: **between a
clever general mechanism and a plain constraint you can state and verify,
this library picks the constraint.** A literal `64` over a standard
interference constant; a `static_assert` over trust; a compile-time gate
over a runtime flag; a comment that shows its arithmetic over a comment
that waves at it. Where the two genuinely conflict, the tiebreaker is
always the same pair of masters: the real-time audio contract and the
embedded targets that cannot fake their way around a bad choice.

## 1. Header-only distribution

The entire library is seven headers under `include/srt/`. The build system
declares exactly one library target, and it has no compiled artifact:

```cmake
add_library(SampleRateTap INTERFACE)
add_library(SampleRateTap::SampleRateTap ALIAS SampleRateTap)
target_compile_features(SampleRateTap INTERFACE cxx_std_20)
```

Consumption is `add_subdirectory` or `FetchContent`, deliberately and
exclusively — the README's *Consuming the library* section says so in as
many words: "there are no install/package rules yet." The tests, examples,
benchmarks and the C ABI shim are all opt-in options that default off when
the project is not top-level, and the warning flags live on a separate
`srt_warnings` target so that the library's own `-Wall -Wextra -Wpedantic
-Wconversion` discipline is never propagated into a consumer's build
(`CMakeLists.txt` carries the comment: "not propagated to consumers").

What was rejected is the conventional pair: a compiled static/shared
library, and a packaged install with exported config files. The costs of
header-only are real and were accepted knowingly. Every translation unit
that includes `srt/srt.h` re-parses and re-instantiates the templates —
compile time is paid repeatedly. There is no ABI boundary, so there is
nothing to version at link time and no way to ship a fixed `.so` to a
customer who cannot rebuild (the C ABI shim in section 15 exists precisely
for the one consumer class that needs a binary boundary).

What it buys is decisive for this library's actual deployment surface.
The code ships to bare-metal Cortex-M33/M55 firmware, a musl-libc Hexagon
toolchain, and ordinary hosts — four toolchains in CI alone, each with its
own flags, each producing incompatible binaries. A prebuilt library per
target multiplies the release matrix; a header vanishes into whatever
build the consumer already has, including builds with LTO, `-march=native`
or MVE auto-vectorization, where cross-TU inlining of the hot kernels is
exactly what the performance chapters measured. And a template library is
header-shaped by nature: the sample-type axis of section 2 means the
"library" is not a fixed set of functions but a recipe the consumer's
compiler executes.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| `INTERFACE` target, `add_subdirectory`/`FetchContent` only | compiled library; install/export packaging | four incompatible toolchains in CI; templates need instantiation in the consumer's TU; costs (compile time, no ABI) accepted, C ABI shim covers the binary-boundary case | `CMakeLists.txt`; README "Consuming the library"; `tools/capi/` |

## 2. Templates and a concept for the sample-type axis

The datapath comes in three sample types — `float`, Q15 `int16_t`, Q31
`int32_t` — and the axis is expressed as a template parameter constrained
by a concept:

```cpp
template <sample_type S>
class basic_async_sample_rate_converter { ... };

using async_sample_rate_converter    = basic_async_sample_rate_converter<float>;
using async_sample_rate_converter_q15 = basic_async_sample_rate_converter<std::int16_t>;
using async_sample_rate_converter_q31 = basic_async_sample_rate_converter<std::int32_t>;
```

The first rejected alternative is virtual dispatch: an abstract
`ISampleOps` with `mac()`, `blend()`, `finalize()` virtuals. That dies on
arithmetic grounds before it reaches performance grounds — the three
datapaths do not share signatures. The float path accumulates in `double`,
the fixed-point paths in `int64_t`; the blend factor is a `float`, a Q15
`int32_t`, or a Q20 `int32_t` depending on the type. Virtual functions
cannot vary their associated types per implementation; you would be forced
to launder everything through the widest type, which is precisely the
soft-double catastrophe the fixed-point paths exist to avoid (the M33
baselines put the float path at roughly 19× the M55's instruction count
for exactly that reason — README, platform section). And even if the types
had lined up, an indirect call per multiply-accumulate inside a 48–80-tap
loop would forfeit the inlining and auto-vectorization that Part III
measured: the M55's Q15 kernel is fast *because* GCC can see through
`sample_traits<int16_t>::mac` and emit Helium.

The second rejected alternative is CRTP — compile-time polymorphism via
inheritance. It solves the dispatch cost but contorts the shape: the
sample type here is `int16_t` itself, a builtin, not a class that can
inherit from a base. CRTP would demand wrapper types around the samples,
and wrapped samples are no longer the raw interleaved buffers that device
drivers and the `memcpy`-based ring (section 6 of the ring chapter)
require. The concept does the one job the template needs guarding for:

```cpp
template <typename T>
concept sample_type = requires(...) {
    { sample_traits<T>::mac(a, x, c) } -> std::same_as<typename sample_traits<T>::Accum>;
    // ... six more operations, each with its exact type checked
};
```

A wrong instantiation fails at the constraint with the list of missing
operations, not three template layers deep in the dot-product loop. The
header then `static_assert`s the concept against all three shipped types —
the same trust-nothing reflex as the ring's lock-free asserts.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| templates constrained by the `sample_type` concept | virtual `ISampleOps`; CRTP wrappers | per-type associated types (`Accum`, `BlendFactor`) are impossible to express virtually; builtins can't inherit; hot loops must inline and vectorize | `include/srt/sample_traits.h` (concept + `static_assert`s); `include/srt/asrc.h` aliases; README platform notes (19× soft-double) |

## 3. A traits struct as the customization point

Given templates, the customization could still have taken several shapes.
The library chose a traits struct with an intentionally undefined primary
template:

```cpp
/// Primary template intentionally undefined; specialize per sample type.
template <typename T>
struct sample_traits;
```

Each specialization bundles three associated types (`Coeff`, `Accum`,
`BlendFactor`) with seven static functions (`make_coeff`, `mac`, `blend`,
`finalize`, ...). Why this over the alternatives?

**Free functions found by ADL** — the customary `swap`-style mechanism —
were worse for two reasons. First, the customization is mostly *types*,
not functions: the fact that Q15 stores coefficients as Q1.14 `int16_t`
but accumulates in `int64_t` is the design (the header's comments derive
it: Q0.15 × Q1.14 products summed exactly, one rounding in `finalize()`).
Free functions cannot carry associated types; you would need separate type
traits anyway, and the customization point would smear across two
mechanisms. Second, ADL on builtin types like `int16_t` has no associated
namespace to hook — the overloads would all pile into `srt` and be
distinguishable only by overload resolution, silently, which is exactly
how a Q15/Q31 mixup would compile and produce garbage.

**Member policies** — making the sample type a class that knows its own
arithmetic — fail as in section 2: the sample types must remain raw
builtins so buffers stay `memcpy`-compatible and ABI-identical to what
audio drivers produce. A traits struct is the standard C++ answer for
attaching behavior to types you cannot modify, and the undefined primary
template makes "I forgot to specialize" a clean compile error at the point
of use rather than a link error or a default that half-works.

The struct also keeps each datapath's documentation in one screenful: the
Q15 specialization's header comment is a complete fixed-point error budget
(coefficient quantization at ~−86 dB, single rounding point, "the
converter is Q15-transparent"), sitting directly above the ten lines that
implement it.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| `sample_traits<T>` struct, undefined primary template | ADL free functions; member policies on sample classes | customization is chiefly associated types; builtins have no ADL namespace and can't have members; missing specialization = clean compile error | `include/srt/sample_traits.h` |

## 4. The real-time contract: exceptions at setup, `noexcept` forever after

This is the load-bearing wall of the whole API, stated as a contract in
the converter's class comment:

```cpp
/// Real-time contract: the constructor performs all allocation and filter
/// design and may throw; push(), pull(), status() and reset_from_consumer()
/// are noexcept, lock-free and allocation-free.
```

The README's feature bullets repeat it, because it is the feature. The
constructor allocates every buffer the object will ever touch — ring,
polyphase table, histories, scratch — designs the filter in double
precision, validates the configuration, and throws `std::invalid_argument`
or `std::bad_alloc` on anything wrong. From that point on, the audio path
never allocates, never locks, never throws; every hot function is spelled
`noexcept`, and the `validated()` function exists to make the constructor
*more* throw-happy, rejecting configurations that "would otherwise
construct successfully and misbehave silently" — NaN sample rates that
design all-NaN tables, band edges that pass images wholesale, deviation
clamps that overflow the Q0.64 conversion (its comment lists each one).

The rejected alternatives are the two ways other libraries split this.
Error codes at setup ("check the return of `init()`") were rejected
because a partially-constructed converter is not a state this object can
represent — there is no meaningful "converter without a filter table," and
C++ constructors-that-throw are precisely the tool that makes invalid
objects unrepresentable. Exceptions on the audio path were never
considered — an unwind inside a device callback is a glitch at best — but
the *strength* of the setup/hot-path split was reinforced from an
unexpected direction. When the first `EXPECT_THROW` test reached the
Hexagon CI leg, it discovered that the hexagon-linux-musl toolchain
cannot catch exceptions at all: a constructor throw terminates via
libc++abi instead of propagating. `docs/PERFORMANCE.md` records it under
Known debt, with the deployment note ("treat invalid config as fatal —
validate inputs before constructing") and the candidate fix
(`-unwindlib=libunwind`). The discovery cost one excluded test on one leg
— because exceptions had been confined to a code region where "terminate
instead of propagate" is survivable. Had the audio path thrown, the same
toolchain quirk would have been a field failure.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| all allocation + throwing in the constructor; `noexcept`/lock-free/allocation-free hot path | `init()` + error codes; exceptions anywhere near audio | invalid objects unrepresentable; RT contract is the product; Hexagon's no-unwind toolchain proved the value of confining throws to setup | `include/srt/asrc.h` (class comment, `validated()`); README bullets; `docs/PERFORMANCE.md` Known debt; commit "Hexagon: exclude ConfigValidation" |

## 5. Runtime filter design, not `constexpr` tables

A modern-C++ reflex says the Kaiser-windowed prototype — pure math on
compile-time-known presets — should be a `constexpr` table. The library
computes it at runtime, in the constructor, and `kaiser.h` opens with
the reason, arithmetic included:

```cpp
/// Design note — runtime vs constexpr: the prototype tables run 12K-33K taps
/// and each tap needs sin/sqrt plus a ~50-term Bessel I0 series. Constexpr
/// evaluation is interpreted (roughly 1e3-1e4x slower than native), would
/// need hand-rolled constexpr transcendentals before C++26, and would cost
/// tens of seconds to minutes of compile time in every including translation
/// unit. Runtime design takes well under 10 ms, runs once in a constructor,
/// and is off the audio path, so all design math here is plain runtime
/// double precision.
```

Unpack the trade. The `balanced()` preset's prototype is 256 × 48 =
12,288 taps, and the presets range upward from there — the comment's
"12K-33K taps". Each tap evaluates `sin`,
`sqrt`, and a Bessel-I0 power series that runs to ~50 terms. `constexpr`
evaluation is an interpreter inside the compiler — three to four orders
of magnitude slower than native — and, before C++26, `std::sin` and
friends are not `constexpr`, so the transcendentals would have to be
hand-rolled *and then trusted* to match runtime libm behavior. In a
header-only library the bill lands in every consumer TU, repeatedly. The
runtime version costs under 10 ms, once, in the constructor — which
section 4 already designated as the place where expensive things happen.
And a runtime design accepts *runtime* configurations: `filter_spec` is
not limited to the three presets, so a compile-time table would have been
a special case bolted alongside the general path, not a replacement.

This is the header-only cost model (section 1) feeding back into design:
having accepted per-TU compilation, the library polices what each TU
costs.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| filter designed at runtime in the constructor | `constexpr` coefficient tables | 12K–33K taps × transcendentals ≈ minutes of interpreted compile time per TU vs <10 ms once at runtime; needs pre-C++26 hand-rolled constexpr math; runtime `filter_spec` must work anyway | `include/srt/detail/kaiser.h` header comment |

## 6. `<bit>` over hand-rolled bit tricks; masks over modulo

Everywhere the library needs power-of-two arithmetic it reaches for
C++20's `<bit>`: `std::bit_ceil` rounds the ring capacity up
(`spsc_ring`'s constructor), rounds the phase count up
(`polyphase_filter_bank`), and sizes the FIFO (`ring_capacity_elems` in
`asrc.h`); `std::countr_zero` recovers log₂(L) in the phase-indexed
kernels so the polyphase branch is the top bits of the Q0.64 phase word:

```cpp
const int lg = std::countr_zero(bank.num_phases()); // L is a power of two
const std::size_t p = static_cast<std::size_t>(phase >> (64 - lg));
```

The rejected alternative is the folklore versions — the
shift-or-shift `bit_ceil`, the de Bruijn log₂ — which every C programmer
has written and half have gotten wrong at the boundaries (what does your
hand-rolled `bit_ceil` do at 0? at values above 2⁶³?). The standard
functions have specified edge behavior, compile to single instructions
where they exist, and *name the intent* — `countr_zero(num_phases())`
under the comment "L is a power of two" is an invariant stated twice.

The deeper decision is what the powers of two are *for*: indexing by mask
instead of modulo. The ring's monotonic indices are wrapped by `head &
mask_` — its class comment: "Indices are monotonic and wrapped by a
power-of-two mask, so the full capacity is usable" — and the ring chapter
proves the wraparound benign. The polyphase table's L being a power of
two is what lets the Q0.64 phase word split into branch index and blend
fraction by pure shifts, with no division and no double arithmetic on the
per-sample path (the phase-accumulator comment in
`polyphase_filter.h`). A general-modulo design would put an integer
divide — tens of cycles on the M-class cores, and a serialization point
everywhere — inside the tightest loops the library owns, to support
capacities nobody asked for.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| `std::bit_ceil` / `std::countr_zero`; power-of-two capacities indexed by mask | hand-rolled bit tricks; arbitrary sizes with `%` | specified edge cases, single instructions, intent named; masks keep divides and doubles off the per-sample path | `include/srt/spsc_ring.h` ctor + class comment; `include/srt/polyphase_filter.h` (`blend_row_phase`, `interpolate_phase`, `ring_capacity_elems`) |

## 7. Memory orderings chosen to be exactly sufficient

The ring chapter walked this in full; the appendix records it as policy,
because it generalizes beyond the ring. Every atomic operation in the
library carries an explicit ordering argument, and each ordering is the
*weakest* that keeps the algorithm correct: `release` on the store that
publishes data, `acquire` on the load that consumes a foreign index,
`relaxed` on a thread's loads of its own index — and `relaxed` on all
telemetry, whose fields are documented as "individually coherent, not
mutually" (`status()` in `asrc.h`).

The rejected alternative is `seq_cst`-by-default — writing
`head_.store(x)` and letting the strongest ordering paper over the
analysis. It would be correct. It was rejected first because it is
measurably stronger than needed on the weakly-ordered targets (full
barriers on ARM in the hottest loop the library owns), and second — the
argument this codebase actually leads with — because **orderings are
documentation**. An explicit `memory_order_relaxed` on `tail_.load()` in
the producer tells the reader "this is my own index; no synchronization
happens here" — a claim the ring chapter spells out and ThreadSanitizer
checks against reality in CI. A default `seq_cst` says only "I didn't
think about this," and in the one file whose entire job is to be thought
about, that is the wrong message. The same honesty cuts the other way:
where synchronization *is* needed, the annotation names which one, so a
future editor who weakens it is contradicting a written claim, not
merely changing a default.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| explicit, minimal orderings on every atomic | `seq_cst` defaults | weaker barriers on ARM where it matters; each annotation documents exactly why it exists; TSan-checked in CI | `include/srt/spsc_ring.h`; `include/srt/asrc.h` telemetry; the ring chapter's "What was rejected" |

## 8. `alignas(64)`, not `std::hardware_destructive_interference_size`

The ring separates producer-owned, consumer-owned and shared-read-only
state onto distinct cache lines, and it does so with a named literal:

```cpp
// 64-byte separation to keep producer- and consumer-owned state on
// distinct cache lines (std::hardware_destructive_interference_size is
// deliberately avoided: it is ABI-fragile and warns on GCC). ...
static constexpr std::size_t k_cache_line = 64;
```

The standard offers a constant whose whole purpose is this alignment, and
the file's comment rejects it by name. The problem is that
`hardware_destructive_interference_size` is not a constant of the
architecture; it is a constant of the *compiler invocation* — its value
can change with `-mtune`, which means two translation units in the same
program can disagree about the layout of the same type. That is an ODR
violation waiting for a victim, and GCC ships a warning
(`-Winterference-size`) telling you exactly this whenever the constant is
used in a context that might cross an ABI boundary. A header-only library
(section 1) lives *entirely* in that danger zone: every consumer TU
re-instantiates `spsc_ring`, potentially under different flags.

A plain `64` is correct on every target this project ships to, cannot
vary between TUs, and states its assumption in a comment a porting
engineer will read. The general lesson — the ring chapter phrases it as
"between a standard facility and a constraint you can state plainly,
prefer the one whose failure mode you can reason about" — is this
appendix's opening theme in miniature.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| `alignas(k_cache_line)` with `k_cache_line = 64` | `std::hardware_destructive_interference_size` | the standard constant varies with tuning flags → ODR/ABI fragility in a header; GCC warns; 64 is right everywhere shipped | `include/srt/spsc_ring.h` member layout comment |

## 9. 32-bit telemetry atomics

The converter's telemetry — state, ppm, fill, underrun/overrun/resync
counters, effective setpoint — is deliberately 32 bits wide, and the
comment above the members carries the whole argument:

```cpp
// Telemetry is 32-bit on purpose: 64-bit atomics fall back to lock-based
// libatomic on 32-bit targets (e.g. Hexagon), which would break the
// lock-free contract of the hot path. float carries ~7 significant
// digits — ample for ppm/fill observability; counters wrap at 2^32.
```

The rejected alternative — `std::atomic<std::uint64_t>` counters and
`double` gauges, the "obviously roomier" choice — is a trap on exactly
the targets this library most cares about. On a 32-bit ISA without a
64-bit atomic instruction, `std::atomic<uint64_t>` still compiles and
still works: libatomic implements it *with a lock*. The hot path would
remain formally correct and silently stop being lock-free — the one
property section 4 declared as contract, broken invisibly by a telemetry
counter. The 32-bit choice keeps every telemetry access a plain
lock-free operation on Hexagon and the M-class cores, and the class
`static_assert`s it rather than assuming:

```cpp
static_assert(std::atomic<int>::is_always_lock_free &&
                  std::atomic<float>::is_always_lock_free &&
                  std::atomic<std::uint32_t>::is_always_lock_free,
              "telemetry atomics must be lock-free for the RT contract");
```

The cost is range, and it is documented rather than hidden: `converter_status`'s
comment tells callers the counters "wrap at 2^32 — far beyond any
plausible event count, but treat them as modular if you difference them
over very long horizons." (The `converter_status` struct itself still presents
`uint64_t` fields — the narrowing is an internal representation choice,
widened at the snapshot.) A `float` gauge carries about seven significant
digits, which comfortably resolves tenths of a ppm and hundredths of a
frame of fill — observability, not metrology.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| `atomic<uint32_t>`/`atomic<float>` telemetry, wrap documented | 64-bit atomic counters/doubles | 64-bit atomics lock via libatomic on 32-bit targets, silently voiding the lock-free contract; 32-bit range/precision suffices and is asserted | `include/srt/asrc.h` telemetry members + `static_assert`; `converter_status` doc comment |

## 10. Designated initializers as API

The filter presets are written the way a datasheet reads:

```cpp
static filter_spec transparent() noexcept {
    return {.num_phases = 512,
            .taps_per_phase = 80,
            .passband_hz = 20000.0,
            .stopband_hz = 26000.0,
            .stopband_atten_db = 140.0};
}
```

`filter_spec`, `config` and `servo_config` are aggregates with member
initializers supplying defaults, and C++20 designated initializers do the
rest. The rejected alternatives are the two classic config-struct styles.
A positional constructor —
`filter_spec(512, 80, 20000.0, 26000.0, 140.0)` — puts two adjacent
`double` band edges next to each other where a swap compiles silently and
mis-designs the filter (which, per `validated()`'s comment, is the kind
of error that "passes images wholesale"). A builder/setter chain adds a
mutable construction protocol and a second way for every field to be set,
to solve a problem the language now solves natively: fields are named at
the call site, unmentioned fields keep their documented defaults, and —
because designated initializers must follow declaration order — the
compiler rejects reorderings instead of reinterpreting them.

The style is also the library's own consumption idiom: the README quick
start and every test build configs by naming only what deviates from
default. Readable initialization is not cosmetic in a config API; the
config *is* the API surface where users make their quality-versus-cost
decisions, and the presets double as documentation of three known-good
points in that space.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| aggregate configs + designated initializers | positional constructors; builder chains | named fields make adjacent-double swaps impossible; defaults stay declarative; declaration-order enforcement | `include/srt/polyphase_filter.h` (`filter_spec` presets); `include/srt/asrc.h` (`config`); `include/srt/pi_servo.h` (`servo_config`) |

## 11. `SRT_RESTRICT`: a portable `__restrict__`, adopted on measurement

C++ has no standard `restrict`. The library defines a two-line macro over
the compiler extensions and applies it to the kernel pointer parameters —
and the comment above the macro is careful to claim only what was
verified:

```cpp
// No-alias qualifier for the kernel hot loops: without it the compiler
// versions the blend loop behind a runtime aliasing check (verified with
// -fopt-info-vec; see docs/PERFORMANCE.md, hypothesis 2).
```

This entry is here as much for its *method* as its content. The
vectorization audit (PERFORMANCE.md, PR C2) did not assume aliasing was a
problem; it asked the compiler. `-fopt-info-vec` showed `blend_row`
vectorizing — but behind a runtime aliasing check, the loop compiled
twice with a pointer-overlap branch choosing between versions.
`SRT_RESTRICT` on the row/history pointers removes the check, and the
measured effect is recorded with the honesty this project's performance
docs enforce: **M55 `pipeline_float` −1.35% instructions, every other
embedded scenario exactly 0.00%, x86 same-state A/B −3.7% wall-clock.**
Small, real, and cheap — the qualifier documents a true invariant (the
scratch row never aliases the history), so it costs nothing to maintain.

The rejected alternatives: doing nothing (leaving the versioned loop and
its branch in the hot path), and restructuring the code so the compiler
could prove non-aliasing itself (possible, but contorting call signatures
to communicate what one keyword states directly). MSVC spells the
extension `__restrict`, everyone else `__restrict__`; hence the macro
rather than a raw keyword.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| `SRT_RESTRICT` macro on kernel pointers | nothing (alias-versioned loops); structural non-aliasing proofs | verified with `-fopt-info-vec`, measured: M55 float −1.35% insns, x86 −3.7% wall-clock; states a true invariant | `include/srt/polyphase_filter.h` macro + comment; `docs/PERFORMANCE.md` C2 |

## 12. Compile-time feature gates — and the measured cost of a runtime one

Target-specific code paths are selected by preprocessor and `constexpr`
machinery, never by runtime flags. `SRT_Q15_SMLALD` turns on the dual-MAC
Q15 dot product exactly where it wins:

```cpp
#if defined(__ARM_FEATURE_DSP) && !defined(__ARM_FEATURE_MVE)
```

— DSP-extension cores *without* Helium (the M33/Pico class), because on
the M55 the compiler already auto-vectorizes the scalar loop with MVE and
the intrinsic would replace vectors with dual-MACs (the gate's comment;
PERFORMANCE.md C4 verified 0.00% change on every M55 scenario).
`SRT_CHANNEL_PARALLEL` enables the frame-major channel axis on hosts only,
and inside the class it becomes a `constexpr` member flag that
`if constexpr` and plain constant folding erase from non-participating
builds:

```cpp
static constexpr bool k_channel_parallel =
    SRT_CHANNEL_PARALLEL != 0 && std::is_floating_point_v<S>;
```

The reason this is dogma rather than taste is that the alternative was
tried, by accident, and measured. During C6 the mode gate was briefly an
ordinary runtime `bool` consulted in the hot loops — and the M55
instruction ratchet, which had nothing to do with the change (C6 is
host-only), moved **+6–8%** from hot-loop branch bloat. PERFORMANCE.md
records the lesson verbatim: "the mode gate must be compile-time — a
runtime bool in the hot loops cost +6–8% on the M55 ratchet before the
constexpr gate restored every embedded scenario to 0.00%." The compaction
path in `append_one` carries the same note at the exact line that was
guilty. A ±3% two-sided CI gate is what turned this from a silent tax
into a failed build; the constexpr gate is what turned the fix from "fast
again" into "provably byte-identical again."

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| preprocessor + `constexpr` flags + `if constexpr` gates | runtime mode flags | a runtime bool in the hot loop measured +6–8% on the M55 ratchet; compile-time gates keep non-participating targets' codegen byte-identical (0.00%) | `include/srt/polyphase_filter.h` (`SRT_Q15_SMLALD`, `SRT_CHANNEL_PARALLEL`, `k_channel_parallel`, `append_one` comment); `docs/PERFORMANCE.md` C4/C6 |

## 13. `std::function` in the simulator, templated callables in the library

The test harness's two-clock simulator configures its signal generators
as `std::function` fields:

```cpp
std::function<S(std::uint64_t)> gen = [](std::uint64_t) { return S{}; };
std::function<double(double)> fs_in_scale = [](double) { return 1.0; };
```

The library's hot path, facing the identical "caller supplies a callable"
problem, does something else entirely. `fractional_resampler::process`
takes its frame source as a template parameter —
`template <typename PopFn> std::size_t process(..., PopFn&& pop_frames)
noexcept` — and the converter passes a `noexcept` lambda that wraps the
ring read. Same need, opposite tools, and the split is deliberate.

`std::function` is the right tool in the simulator: tests assign
different generators per test case at runtime, the cost of a type-erased
call per sample is irrelevant next to the double-precision sine it
invokes, and construction-time allocation in a test fixture harms
nothing. It would be the wrong tool in `process()` three ways at once.
Its call is an indirect jump through erased type information that the
optimizer cannot inline — and `pop_fn` is invoked inside the per-frame
loop, where the entire benefit of the current design is that the ring's
`read()` inlines into the resampler's refill path. Assigning one may
allocate, which is forbidden anywhere reachable from `pull()`
(section 4). And its call operator is not `noexcept` — an empty
`std::function` throws `bad_function_call` — which poisons the `noexcept`
audio path either with a formal lie or a terminate-on-bug. The template
parameter has none of these problems and costs only what templates
always cost: the code is instantiated per callable type, which for
exactly one production callable is nothing.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| templated `PopFn&&` in the library; `std::function` only in test config | `std::function` on the hot path; templates in test fixtures | hot path needs inlining, no allocation, honest `noexcept`; tests need runtime reassignment and don't care about a type-erased call | `include/srt/polyphase_filter.h` (`process`, `prime`); `include/srt/asrc.h` (`pop_fn` lambda); `tests/support/two_clock_sim.h` |

## 14. `std::vector` everywhere, custom allocators nowhere

Every owned buffer in the library is a plain `std::vector`: the ring's
storage, the coefficient table, the resampler's histories, scratch and
blended row. No allocator parameters, no PMR, no small-buffer tricks. In
a real-time audio library this looks, at first glance, like negligence —
until you notice *when* those vectors are touched. Every `resize`,
`assign` and construction happens in a constructor or in `prime()`-time
setup; the hot path only ever reads `data()` and indexes. The RT problem
with allocation is not that heap memory is slow; it is that allocation
is unbounded and lock-taking *at the moment you cannot afford it*.
Section 4's contract solves that by construction-time-only allocation —
after which a custom allocator has nothing left to fix. It would add a
template parameter that infects every class signature, a policy decision
for every consumer, and a second code path to test, in exchange for
optimizing events that occur once per converter lifetime, off the audio
thread, in a place explicitly allowed to throw `bad_alloc`.

The rejected-in-spirit alternatives — fixed `std::array` capacities, or
caller-supplied arenas — also fail the configurability test: table and
buffer sizes derive from runtime `filter_spec` and `config` values
(section 5), so compile-time capacities would cap the very parameters
the config API exposes. Embedded consumers who must avoid the heap
entirely have the honest option the design leaves open: construct the
converter during initialization, when the heap (or a bump allocator
behind `operator new`) is still a fine place to get memory from.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| `std::vector` storage, default allocator | allocator/PMR parameters; fixed arrays; arenas | allocation is construction-only by contract, so allocators optimize a non-problem at the cost of infecting every signature; sizes are runtime config | `include/srt/spsc_ring.h`, `polyphase_filter.h`, `asrc.h` (members); RT contract in section 4 |

## 15. The C ABI: opaque handles, `reinterpret_cast`, and `impl()` outside `extern "C"`

The FFI surface (`tools/capi/`) wraps the float converter behind an
opaque `SrtHandle*`. The pattern is textbook, but two details record
decisions. First, the handle is a declared-but-never-defined struct, and
the conversion is a `reinterpret_cast` in a pair of helpers:

```cpp
extern "C" { struct SrtHandle; } // opaque

namespace {
srt::async_sample_rate_converter* impl(SrtHandle* h) noexcept { ... }
const srt::async_sample_rate_converter* impl(const SrtHandle* h) noexcept { ... }
}
```

The helpers live in an anonymous namespace *outside* the `extern "C"`
block for a reason C++ makes easy to forget: those two `impl` functions
are overloads (const and non-const), and **overloading is illegal under C
linkage** — C linkage names carry no type information to distinguish
them. Keeping the C++ conveniences in C++ linkage and only the exported
symbols in `extern "C"` is the discipline that lets the shim be written
as C++ without leaking C++ into the ABI.

The rejected alternatives for the handle: exposing the class definition
(no ABI stability — the whole point of the shim is a boundary the C++
headers don't have, per section 1), or a lookup table of integer handles
(indirection and lifetime bookkeeping to solve a problem the opaque
pointer already solves). Around the handle, the shim converts the C++
error model to C conventions at the boundary: `srt_create` catches
everything and returns null; every entry point tolerates a null handle,
because — the file's own comment — the documented "check srt_create for
NULL" convention "otherwise invites a crash on exactly the path where the
caller forgot to check." An unchecked failure degrades to silence, not a
crash, which for an audio library is the correct failure sound.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| opaque `SrtHandle*` + `reinterpret_cast`; `impl()` overloads outside `extern "C"`; null-tolerant entry points | exposed class; handle tables; unguarded entries | ABI boundary with zero C++ leakage; C linkage forbids overloads; unchecked create must fail soft | `tools/capi/srt_capi.cpp`, `tools/capi/srt_capi.h` |

## 16. Deleted copy operations: these are identity types

Both concurrency-bearing classes delete copying:

```cpp
spsc_ring(const spsc_ring&) = delete;
spsc_ring& operator=(const spsc_ring&) = delete;
```

and likewise `basic_async_sample_rate_converter`. The rejected alternative —
letting the compiler generate copies, or writing "deep copy" semantics —
fails the simplest question first: *what would a copy even mean?* A ring
mid-stream has a producer thread and a consumer thread holding a
reference to *this specific object*; a copy would duplicate the buffer
contents but not the relationship, producing an orphan that no thread
feeds. (Mechanically, `std::atomic` members are not copyable anyway —
the language is trying to tell you the same thing.) The converter is
worse: copying would snapshot servo state, telemetry and half-consumed
scratch into a second object whose FIFO occupancy no longer corresponds
to any real clock relationship. These are what the two-agent contract
makes them: objects with identity, addressed by the threads that share
them, not values to be passed around. Deleting the operations turns the
meaningless question into a compile error — the same conversion of
convention into compiler-enforced fact as the `static_assert`s
(sections 2, 9) and the concept (section 2). Moves are deleted along
with copies (declaring the deleted copy suppresses them), which is also
right: a moved-from ring would invalidate the pointers the other thread
is using *right now*.

| Decision | Rejected | Reason | Evidence |
|---|---|---|---|
| deleted copy (and hence move) on ring and converter | default/deep copies | two live threads reference the object by identity; a copy duplicates state but not the clock relationship; atomics aren't copyable | `include/srt/spsc_ring.h`, `include/srt/asrc.h` |

## 17. Rejected wholesale, with reasons

Some decisions are visible only as absences. For each, the reason is on
record.

**`std::simd` / `std::experimental::simd`.** Not in C++20 — the library's
floor — and the portable-SIMD abstraction solves a problem this codebase
measured its way out of differently: where explicit SIMD wins, it is
gated per target and per measurement (the SMLALD path, +measured, kept;
the Hexagon `vrmpyh` path, −0.31%, implemented, proven bit-exact, and
*deliberately deleted* per the stop rule — PERFORMANCE.md C5). Where
auto-vectorization already wins (Helium on the M55, host AVX2 via the
channel axis), abstraction would only obscure what `-fopt-info-vec` and
`objdump` verified.

**Coroutines.** The library's callers are device callbacks with hard
deadlines: `push()` on the capture thread, `pull()` on the playback
thread, both synchronous by the nature of the contract. No async model
fits — a suspension point inside a real-time callback is a category
error, and the frame flow the library does need (the resampler pulling
from the ring mid-synthesis) is expressed by the `PopFn` callable of
section 13 at zero machinery.

**CRTP mixins.** Section 2's reasons in general form: the concept + traits
pair already delivers static dispatch and constraint checking without
forcing an inheritance shape onto builtin sample types or wrapper types
onto raw buffers.

**Exceptions on the audio path.** Section 4; reinforced by a toolchain
that cannot unwind at all.

**`std::jthread` (or any thread) in the library.** The library owns *no*
threads. It is a passive object with a two-agent contract — "one producer
thread calls push() at the input clock; one consumer thread calls pull()
at the output clock" (`asrc.h`) — and the threads belong to the caller,
because they already exist: they are the audio device callbacks. Spawning
threads would also be unbuildable on half the CI matrix; the bare-metal
targets have no `std::thread` at all, which is why even the *tests*
compile the two-thread stress only where `find_package(Threads)` succeeds
(`tests/CMakeLists.txt`).

**Virtual interfaces for "pluggable filters."** The filter is not a
plugin point; it is a *parameter space*. `filter_spec` exposes the five
numbers that matter (L, T, band edges, attenuation) and the design
machinery is one fixed, well-understood method (Kaiser-windowed sinc)
whose properties the quality tests pin. An `IFilterDesigner` interface
would buy the ability to substitute arbitrary coefficient tables at the
cost of an indirect call chain into the kernel (section 2's costs) and
the loss of every invariant the code currently states about its own
tables — per-branch DC gain, the extra phase row's exact continuity,
the measured |diff| ≤ 41 adjacent-phase delta of section 18.

| Rejected | Reason | Evidence |
|---|---|---|
| `std::simd` | not in C++20; per-target measured intrinsics (kept or deleted by number) beat portable abstraction | `docs/PERFORMANCE.md` C4/C5 |
| coroutines | hard-RT synchronous callbacks; no async model fits | `include/srt/asrc.h` thread contract |
| CRTP mixins | concept + traits already give static dispatch without inheritance shape | `include/srt/sample_traits.h` |
| audio-path exceptions | RT contract; Hexagon cannot unwind | section 4 |
| `std::jthread` in the library | passive two-agent object; caller owns the (callback) threads; bare metal has none | `include/srt/asrc.h`; `tests/CMakeLists.txt` Threads probe |
| virtual pluggable filters | filter is a parameter space, not a plugin point; would cost kernel inlining and table invariants | `include/srt/polyphase_filter.h` (`filter_spec`) |

## 18. The meta-decision: comments that show their arithmetic

Read back through the evidence column of this appendix and notice where
it points: overwhelmingly at *comments*. The library's final C++ decision
is about prose. Its comments do not narrate ("increment the index");
they state constraints and record arithmetic at the point where the code
depends on them. The Q15 traits comment derives the accumulator budget
("48-80 taps add ~6-7 bits — no overflow, no intermediate rounding"). The
`kaiser.h` note quantifies the constexpr rejection (section 5). The
resampler's eps conversion documents its own safety margin ("|eps| is
servo-clamped to ~1e-3, so eps * 2^64 fits int64 comfortably"). The
`append_one` compaction comment carries the +6–8% scar of section 12.
These comments are load-bearing: they are the reasons future editors
will weigh before changing the code, so they are held to the same
standard as the code.

Including being *audited*. The package audit that hardened the core
(commit `029607f`, "Core hardening from the package audit") checked the
comments' arithmetic along with the code's, and found one wrong: the Q15
`blend()` comment claimed the int32 product had "~5% margin" against a
worst-case adjacent-phase delta. The audit did the multiplication —
32767 × 65535 = 2,147,385,345, which sits 0.005% under `INT32_MAX`, not
5% — and the commit's own summary records the fix: "Q15 blend margin
comment corrected (0.005%, not ~5%)." The corrected comment in
`sample_traits.h` now shows the numbers and the measurement
(real deltas: |diff| ≤ 41 on the transparent table) and draws the
conclusion the wrong margin obscured: "a margin that thin is not an
invariant worth relying on silently" — which is precisely why the code
computes the blend in `int64_t`. Note what did *not* change: the code
was already right. The comment was the bug.

That is the standard this appendix has been documenting all along. A
decision is not what the code happens to do; it is a claim, written where
the code makes it true, precise enough to be checked — and checked.
