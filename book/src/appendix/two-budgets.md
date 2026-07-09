# Appendix D: The two budgets

> We shape our tools, and thereafter our tools shape us.
>
> — John M. Culkin (often attributed to Marshall McLuhan)

*This appendix speaks in the first person: the surprise it describes was
mine. — T.*

## The sentence that stopped me

In the middle of the mailing-list exchange that became this book's
epilogue, Robert Bristow-Johnson mentioned, in passing, how he planned to
fix his filter-design script: "I can modify that by using an FFT (with a
really large size N = 2^20 = 1048576)."

I stopped on that sentence. I have been writing real-time audio software
for thirty years, and my honest, immediate reaction was: *I have never
heard of anyone running an FFT that large.* A million points. It sounded
like something you'd book cluster time for.

Here is what a million-point FFT actually costs, measured on the modest
shared virtual machine this book's figures are built on — not a good
machine; the point survives the handicap:

| size | points | time | input |
|---|---|---|---|
| 2²⁰ | 1,048,576 | 165 ms | 8.4 MB |
| 2²¹ | 2,097,152 | 339 ms | 16.8 MB |
| 2²⁴ | 16,777,216 | 3.4 s | 134 MB |

On a decent laptop with a tuned FFT library, divide by five or ten. The
"really large" FFT is a *sip of coffee*. And the embarrassing coda: this
project had been running 2²¹-point FFTs all along — every response
measurement in the verification notebook, every filter curve in the
book's figures. The huge FFT was in the room the whole time. I had
written the code that called it and never once registered its size as a
number that meant anything.

That gap — between what the number *sounded like* and what it *cost* —
is worth an appendix, because it isn't ignorance. It's training.

## Where the instinct comes from

I grew up, professionally, inside the perform loop. In the Max/MSP world
the signal vector is 64 samples long; at 44.1 kHz that is 1.45
milliseconds, and everything — every object, every patch, every product —
is priced in what it does to that deadline. You learn to feel a
per-sample multiply the way a carpenter feels the weight of a hammer. An
FFT, in that world, is not "an algorithm"; it is a *latency decision*: a
4096-point FFT means 93 milliseconds of buffering before the first
useful output, so you don't run one unless the musical idea demands it,
and you certainly never think about a size with seven digits, because
seven digits is twenty-two *seconds* of audio and no deadline that
matters is twenty-two seconds away. The thought "a million-point FFT" is
not rejected by that mindset. It simply never forms.

Thirty years of that is not a bias you notice. It is a currency you
think in. Every computation gets priced in the only unit that ever
mattered — time per sample, paid at the deadline — including, silently,
the computations that never go anywhere near a deadline.

Robert's vocabulary has the same shape with an older mint mark. He built
his instincts on SHArCs three decades ago, when a million points of
*memory* — eight megabytes of doubles — was the obstacle, never mind the
cycles. When he says "really large," he is speaking 1995 fluently. When
I hear "really large" and flinch, I am speaking perform-loop fluently.
We are both correct in a currency that no longer applies to the
transaction: his script runs once, offline, in MATLAB, on a laptop.

## The two budgets

So name the thing properly. Audio software lives under two budgets, and
they have almost nothing in common:

**The sample budget.** Paid per sample, at a deadline, forever. Miss it
once and everyone hears the click. This budget is hard in both senses —
unforgiving and difficult — and it deserves every instinct the last
thirty years installed: count the multiplies, fear the allocation,
distrust the branch. In this library the sample budget governs exactly
two functions, `push()` and `pull()`, and the numbers it runs to are
a couple hundred nanoseconds per stereo frame.

**The construction budget.** Paid once, off the deadline. Design a
filter, fit a compensation curve, solve a least-squares system, verify a
claim with a million-point FFT — none of it happens while anyone is
listening. Work here is not free, but it is priced in a different unit:
*is this fast enough to happen at startup, or in a design session,
without annoying a human?* Milliseconds are pennies. Whole seconds are
sometimes fine.

The line between the two budgets is drawn straight through this
library's API, and always was: the constructor may allocate, may throw,
may spend milliseconds on Bessel functions; `pull()` is `noexcept`,
lock-free, allocation-free, and answers to the deadline. Half of this
book has been about defending that line. What the FFT moment taught me
is that I had internalized the line for *code* but not for *thinking* —
my design-time reasoning was still spending sample-budget currency,
thirty years after it stopped being the right money.

