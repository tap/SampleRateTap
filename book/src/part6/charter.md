# A ratio is a type: ratio.h

> Make illegal states unrepresentable.
>
> — Yaron Minsky

Part V ended with a decision rule and a confession. The rule: which
converter applies is a property of the *clock topology*, never inferred
from a ratio of two numbers. The confession: the synchronous half of the
family lived in another repository, so the book owed you the rule and
nothing else. Both halves have since moved in. The tree this book is built
from holds three engines under one namespace, `tap::sr`, and the rule now
has three types to choose between:

- `async` — two independent clocks, one nominal rate. Absorbs the *clock*.
  Parts 0–V.
- `bridge` — the fixed 44.1 ↔ 48 kHz pair (160/147 up, 147/160 down), one
  clock. Converts *the* number.
- `rational` — a small-factor L/M inside one rate family, one clock.
  Converts *a* number: 96 → 48, 48 → 32, 8 → 384.

This part reads `rational` the way Part I read the asynchronous converter:
header by header, in dependency order, with the code included live from
the tree. It is a smaller machine than the ASRC — six headers, no servo, no
ring, no second thread — and that smallness is the point of reading it:
every decision the ASRC had to make under the pressure of two clocks,
`rational` gets to make under the pressure of *only* speed, and you can
watch what changes.

The first header is the smallest and the most consequential. It contains
no arithmetic at all. It contains a refusal.

## What the engine refuses to do

The family's fourteen supported rates fall into two families: the 48 kHz
family (8, 12, 16, 24, 32, 48, 96, 192, 384 kHz: 8 kHz × 2ᵃ·3ᵇ with b ≤ 1)
and the 44.1 kHz family (11.025, 22.05, 44.1, 88.2, 176.4 kHz: 11.025 kHz ×
2ᵃ). Inside a family, every ratio between two rates reduces to 2ˣ·3ʸ with
x ∈ [−5, 5] and y ∈ {−1, 0, 1}. Between the families there is exactly one
irreducible step, 147/160, which is `bridge`'s and nobody else's.

So the engine's charter is one sentence: **L/M with L, M ∈ {2ᵃ·3ᵇ},
gcd(L, M) = 1, L ≠ M, as chains of Nyquist (L-th-band) stages.** Every
ratio a within-family pair needs is reachable from a vocabulary of fourteen
single stages — ↑2, ↓2, ↑3, ↓3, ↑6, ↓6, ↑8, ↓8 and the mixed 3/2, 2/3, 4/3,
3/4, 8/3, 3/8 — and what the vocabulary does not contain is as deliberate
as what it does:

- **No 4.** ↑4 and ↓4 are two half-band stages, never one 4th-band stage,
  because the half-band pair is cheaper (the next chapter has the
  arithmetic: 37 MACs per output against 55). `ratio<4, 1>` is a valid
  *ratio* — the chain by 4 is named by it — but no stage is shipped for it.
- **No 7.** 37.8 and 50.4 kHz are ratios of 7 against the family bases and
  are outside the charter by construction, not by a table entry.
- **No 1000/1001.** The pull-down rates are synchronous and not
  2ᵃ·3ᵇ-smooth. They also sit inside `async`'s ±1000 ppm capture range,
  which is exactly why the family plan says, in writing, that `async` must
  never be used to serve them: a ratio that is *known* is converted, not
  tracked.
- **No crossing.** A 44.1-family rate is reached from a 48-family rate
  only by a chain the caller writes through `bridge`. There is no path
  through this engine, and — the rule from Part V — no `(in_hz, out_hz)`
  lookup anywhere, in C++ or in the C ABI.

In the ASRC, the equivalent boundaries were runtime policy: a servo clamp,
a `validated()` gate, a thrown `invalid_argument`. Here they are not policy
at all. They are the type system.

## The charter is a `static_assert`

```cpp
{{#include ../../../rational/include/tap/sr/rational/ratio.h:rational_ratio}}
```

Three assertions and two constants: that is the whole definition of a
ratio. `is_two_three_smooth` divides out every 2 and every 3 and asks
whether 1 is left — a `constexpr` loop that the compiler runs at
instantiation. `std::gcd` is C++17's, and `L != M` rules out the identity,
which is not a conversion and would otherwise instantiate a perfectly
functional stage that copies its input through a one-tap filter.

Why a compile error rather than a constructor that throws? Three reasons,
in increasing order of weight.

First, *where the error lands*. `ratio<5, 1>` fails at the line that names
it, with the charter's own sentence in the diagnostic, in the caller's
translation unit, before anything links. A runtime throw lands in a log on
a device, after the firmware shipped, at the moment a configuration file
first contains a 5.

Second, *what the engine gets to assume*. Because L and M are template
arguments, every quantity derived from them is a compile-time fact: the
phase schedule of the next chapter is a `constexpr std::array` of exactly L
entries; the number of delay lines a decimator keeps is M; the loop that
walks a superblock has a trip count the compiler can see. The ASRC's
inner loop is parameterized by a 64-bit phase accumulator that changes
every sample; `rational`'s is parameterized by two small integers that
change never. Speed-first means spending that difference, and you can only
spend it if the ratio is a type.

