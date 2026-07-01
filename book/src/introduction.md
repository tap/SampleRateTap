# Introduction

This book explains one piece of software completely.

The software is **SampleRateTap**, a header-only C++20 library that solves a
narrow, stubborn problem in real-time audio: two devices both claim to run at
48 kHz, but each owns its own crystal oscillator, so neither actually does.
One drifts a few parts per million against the other — imperceptibly slowly
and absolutely relentlessly — and any system that moves audio between them
must either resample adaptively or eventually glitch. The library converts
between two such clock domains transparently (about 135 dB of measured
fidelity), in real time (about 1.5 ms of latency), on hardware from Xeon
servers down to a $5 microcontroller.

That is a small enough problem to fit in your head and a deep enough one to
teach from. Solving it well demands working knowledge of half a dozen fields
that are usually taught separately: FIR filter design, fixed-point
arithmetic, control theory, lock-free concurrency, the C++ memory model,
SIMD micro-architecture, and the discipline of measuring instead of
guessing. The premise of this book is that you learn those subjects better
around one real, shipping artifact — where every design decision had to
survive contact with every other — than from isolated examples built to
illustrate exactly one thing.

## Who this is for

You are comfortable in C++ — templates, RAII, the standard library — but you
have not necessarily written audio code, used `std::memory_order_acquire` in
anger, designed a filter, or counted the instructions your compiler emits.
No DSP background is assumed; the mathematics is built up exactly as far as
the code needs it and no further. Where a result has a textbook derivation,
we cite the textbook and spend our pages on what the textbooks omit: why
*this* form of the equation, in *this* code, on *this* hardware.

## How this book stays honest

Two mechanical commitments distinguish this book from most code walkthroughs.

**The excerpts are live.** Every block of library code you read is included
into the book at build time from the actual header in the repository, by
anchor. If the code changes, the book changes or the book's build breaks —
in this project's continuous integration, like every other published number.
There is no possibility of the classic tutorial failure where prose
describes code that no longer exists.

**Every claim ends in a command.** The library's culture is that performance
and quality numbers are measured, gated, and regenerated — never asserted
from memory. The book inherits that: each chapter closes with a *Verify it
yourself* section listing the exact tests, benchmarks, or notebooks that
back what you just read. When this book says the ring buffer is correct
under weak memory ordering, you will be holding the ThreadSanitizer
invocation that fails if it is not.

## The history is the curriculum

This codebase was built measurement-first, and its history contains real
reversals, preserved deliberately:

- An optimization hypothesis about the Cortex-M55's floating-point unit that
  was **wrong**, discovered because a 1.4% instruction-count regression
  contradicted the project's own documentation — and the documentation, not
  the measurement, turned out to be at fault.
- A Hexagon vectorization effort that was implemented, proven bit-exact,
  measured at a 0.31% improvement — and then **deliberately deleted**, with
  the disassembly evidence recorded so nobody re-derives the dead end.
- A correctness bug that survived months of green CI because every test and
  benchmark happened to be configured just clear of it, found by an
  adversarial audit, and demonstrated before it was fixed.
- A toolchain that turned out to be unable to catch C++ exceptions at all —
  discovered the day the first `EXPECT_THROW` reached it.

These are not embarrassments to be edited out; they are the most valuable
material in the book. Anyone can present a finished design as if it were
inevitable. Watching a design *survive falsification* teaches you what the
finished form is actually load-bearing against.

## The shape of the book

**Part 0** establishes the problem and its budgets: why a plain FIFO
measurably fails (−34.7 dB!), what near-unity specialization buys, and the
arithmetic that connects picoseconds of timing jitter to decibels of
fidelity.

**Part I** is the heart: the library's seven headers, one chapter each, in
dependency order — filter design, the polyphase table, the sample-type
traits, the lock-free ring, the clock servo, the fractional resampler, and
the converter that composes them. Each chapter covers the algorithm, the
C++ idioms chosen *and rejected*, and the failure modes the design guards
against.

**Part II** explains the proof system: deterministic two-clock simulation,
sine-fit metrology, and the instruction-count ratchet that lets a CI runner
gate embedded performance to the exact instruction.

**Part III** retells the optimization campaign as it actually happened —
six efforts, four wins, one honest draw, one deliberate revert — with the
real numbers and the two implementation traps that cost a day each.

**Part IV** is portability: what a Qualcomm DSP, two bare-metal ARM cores,
and a C foreign-function interface each demanded.

**Part V** reaches hardware: real crystals, real cycle counters, and the
configuration rules that scale across channel counts and sample rates.

The appendices collect the C++ decision log (every idiom adopted or
rejected, with reasons), a glossary, and an annotated bibliography.

Chapters are largely self-contained, but Part I builds on itself; if you
read only one chapter, make it [the lock-free ring](part1/spsc-ring.md) —
it is short, complete, and representative of the whole book's method.
