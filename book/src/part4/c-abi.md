# The C ABI

> Be conservative in what you do, be liberal in what you accept from others.
>
> — Jon Postel, RFC 761

This chapter exists because of a plot. Part II's notebooks are the
library's most persuasive evidence — the servo locking from a cold start,
the 135 dB money plot, the naive-FIFO spectrogram full of clicks — and
every curve in them comes from the *actual shipping library*, not a Python
reimplementation of it. A reimplementation would prove nothing: the entire
point of measurement-first development is that you measure the artifact you
ship. So Python has to call the C++ converter.

And Python cannot. Neither can Julia, nor anything else that loads shared
libraries at run time, because C++ deliberately has no stable binary
interface. Symbol names are mangled, and the mangling differs by compiler.
Exceptions, RTTI, and the layout of `std::` types differ by compiler *and*
by standard-library vendor. A template — and this whole library is
templates — doesn't exist in a binary at all until something instantiates
it. The one interface every FFI on earth speaks (`ctypes`, `cffi`, Julia's
`ccall`, Rust's `extern "C"`, every scripting language's dlopen wrapper) is
the C ABI: plain functions, plain data, names that mean what they say.

So the library ships a shim: `tools/capi/`, about ninety lines of C++
presenting a C face, built as a shared library with `-DSRT_BUILD_CAPI=ON`.
This chapter is small because the shim is small, but three of its design
decisions were paid for the hard way — one by a compile error, one by an
audit finding, and one by a toolchain that turned out to be unable to
throw an exception at all.

## The surface: eight functions

The entire foreign-function interface:

```c
{{#include ../../../tools/capi/srt_capi.h:abi_surface}}
```

Create, destroy, push, pull, status, latency, reset, version. The shim
wraps the *float* converter only — the notebooks are metrology instruments
and float is what they measure with; tripling the surface for Q15/Q31
would triple the contract for consumers that don't exist yet. Minimalism
here is a feature: every function in an ABI is a promise you keep forever.

`SrtHandle` is the classic opaque-handle pattern: a `typedef` of a struct
that is *declared* and never *defined*. C callers can hold a
`SrtHandle*`, pass it around, and store it — but never dereference it,
size it, or copy what it points to, because the compiler has no idea what
it is. Compared to the lazier convention of handing out `void*`, the named
opaque type keeps some type checking alive at the boundary: pass a
`FILE*` where an `SrtHandle*` belongs and a C compiler will at least warn.
The pointer's true identity lives on the other side of the wall.

## Two `extern "C"` blocks and the lesson between them

Here is the other side of the wall, and the file structure is itself a
fossil of a compile error:

```cpp
{{#include ../../../tools/capi/srt_capi.cpp:abi_impl}}
```

The handle is simply the converter pointer in disguise —
`reinterpret_cast` in `srt_create`, `reinterpret_cast` back on every call.
No wrapper struct, no registry of live handles, no indirection table:
there is nothing to store beyond the object itself, so the handle *is* the
object.

Look at where the `impl()` helpers live: in an anonymous namespace,
*between* two `extern "C"` regions rather than inside one. That placement
is load-bearing. There are two `impl()` functions — one taking
`SrtHandle*`, one taking `const SrtHandle*` — which is to say, `impl` is
**overloaded**. And overloading is illegal for functions with C linkage:
C has no name mangling, both overloads would demand the same symbol name,
and the program is ill-formed. Write the helpers in the obvious place —
inside the `extern "C"` block where everything else in the file lives —
and the compiler stops you cold. That is exactly how it was discovered
here. The fix is what you see: the helpers sit outside the C-linkage
region, in an anonymous namespace that both gives them ordinary C++
linkage (overloading welcome) and keeps them out of the shared library's
exported symbol table, where an FFI user enumerating symbols would only be
confused by them. The general rule: `extern "C"` is for the eight names
you are promising to the world, and *nothing else* belongs inside it.