## What "large" costs where

The two budgets don't just price the same work differently — they
sometimes invert it, and this project has measured both directions.

Robert's million-point FFT is the cheap direction. His design method
draws the ideal response on an N-point frequency grid and inverse-FFTs
it; N must be much larger than the filter so the grid is dense and the
circular wraparound has room to die out — his 2²⁰ over a 16,384-tap
filter is the standard 64× headroom, not extravagance. Cost: O(N log N),
about twenty million operations, a sip of coffee. Meanwhile, in the same
email, his `firls()` call "takes several seconds" and `firpm()` "gets
funky" at the same 16K taps — because dense least-squares design scales
like the *square* of the filter length and Remez exchange grows fragile
long before that. Offline is not one place. It has its own O(·)
geography, and the FFT lives in the flattest part of it.

The inversion runs the other way on small machines, and the
instruction-count ledger caught this project relying on the cheap-side
assumption. The compensated filter design costs roughly seven million
double-precision operations — nothing, I would have said, it's the
construction budget. Then the Cortex-M33 leg of CI priced it: on a
target where every double multiply is a ~140-instruction library call,
"nothing" measured **1.9 billion instructions** — seconds of boot on a
part that might be inside a product with a power button. The design got
resized (one correction pass, fewer probes) to fit a budget I hadn't
known it was spending against. Same work, three targets, three prices:
16 million instructions on the M55, 131 million on Hexagon, 929 million
on the M33. "Construction-time" names *when* the money is spent, not
*how much there is*.

And the cosine-series trick at the heart of the compensated design is,
seen this way, a currency exchange. Robert pays for his passband tilt
with a million-point FFT, because MATLAB makes FFTs free and his script
runs on a laptop. This library pays for the identical tilt with a
handful of shifted sinc evaluations and a 15-unknown least-squares
solve, because a dependency-free C++ header makes FFTs expensive and its
constructor sometimes runs on a microcontroller. Two implementations of
one filter, each shaped by which budget its environment discounts.

## What the instinct is still for

It would be the wrong reading of this appendix to conclude that the
old reflexes are obsolete. The sample budget is exactly as merciless as
it was in 1995; the perform loop still doesn't care about your feelings;
every instinct that flinches at an allocation inside `pull()` is earning
its keep tonight in somebody's live rig. The reflexes are not wrong.
They are *scoped*, and the scope is the deadline.

The skill I apparently still get to learn, thirty years in, is noticing
which budget I'm standing in before I price the work. A million-point
FFT at the deadline is absurd. The same FFT in a design script is a
rounding error, and treating it as absurd there has real costs — it
makes you avoid the honest tool, under-verify the finished design, and
mistake an expert's era-stamped vocabulary ("really large") for a
warning when it was just an old accent, faithfully kept.

The samples still have to arrive on time. Everything else has all the
time in the world.

## Verify it yourself

```sh
# The table above, on your machine (any Python with numpy):
python3 -c "
import numpy as np, time
for p in (20, 21, 24):
    x = np.random.default_rng(1).standard_normal(1 << p)
    t0 = time.perf_counter(); np.fft.rfft(x); t1 = time.perf_counter()
    print(f'2^{p}: {1e3*(t1-t0):8.1f} ms  {x.nbytes/1e6:7.1f} MB')"

# The million-point-class FFTs this book was already running: every
# spec/response measurement in the verification notebook uses rfft at 2^21.
grep -n '1 << 21' notebooks/asrc_rbj_analysis.ipynb scripts/book_figures.py

# The two budgets, drawn through the API: the constructor may throw and
# allocate; the audio path may not. The real-time contract is a grep away:
grep -n 'noexcept' include/srt/asrc.h | head

# What the construction budget costs where it is NOT cheap: the M33 entry
# in the instruction-count ledger.
grep -n 'M33' docs/PERFORMANCE.md | head
```

Wall-clock caveat, as always in this book: the timings are one machine's
and the table's job is the order of magnitude, not the digits. That is
the entire point.
