# The lock-free ring: `spsc_ring.h`

> Time is what keeps everything from happening at once.
>
> — Ray Cummings

Every other component in this library is mathematics. This one is physics.

The converter's whole purpose is to sit between two threads that must never
wait for each other: an audio capture callback pushing frames at its
device's pace, and a playback callback pulling frames at a *different*
device's pace. If either thread ever blocks — on a mutex, on an allocation,
on a priority-inverted anything — the audio glitches, and a glitch is the
one failure this library exists to prevent. So the channel between the
threads must be **lock-free**, and not in the loose marketing sense: every
operation must complete in a bounded number of steps regardless of what the
other thread is doing, including being suspended indefinitely at the worst
possible instruction.

The ring also serves a second master, and this is the design's quiet
novelty: its **occupancy is the control system's sensor**. The clock servo
(next chapter) estimates the rate mismatch between the two crystals
entirely from how full this buffer is. That is why the class exposes exact
`read_available()` and a consumer-side `discard()` — operations a generic
SPSC queue wouldn't bother with — and why "approximately full" isn't good
enough anywhere in this file: a biased occupancy reading would become a
biased frequency estimate.

Here is the entire contract:

```cpp
{{#include ../../../include/srt/spsc_ring.h:contract}}
```

Forty lines of comment and assertion before any logic. Three things deserve
attention already.

**`is_trivially_copyable_v<T>`** — the ring moves data with `memcpy`, in at
most two segments per transfer. This is a *bulk* ring: the producer hands
over whole blocks of interleaved frames, not elements one at a time. A
`memcpy`-based design rules out element types with constructors, and the
`static_assert` makes that a compile error instead of undefined behavior.

**`std::atomic<std::size_t>::is_always_lock_free`** — the class claims
lock-freedom, so it asserts the precondition. On every target this project
ships to, a `size_t` atomic compiles to plain loads and stores plus memory
ordering. But "every target this project ships to" is exactly the kind of
claim that rots silently; the assert costs nothing and converts rot into a
compile error. (This line has its own small history: it was added by an
audit that noticed the library asserted lock-freedom for its *telemetry*
counters but not for the indices the entire hot path rests on.)

**Indices are monotonic, not wrapped.** `head_` and `tail_` count every
element ever written and read, forever; only at the moment of buffer access
are they masked down to a position. This is the single most consequential
decision in the file, and it earns its own section below — including what
happens when "forever" meets a 32-bit `size_t`.

## The memory model, from the only direction that matters

There are two ways to teach C++ memory ordering. The textbook way starts
from the six `memory_order` enumerators and their formal guarantees. The
way that actually sticks starts from a bug.

Suppose both threads used `memory_order_relaxed` everywhere. The producer
writes 64 samples into the buffer, then advances `head_` by 64. The
consumer reads the new `head_`, concludes 64 samples are available, and
copies them out. On x86 this works every time you test it. On a Cortex-A
or M-class core — or under ThreadSanitizer — the consumer can observe the
*index* update **before** it observes the *sample data* the index claims to
cover, because nothing told either the compiler or the CPU that those
writes were related. The consumer then plays whatever stale bytes were in
the buffer. The bug is silent, rare, load-dependent, and absolutely real.

The fix is a single pairing, used twice, and it is the only synchronization
in the file:

> The producer **releases** `head_` after writing data; the consumer
> **acquires** `head_` before reading data. Everything the producer did
> before the release-store is visible to the consumer after the
> acquire-load that observes it.

Read the producer side with that lens:

```cpp
{{#include ../../../include/srt/spsc_ring.h:write}}
```

The two `memcpy` calls happen *before* the `release` store of the new head.
That ordering — data first, then the index that publishes it — is the
entire correctness argument for the data path. Symmetrically:

```cpp
{{#include ../../../include/srt/spsc_ring.h:read}}
```

The consumer `acquire`-loads `head_` (inside the cache-refresh branch,
discussed next), and only then copies data the head covers. Its own
`release` store of `tail_` plays the mirrored role for a subtler resource:
**buffer reuse**. The producer may overwrite a slot only after the consumer
has finished copying out of it; the consumer's release of `tail_` and the
producer's acquire of it order exactly that. Miss this second pairing and
you have a bug that no amount of staring at the "obvious" head-side pairing
will reveal.