## The error convention, and why every function tolerates `NULL`

The shim's entire error vocabulary is one value:

```cpp
{{#include ../../../tools/capi/srt_capi.cpp:abi_create}}
```

`srt_create` returns `NULL` on invalid configuration or allocation
failure. No error codes, no `errno`, no last-error string: for a
constructor with a handful of scalar parameters, "it didn't work, and the
header tells you the two reasons it can't" is a complete diagnostic, and
every additional error channel is more contract to keep frozen forever.

The subtle decision is downstream of that one. The first version of this
shim checked nothing: `srt_push` cast the handle and called through it,
unconditionally. The hardening audit changed every entry point to this
shape:

```cpp
{{#include ../../../tools/capi/srt_capi.cpp:abi_null}}
```

The reasoning is stated in the file's own header comment, and it is worth
reading as a small essay on API design:

```cpp
{{#include ../../../tools/capi/srt_capi.cpp:abi_doc}}
```

A "check create for NULL" convention *concentrates* failure on precisely
the caller who forgot the check — the one writing quick notebook code, the
one least prepared for a segfault in a foreign runtime where the crash
arrives with no C++ stack and no Python traceback, just a dead kernel.
With the guards, an unchecked failed create degrades to a converter that
accepts nothing and produces zeros: `srt_pull` returns silence, which is —
not coincidentally — the same thing the real converter produces on
underrun. The failure is still visible (`srt_status` reports zeros, the
audio is silent), but it is *debuggable* instead of fatal. Eight null
checks on functions that move hundreds of frames per call cost nothing
measurable; they buy an FFI that fails the way dynamic-language users can
diagnose.

## The header is the contract

`srt_capi.h` did not exist in the shim's first version — the notebook
simply re-declared the prototypes in `ctypes`, which worked and proved
nothing for anyone else. The audit shipped the header, and its top comment
is the ABI's real substance — the part no binary interface can encode:

```c
{{#include ../../../tools/capi/srt_capi.h:abi_contract}}
```

Three promises deserve emphasis, because each answers a real foreign-caller
failure mode.

**Thread affinity is spelled out per function.** The C++ API's
single-producer/single-consumer contract (the ring chapter) does not
dissolve because the caller is Python or Julia — but an FFI user cannot see
`std::memory_order` annotations, so the header must say it in words: one
thread pushes, one thread pulls, `srt_status` from anywhere,
`srt_reset_from_consumer` only from the consumer, create/destroy never
concurrent with anything. An ABI that documents signatures but not thread
affinity has documented the easy half.

**`size_t` follows the platform ABI.** On every 64-bit host this is
invisible; on a 32-bit target (and this library ships to several) `size_t`
is 32 bits, and a foreign declaration hard-coding `uint64` for `frames`
corrupts the argument list. `ctypes.c_size_t` tracks the platform
automatically — the notebook uses it — but `cffi` and Julia users write
their own declarations, so the header says it explicitly. This is the kind
of sentence you only think to write after watching Part IV's 32-bit ports
in action.

**`srt_version()` is a probe.** It returns
`major*10000 + minor*100 + patch` — `100` for today's 0.1.0. A version
*macro* would vanish into the caller's compile; a version *function*
reports what the loaded shared library actually is, which is the question
an FFI user is really asking when their symbols don't match their
expectations. It is also the cheapest possible smoke test that the DSO
loaded and calls marshal correctly — one integer, no state, no handle.

## Six doubles and two return values: marshaling without a struct

Two smaller conventions in the surface reward a moment each, because both
are shaped by what FFIs do badly.

