# Appendix C: Annotated bibliography

> If I have seen further it is by standing on the shoulders of giants.
>
> — Isaac Newton, letter to Robert Hooke

This project's provenance statement is short: all code implements
long-published methods, and no third-party source was copied. This
appendix lists those methods' sources — plus the tools and competitors the
measurements depend on — with a note on what the project *actually took*
from each. It deliberately cites nothing the codebase does not genuinely
draw on.

## Signal processing

**J. F. Kaiser, "Nonrecursive digital filter design using the I₀-sinh
window function," *Proc. IEEE Int. Symp. Circuits and Systems*, 1974.**
The origin of the Kaiser window and of the two empirical fits the library
evaluates verbatim in `include/srt/detail/kaiser.h`: stopband
attenuation → window shape parameter β, and the attenuation/transition-
width → filter-length estimate. The project took the closed forms exactly
as published — the value of the Kaiser window here is precisely that its
design procedure is a page of code with known error bounds, needing no
iterative optimization at construction time.

**f. harris, *Multirate Signal Processing for Communication Systems*,
Prentice Hall, 2004.** The standard reference for polyphase decomposition
— factoring one long prototype filter into L short branches indexed by
fractional delay — which is the structure of the library's coefficient
table. The tap-length estimate in `estimateTaps()` is the Kaiser/harris
formula in the form `N = (A − 8) / (2.285 · Δω)`, applied per polyphase
branch; the codebase credits both names, as the literature does.

**J. O. Smith, "Digital Audio Resampling Home Page" (and the *Bandlimited
Interpolation* material), CCRMA, Stanford University.** The theory the
datapath implements: resampling as evaluation of a windowed-sinc
interpolation kernel at fractional positions, with a finite table of
kernel phases and *linear interpolation between adjacent table entries*.
Smith's analysis of that last step is where the library's most-quoted
scaling law comes from — interpolation residue falling ~12 dB per doubling
of the table size L and rising with signal frequency — which Part 0 turns
into the budget arithmetic connecting L to decibels.

**Analog Devices, AD1896 datasheet ("192 kHz Stereo Asynchronous Sample
Rate Converter").** The architectural ancestor. The README describes the
library as "the classic commercial-ASRC architecture (AD1896-style
polyphase FIR + clock servo), specialized for the near-unity regime," and
the datasheet documents that architecture: a polyphase interpolation
filter addressed by a recovered rate ratio, with a FIFO between the clock
domains. It also supplies the hardware row in the comparison table —
quoted as datasheet values, with the caveats about measurement environment
stated in `docs/COMPARISON.md`.

**AES17, *AES standard method for digital audio engineering — Measurement
of digital audio equipment* (Audio Engineering Society).** The measurement
definition behind the headline quality numbers: remove the fundamental,
integrate the residual across the audio band for THD+N, measure dynamic
range at −60 dBFS with A-weighting. The comparison notebook implements an
AES17-style procedure (exact fit plus ±20 Hz notch, 20 Hz–20 kHz
integration) and calibrates it against synthetic signals before use — the
standard is what makes the −132 dB figure commensurable with silicon
datasheets rather than a house metric.

## The measured competitors

**libsamplerate (Secret Rabbit Code), E. de Castro Lopo —
documentation at libsndfile.github.io/libsamplerate.** The closest
architectural analog (streaming time-domain polyphase resampler) and one
of the two software subjects measured under identical conditions in
`docs/COMPARISON.md` and the comparison notebook. Its documentation also
supplied the honesty check the comparison repeats: the published "97 dB
worst case" figure applies to aggressive ratios, so near-unity results at
the format ceiling are its *easy* regime, not a contradiction.

**soxr (the SoX Resampler library) — github.com/chirlu/soxr.** The second
measured competitor, and the source of its own latency figure via
`soxr_delay()`. What the project took from soxr is mostly a boundary
lesson made quantitative: soxr wins raw host throughput decisively and
carries ~12–16 ms of latency doing it, which is the measured statement of
why a 1–2 ms live-monitoring budget needs a different design.

## C++

**Anthony Williams, *C++ Concurrency in Action*, 2nd ed., Manning, 2019.**
The working reference for the C++ memory model as this book teaches it:
acquire/release pairing as the establishment of happens-before, the
legitimacy of relaxed loads of data a thread itself owns, and lock-free
queue design generally. The ring chapter's proof style — argue the two
release/acquire pairs, then treat everything else as sequential code —
is the book's method applied to a hundred-line class.

**cppreference.com — in particular `std::memory_order`,
`std::atomic<T>::is_always_lock_free`, `std::bit_ceil`, and
`std::hardware_destructive_interference_size`.** The day-to-day authority
for the exact semantics the headers rely on: the ordering guarantees the
ring asserts, the compile-time lock-freedom predicate the audit added,
the power-of-two rounding used by the ring and the polyphase table, and —
for the interference-size constant — the documented ABI fragility that
justified *rejecting* the standard facility in favor of a literal `64`.

## Tooling

**mdBook — rust-lang.github.io/mdBook.** The tool this book is built
with. Its `\{{#include path:anchor}}` mechanism is what makes the book's
central honesty commitment mechanical rather than aspirational: code
excerpts are pulled from the real headers at build time, so prose that
drifts from the code breaks the build in CI instead of quietly lying.