Notice also what is *relaxed*: each side loads **its own** index with
`memory_order_relaxed`. The producer is the only writer of `head_`, so it
cannot race with itself; a thread always observes its own prior writes.
Using `acquire` there would be harmless but dishonest — ordering
annotations in this codebase are documentation, and claiming
synchronization where none is needed misleads the next reader. This is a
deliberate idiom: **the memory orderings are chosen to be exactly
sufficient, so that each one tells you why it exists.**

### What was rejected

A sequentially-consistent version (`memory_order_seq_cst` everywhere, the
default) would be correct. It was rejected for two reasons, in order of
importance: first, on ARM it compiles to strictly stronger barriers than
the algorithm needs, in the hottest loop the library owns; second — again
the documentation argument — `seq_cst` says "I didn't think about this,"
and in a file whose whole job is to be thought about, that is the wrong
message. A mutex-based version was never on the table: it would forfeit the
bounded-progress guarantee the audio contract requires, priority inversion
being the canonical way real-time audio dies.

## The cached-index trick

Correctness needs one acquire/release pair per direction. Performance is
about how *rarely* you can afford to do even that.

Every atomic load of the other thread's index is a potential cache-line
transfer between cores — the line bounces from the writer's L1 to the
reader's, hundreds of cycles when it goes badly, and it goes badly
precisely when both threads are busiest. The standard remedy (this design
follows the well-known pattern used by production SPSC queues) is for each
side to keep a **stale local copy** of the other side's index and consult
the real atomic only when the stale copy makes the operation look
impossible:

- The producer computes free space against `tailCache_`. Only if that says
  "not enough room" does it acquire-load the real `tail_` and retry the
  computation. If space *still* falls short, the answer is truthful — the
  buffer really is that full *right now* — and the write is clipped.
- The consumer does the same dance with `headCache_` for availability.

The asymmetry of staleness is safe by construction: a stale `tailCache_`
can only *underestimate* free space (the consumer only ever frees), and a
stale `headCache_` can only *underestimate* availability (the producer only
ever adds). Stale data makes the ring conservative, never wrong. In the
steady state the converter lives in — producer and consumer chasing each
other around a buffer that is never near full or empty — the fast path
touches **no foreign cache lines at all**: one relaxed load of your own
index, arithmetic against a plain local member, two `memcpy`s, one release
store.

The member layout enforces the same philosophy at the hardware level:

```cpp
{{#include ../../../include/srt/spsc_ring.h:layout}}
```

Producer-owned state (`head_`, `tailCache_`), consumer-owned state
(`tail_`, `headCache_`), and the shared read-only state (`buf_`, `mask_`)
each get their own 64-byte cache line, so neither side's writes invalidate
lines the other side reads in its fast path. The comment records a rejected
alternative worth pausing on:
`std::hardware_destructive_interference_size` is the standard's name for
exactly this constant, and this file deliberately doesn't use it. The
constant is **ABI-fragile** — its value can differ between translation
units compiled with different tuning flags, which is why GCC warns when you
use it in a header — and a header-only library lives entirely in that
danger zone. A plain `64` with a comment is less clever and more correct.
The general lesson recurs throughout this codebase: *between a standard
facility and a constraint you can state plainly, prefer the one whose
failure mode you can reason about.*

## Monotonic indices and the wraparound proof

Most ring buffers wrap their indices at the capacity and pay for it twice:
one slot must be wasted to distinguish full from empty, and every index
update needs a conditional wrap. This ring's indices instead run forever
and are masked (`idx = head & mask_`) only at access time, which is why the
capacity must be a power of two (`std::bit_ceil` in the constructor) — the
mask replaces a modulo, and the full capacity is usable because occupancy
is computed by subtraction, not by comparing wrapped positions.

The objection arrives immediately: *forever* is finite. On Hexagon and
Cortex-M, `size_t` is 32 bits; at 48 kHz stereo, the indices wrap every
twelve hours or so of continuous audio. What happens then?