Third — and this is the family rule wearing C++ clothes — *a type cannot be
computed from a rate*. There is no expression in C++ that takes two
`double`s at runtime and yields a `ratio<L, M>`; the only way to obtain one
is to write it. So the prohibition on routing by rate is not a review
checklist item. It is a property of the language.

The refusals are tested the only way a compile error can be tested:
`Ratio.ChartersFailToCompileWithTheMessage` configures a separate
`try_compile` project (`rational/tests/compile_fail/`) with the build's own
compiler — or its cross toolchain file, on the Cortex-M and Hexagon legs —
and asserts that `ratio<5, 1>`, `ratio<4, 2>` and `ratio<2, 2>` each fail
*and* that the diagnostic contains the charter's message. A `static_assert`
that fires for the wrong reason, or that a refactor accidentally weakened
into a warning, would fail this test.

## Everything the stage will need, as constants

A ratio knows two numbers. A stage needs a dozen facts derived from them,
and `ratio_traits` is where they are derived — once, at compile time, with
their own consistency check:

```cpp
{{#include ../../../rational/include/tap/sr/rational/ratio.h:rational_ratio_traits}}
```

Most of these are bookkeeping (the direction flags, the exponents of 2 and
3 that the chain factoring reads). One is the design decision the whole
engine turns on:

**`k_band = max(L, M)`.** A stage at ratio L/M runs one filter at the
*composite* rate L·f_in, with its cutoff at the lower rate's Nyquist. For
an interpolator by L, that filter is an L-th-band lowpass; for a decimator
by M, an M-th-band lowpass; for a mixed ratio it is a band-max(L, M)
lowpass driven by an L-phase polyphase schedule. The next chapter explains
why "L-th-band" buys so much; what matters here is the identity the traits
assert at the bottom of the struct: *for every ratio of the charter, the
composite rate is `k_band` times the lower rate.* Going up, the lower rate
is f_in and the composite rate is L·f_in = band·f_in. Going down by L/M,
the lower rate is (L/M)·f_in and the composite rate L·f_in = M·(L/M)·f_in =
band·lower. That is why one designer, taking "the band" and "the passband
as a fraction of the lower rate", serves every stage of the vocabulary
without a special case for direction — and why the assertion is there: it
is the proposition the design header silently relies on, so it is checked
where the numbers are born.

The `k_rate_ratio` double at the end is commented *informational (nothing
routes by it, D12)*. It exists so a test or a notebook can print "2/3 ≈
0.667"; it is a `constexpr double` precisely so nobody can be tempted to
compare it against a measured rate.

## The named vocabulary

The header closes with aliases — `up_2`, `down_2`, `up_3`, … `ratio_3_8` —
and nothing else. There is no `ratio_4_1` alias for a stage, because none
ships; there is no `ratio_5_1`, because it would not compile. The aliases
are the engine's public vocabulary, the C ABI's chain constants are spelled
from them (`TAP_SR_RATIONAL_RATIO_2_3`, `TAP_SR_RATIONAL_DOWN_3_DOWN_8_DOWN_2`),
and the coverage matrix two chapters from now is written entirely in them.

A reader coming from the ASRC chapters may miss the `config` struct, the
`for_sample_rate()` factory, the validation gate. There is no `config`
here. The ratio is a type, the profile (next chapter) is one of four named
values, and the channel count is the only runtime argument the constructor
takes. Everything the ASRC had to validate at runtime because it could be
wrong, `rational` cannot express.

## Why this header looks the way it does

| Decision | Alternative rejected | Reason |
|---|---|---|
| `ratio<L, M>` as a template type | runtime `(L, M)` pair, validated in the constructor | the charter is a compile error with the charter's message; every schedule and trip count becomes a compile-time constant; a type cannot be derived from a measured rate (D12 by construction) |
| L, M ∈ {2ᵃ·3ᵇ} | any coprime pair | the fourteen rates need exactly these; 7 (37.8 / 50.4 kHz) and 1001 are outside the family plan; `bridge` owns 147/160 |
| `k_band = max(L, M)`, one filter per stage | a separate interpolator and decimator per mixed ratio | one filter keeps the exhaustive-phase discipline per stage and the alignment contract one sentence long; the composite rate is band × lower rate for every ratio, asserted in the traits |
| No single 4th-band stage | `ratio<4, 1>` as its own stage | two half-bands cost 37 MACs per output against 55 (next chapter); the alias names the chain, not a stage |
| `k_rate_ratio` as `constexpr double`, documented informational | omit it | tests and notebooks print it; a constant cannot be compared against a measured clock by accident |
| Compile-fail test via `try_compile` | trust the `static_assert` | the diagnostic's text is part of the contract; the test runs with the cross toolchains too |

## Verify it yourself

```sh
# The charter, the traits, the vocabulary:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build -R 'rational\.Ratio\.' --output-on-failure

# The compile-fail test alone (it configures tests/compile_fail/ with this
# build's compiler and asserts the three rejections carry the message):
ctest --test-dir build -R ChartersFailToCompileWithTheMessage --output-on-failure

# Break it on purpose: write `tap::sr::rational::converter<tap::sr::rational::ratio<5, 1>>`
# anywhere in a test. It will not link, because it will not compile, and the
# first line of the diagnostic is the charter.
```