`srt_status` reports six quantities — state, ppm estimate, FIFO fill,
underruns, overruns, resyncs — and the obvious C design is a struct.
The shim instead fills a caller-provided `double out[6]`. A struct
returned across an FFI boundary is a *layout* contract: the foreign side
must re-declare every field, in order, with matching types, padding, and
alignment, and nothing checks the re-declaration — get it wrong and the
fields silently shear. An array of one scalar type is the
lowest-common-denominator marshaling that every FFI on earth handles in
one line (`(ctypes.c_double * 6)()` in the notebook). The price is that
counters and an enum ride in doubles — harmless, since a double carries
integers exactly to 2⁵³ and the header documents each slot by index. One
type, one array, zero layout risk: for six values polled a few times per
second, the trade is not close.

The push/pull return values encode the real-time contract from the ring
chapter, translated for callers who never read it. `srt_push` returns the
frames *accepted*, which may be fewer than offered — the clipped write
when the FIFO is full. `srt_pull` is deliberately asymmetric: it **always
fills** the requested frames, substituting silence while the converter is
still filling or after an underrun, and its return value reports how many
frames came from real input. An audio callback must hand *something* to
the DAC in bounded time; an API that could return "no data, try again"
would push retry logic — and the opportunity to get it wrong — into every
consumer. Silence-on-shortfall keeps the failure mode the library already
promised (a dropout sounds like silence, then a fade back in), and the
return value keeps it observable. FFI code that ignores both return values
still plays audio; FFI code that reads them gets telemetry. Both are valid
clients, and neither can deadlock or glitch the other side.

## Exceptions must not cross — and one target where they cannot even fly

Look again at `srt_create`'s body: the `new` is wrapped in
`try { ... } catch (...) { return nullptr; }`. This is not defensive
decoration. A C++ exception that propagates out of an `extern "C"`
function into a C caller is undefined behavior — there is no agreement
about what unwinding even *means* across that boundary, and the practical
result ranges from `std::terminate` to stack corruption inside a foreign
interpreter. The converter's constructor is the one place this library
throws (`Config` validation and allocation); the shim's job is to convert
that exception into the ABI's error vocabulary — `NULL` — before it
reaches the boundary. `catch (...)` rather than `catch (const
std::exception&)` because the boundary does not care *what* was thrown;
everything becomes `NULL`.

Now the hard lesson, recorded in `docs/PERFORMANCE.md` under *Known debt*.
One of this library's supported toolchains — the Hexagon static-musl
configuration from the Part IV DSP chapter — **cannot unwind at all**. Its
runtime lacks the unwinder: when a constructor throws, the exception does
not propagate to *any* catch block, anywhere; the process terminates via
`libc++abi`. This was not discovered by reading toolchain documentation.
It was discovered the day the first `EXPECT_THROW` test reached that CI
leg and the test *runner* died — the `ConfigValidation` suite is excluded
on Hexagon to this day, and the candidate fix
(`-unwindlib=libunwind` in the toolchain file) sits unclaimed in the debt
list.

Think through what that does to this shim's design. The `catch (...)` in
`srt_create` is *necessary* — on normal targets it is the entire error
mechanism — but on a no-unwind target it is **unreachable**: the throw
terminates the process before the catch can run. A caller on such a target
cannot be saved by any code positioned *after* the throw. The only
placement that works is *before* it: **validate, then construct.** The
deployment guidance in the debt entry says exactly this: on that
toolchain, treat an invalid `Config` as fatal and validate inputs *before*
constructing — check them against the constraints the constructor
enforces (positive finite sample rate, nonzero channels, band edges that
sum under the rate, and the rest of `validated()`'s list) so the
constructor is never asked to throw. It is a weaker mechanism than a
`catch`, and that is the point: it is the strongest mechanism the target
actually has.

The generalizable ABI lesson: an FFI boundary that reports failure by
*catching* is betting that every target can unwind, and that bet is not
safe even within one library's own CI matrix. Error strategies that
*return* — validate-before-construct, factory functions, status codes —
degrade gracefully on runtimes where error strategies that *throw* simply
end the process.

## The client: forty lines of ctypes

The notebook's first code cell is the reference consumer, and it exercises
every clause above: it locates the DSO (building it on first run), declares
each prototype, and wraps the handle in a small numpy-aware class. Two
lines carry the load:

```python
_lib.srt_create.restype = ctypes.c_void_p
_lib.srt_push.argtypes = [ctypes.c_void_p, _FLOATP, ctypes.c_size_t]
```

Without the explicit `restype`, `ctypes` assumes functions return a C
`int` — on a 64-bit machine the handle comes back truncated to its low 32
bits, and the crash lands on some *later* call, far from the actual
mistake. Declaring the full prototypes is the ctypes equivalent of
including the header, and `c_size_t` is the notebook honoring the width
caveat. The wrapper's `__del__` calls `srt_destroy` (guarded, per the
convention, against a handle that never existed), and its constructor
asserts `srt_create` succeeded — the check the null-tolerance exists to
forgive, present anyway, because tolerance is for accidents, not policy.
Everything downstream — the lock-acquisition plot, the ≥125 dB
transparency assertion, the impulse-response latency check that agrees
with `srt_designed_latency_seconds()` to within 0.3 ms — runs through
these eight functions.