Nothing — and the reason is worth proving rather than waving at, because
the proof is two lines of modular arithmetic that many engineers have
never consciously done. Unsigned arithmetic in C++ is arithmetic modulo
2^N. Occupancy is computed as `head - tail`; if the true (unbounded) counts
are H and T, the machine computes `(H mod 2^N) - (T mod 2^N) mod 2^N`,
which equals `(H - T) mod 2^N`. Since the algorithm guarantees
`0 ≤ H - T ≤ capacity` and capacity is at most 2^31 on a 32-bit target, the
true difference is always representable, so the modular result *is* the
true result — through the wrap, across the wrap, at the wrap. The masked
position is likewise exact: capacity divides 2^N (it's a power of two), so
`(H mod 2^N) & mask = H mod capacity`. The wrap is not an edge case the
code handles; it is a case the arithmetic never notices.

This was verified the trustworthy way as well: the audit that reviewed this
file ran the ring with indices initialized to `0xFFFFFFF8` and watched
transfers stride across the 2^32 boundary, byte-exact. The proof says it
must work; the test removes the possibility that the proof was about a
slightly different program than the one we shipped.

## What the tests can and cannot certify

Three layers of evidence back this file, and their *limits* are as
instructive as their coverage.

**Single-threaded exactness** (`tests/test_spsc_ring.cpp`): fill/drain
equality, wraparound data preservation, partial writes near full, discard
accounting. These pin the sequential semantics — necessary, and nowhere
near sufficient.

**A two-thread stress test** (`tests/test_spsc_ring_threads.cpp`): millions
of elements of a counting sequence pushed and popped with randomized chunk
sizes, verified in order on the consumer side, run under ThreadSanitizer in
CI. TSan observes the actual ordering annotations, so it would flag the
relaxed-everywhere bug described above as a data race.

**And the honest limitation**: a sanitizer can only judge the interleavings
the hardware deigns to produce during the run, and an x86 host barely
reorders anything. A memory-ordering bug can be invisible on x86 *and* pass
TSan there, then fire on a weakly-ordered ARM core in production. This
project's answer is a weekly CI job that runs the same TSan stress on
genuinely weakly-ordered arm64 hardware, plus the per-push macOS Apple
Silicon leg. That is also a limit worth naming: none of this *proves* the
algorithm; it raises the price of being wrong. The proof remains the
acquire/release argument above — which is exactly why this chapter spent
its pages on the argument rather than the test list.

## Why these ~130 lines look the way they do

A summary of the decisions, several of which recur throughout the library:

| Decision | Alternative rejected | Reason |
|---|---|---|
| Lock-free SPSC, two fixed roles | mutex; MPMC generality | bounded progress is the audio contract; generality costs exactly the cycles this file exists to save |
| Bulk `memcpy` transfers | element-at-a-time queue | the workload is blocks of frames; two `memcpy` segments beat N atomic handoffs |
| Exact occupancy + `discard()` | "approximate size is fine" | occupancy is the servo's sensor; bias here becomes frequency-estimate bias |
| Acquire/release, minimal | `seq_cst` everywhere | sufficiency-as-documentation; weaker barriers on ARM |
| Cached cross-indices | always load the atomic | steady-state fast path touches no foreign cache line |
| Monotonic masked indices | wrap-at-capacity | full capacity usable, no full/empty ambiguity; wrap is provably benign |
| `alignas(64)` literal | `hardware_destructive_interference_size` | the standard constant is ABI-fragile in headers; GCC warns for good reason |
| `static_assert` the preconditions | trust the porting engineer | rot becomes a compile error, not a field failure |

## Verify it yourself

```sh
# Sequential semantics, wraparound, discard accounting:
ctest --test-dir build -R spsc_ring --output-on-failure

# The two-thread counting-sequence stress (built when threads exist):
ctest --test-dir build -R TwoThreadStress --output-on-failure

# The same stress under ThreadSanitizer (as CI runs it):
cmake -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread" -DSRT_BUILD_EXAMPLES=OFF
cmake --build build-tsan -j && ctest --test-dir build-tsan -R spsc_ring

# Break it on purpose: change memory_order_release to relaxed in write(),
# rebuild the TSan variant, and watch the stress test report the race.
```

The last suggestion is the chapter in one line. The annotations are not
incantations; remove one and the tooling shows you precisely the disaster
it was holding back.