## Why these ~90 lines look the way they do

| Decision | Alternative rejected | Reason |
|---|---|---|
| C shim over the C++ API | Python bindings / pybind11 | one C ABI serves ctypes, cffi, Julia, and everything else; bindings serve one language and drag in a build dependency |
| Float converter only | mirror all three sample types | the consumers are metrology notebooks; unused surface is unpaid-for contract |
| Named opaque handle | `void*` | keeps compiler type-checking alive at the FFI edge |
| Handle = object pointer, `reinterpret_cast` | handle registry / wrapper struct | there is nothing else to store; indirection would add state and failure modes |
| `impl()` overloads outside `extern "C"` | helpers inside the block | overloading is ill-formed with C linkage — the compiler enforced this one personally |
| `NULL` return + null-tolerant entry points | "caller must check" | the convention otherwise concentrates crashes on exactly the caller who forgot, in a runtime with no useful stack trace |
| `catch (...)` → `NULL` in `srt_create` | let exceptions cross | UB across the C boundary; and see below |
| Validate-before-construct guidance | rely on the `catch` | one supported toolchain cannot unwind at all — a throw terminates before any catch runs |
| `srt_version()` function | version macro | reports the loaded binary, not the caller's compile-time assumption |
| Thread affinity + `size_t` width in the header | "see the C++ docs" | the header is the only artifact an FFI consumer reads |

## Verify it yourself

```sh
# Build the shared library:
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSRT_BUILD_CAPI=ON
cmake --build build --target srt_capi -j

# The exported surface — eight srt_* symbols, unmangled, and nothing else
# from this file (the impl() helpers are invisible, as promised):
nm -D --defined-only build/tools/capi/libsrt_capi.so | grep srt_

# The one-integer smoke test (0.1.0 -> 100):
python3 -c "import ctypes; \
  print(ctypes.CDLL('build/tools/capi/libsrt_capi.so').srt_version())"

# The null-tolerance convention, exercised directly — no crash, zero frames:
python3 -c "import ctypes; lib = ctypes.CDLL('build/tools/capi/libsrt_capi.so'); \
  lib.srt_create.restype = ctypes.c_void_p; \
  print('bad create:', lib.srt_create(ctypes.c_double(-1.0), 0, 0, 1)); \
  print('push on NULL:', lib.srt_push(None, None, 128))"

# The full reference client, plots and assertions included:
jupyter nbconvert --to notebook --execute notebooks/asrc_demo.ipynb \
  --output /tmp/asrc_demo_run.ipynb

# Break it on purpose: move the two impl() overloads inside the extern "C"
# block and rebuild — the compiler rejects the overload set, which is the
# whole story of this file's structure in one diagnostic.
```

The second Python one-liner is the chapter's argument compressed: an
invalid configuration and a forgotten check, and the program prints two
zeros instead of dying.
