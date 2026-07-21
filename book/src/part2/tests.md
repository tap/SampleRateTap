# Tests as specifications

> Program testing can be used to show the presence of bugs, but never to show their absence!
>
> — Edsger W. Dijkstra

Part I ended each chapter with a list of tests. This chapter is about what
those tests actually *are* — because in this project they are not the usual
smoke detectors bolted on after the fact. They are the specification. The
README publishes a table of signal-to-noise figures; the reason that table
can be trusted is not editorial diligence, it is that every number in it has
a test asserting something just below it, and CI runs the assertion on every
push. `docs/PERFORMANCE.md` states the policy in one line: "The SNR table is
already enforced by test thresholds."

That sentence hides three design problems, each with a wrong answer that
most test suites pick by default. How tight do you pin a measured quantity?
How do you make a two-clock, two-thread, analog-flavored system produce the
same bits every run? And how do you measure 135 dB of fidelity without your
measuring instrument lying to you? The suite's answers are the subject of
this chapter.

## Thresholds a few dB under reality

Here is the convention, straight from the top of the quality suite
(`tests/test_asrc_quality.cpp`):

```cpp
// Thresholds sit 4-7 dB under measured performance (135/120/113/106 dB for
// balanced at 997/6k/12k/19.5k; 133/108 dB for transparent). The residual at
// high frequencies is dominated by the linear interpolation between adjacent
// phase-table rows, which falls ~12 dB per doubling of num_phases and rises
// ~12 dB per octave of signal frequency.
```

And a representative enforcement:

```cpp
TEST(AsrcQuality, Balanced997Hz) {
    EXPECT_GT(measure_snr_db(tap::samplerate::filter_spec::balanced(), 997.0), 128.0);
}
```

Measured 135.0, asserted 128.0. Consider the two alternatives this rejects.

**A loose threshold** — say, "SNR must exceed 60 dB, comfortably transparent
for casual listening" — turns the test into a tautology. The converter could
regress by seventy decibels, an *enormous* defect by this library's
standards, and CI would stay green while the README continued to advertise
135 dB. A loose threshold means the published claim and the enforced claim
are different claims, and only the weaker one is real. This suite's position
is that a quality number you publish is a number you gate, at very nearly
the value you publish.

**An exact threshold** — asserting 135.0 because you measured 135.0 — fails
for the opposite reason: the measurement is a physical quantity with
legitimate variation. Different hosts, compilers, and math libraries move
the residual by fractions of a dB; the float path's strict double
accumulation keeps outputs bit-stable per platform but not across them. The
4–7 dB of headroom is sized to absorb that variation and nothing else: any
*algorithmic* regression — a filter redesign that loses stopband, a servo
change that leaks more clock noise into the passband — costs whole decibels
and lands outside the slack.

The comment carries a second load worth noticing: it explains *where the
residual comes from* (phase-table interpolation, with its 12 dB scaling laws
in both `num_phases` and signal frequency). That converts the threshold table
from arbitrary constants into a checkable physical model — when the 16 kHz
suite was added later, its expectations could be *predicted* from the same
model (the residual depends on the normalized frequency f/fs, so tones at
the same f/fs should measure the same), then measured, and they matched
within about 1 dB (`tests/test_asrc_quality_16k.cpp` records both sets of
numbers). A threshold you can predict is a specification; a threshold you
can only observe is a snapshot.

The convention also imposes a maintenance discipline that deserves to be
stated honestly: when performance *improves*, the thresholds are stale and
must be re-pinned upward, or the enforcement quietly loosens. That happened
in this repository — the Q0.64 phase accumulator (Part III) improved the
997 Hz figure to 135.0 dB, and a subsequent documentation audit re-aligned
the published headline and threshold comments to the post-change reality.
The instruction-count ratchet in the next chapter solves the same
staleness problem mechanically, with a two-sided gate; the quality suite
solves it by convention and audit. The difference is instructive: ±3% on a
deterministic integer can be automated; "4–7 dB under a measurement that
legitimately varies by platform" still needs a human to re-pin.

## The two-clock simulator

Every quality number above comes from the same experimental rig, and it fits
in a page of header (`tests/support/two_clock_sim.h`). The problem it
solves: the converter's whole reason to exist is that *two independent
clocks* drive it, but tests that use two real threads and real timers are
nondeterministic — schedulers differ, load differs, and a 0.2 dB shift in a
measurement could be the code or could be the machine. For metrology you
want the clocks without the threads.

The rig is a struct of knobs:

```cpp
{{#include ../../../tests/support/two_clock_sim.h:pf_knobs}}
```

and one loop:

```cpp
{{#include ../../../tests/support/two_clock_sim.h:pf_run}}
```

This is discrete-event simulation reduced to its minimum. Two virtual
clocks, `t_in` and `t_out`, advance in *virtual time*: a producer event pushes
`chunk_in` frames and advances `t_in` by `chunk_in / fs_in`; a consumer event
pulls `chunk_out` frames and advances `t_out` by `chunk_out / fs_out`; whichever
clock is behind fires next. With `fs_in = 48 000 × (1 + 200 ppm)` and
`fs_out = 48 000`, the producer naturally lands one extra sample every 5 000
— the exact asynchrony a real capture/playback pair exhibits, with zero
dependence on the host scheduler. Runs are exactly reproducible: same
sequence of pushes and pulls, same occupancy trajectory seen by the servo,
same output samples, every time, on every machine.

Why determinism beats realism for regression work:

- **A failure is a coordinate, not a weather report.** When
  `Balanced19_5kHz` drops below 100 dB, re-running reproduces the identical
  run; you can bisect it, instrument it, and diff intermediate state against
  a good commit. A threads-and-timers failure reproduces "sometimes."
- **Thresholds can be tight.** The 4–7 dB convention above is only possible
  because run-to-run variance is zero; scheduler-dependent tests must budget
  slack for the scheduler, and that slack is exactly where regressions hide.
- **The interesting parameter becomes controllable.** Transfer granularity
  — how many frames move per event — is a *physical property of real
  deployments* (sample-synchronous codecs at one extreme, USB and network
  audio moving multi-frame bursts at the other), and it changes converter
  behavior: the servo promotes to its low-bandwidth Quiet stage only when
  occupancy is observed at fine granularity. The quality suites run
  `chunk_in = chunk_out = 1` to reach the Quiet stage; the multichannel short
  variants run `chunk = 8` deliberately, to certify the Track stage that
  block-fed deployments actually live in. In a real-threads test,
  granularity would be an accident of scheduling; here it is an axis of the
  test matrix.
- **Slow clock dynamics are testable at all.** `fs_in_scale` lets a test ramp
  the input rate — the lock suite sweeps drift ramps and asserts the servo
  follows without unlocking — which on real hardware would require a
  programmable oscillator and a lab.

What determinism deliberately does *not* cover is the one thing it removes:
real concurrency. The memory-ordering claims of the ring buffer are tested
by the separate two-thread stress under ThreadSanitizer (the
[ring chapter](../part1/spsc-ring.md) walks its limits). The division of
labor is explicit — realism where realism is the subject, simulation
everywhere else — and the technique travels: the same virtual-time
interleaving reappears in Python inside every notebook of the
[notebooks chapter](notebooks.md).

One number shows what the rig's determinism costs in patience rather than
trust. The quality runs last 40 virtual seconds because, as the test's
comment puts it, "the 0.05 Hz locked loop must fully forget the acquisition
transient before the measurement window" — and only the final second is
analyzed. At 16 kHz the servo bandwidths scale down with the rate, so the
same suite runs 120 seconds to cover the identical number of loop time
constants; its comment records that a 40 s run still sits ~15 dB above the
settled residual. Deterministic time is cheap; *skipping* settling time is
how you measure your transient instead of your converter.

## Sine-fit metrology

The simulator produces a signal; something must turn it into a decibel
figure, and at 135 dB the instrument is the hard part. The suite's
instrument (`tests/support/sine_analysis.h`) is a least-squares sine fit:
model the output window as `a·sin(ωi) + b·cos(ωi) + c`, solve the 3×3
normal equations for the best-fit fundamental, subtract it *exactly*, and
call everything that remains — harmonics, images, servo noise, quantization
— the residual. `snr_db()` is then the fitted fundamental's power over the
residual's.

Why a fit instead of an FFT? Because subtraction is exact and windows are
not. A windowed spectrum smears the near-full-scale fundamental across
neighboring bins at the window's sidelobe level; measuring a residual 135 dB
down *under* that skirt means fighting your own instrument. The fit has no
window: the fundamental is removed to the precision of the arithmetic
(double throughout), and the method's own floor sits far below anything the
converter produces. (The notebooks meet the same problem with the same
answer, plus a notch — that chapter tells the ~60 dB horror story that
motivates the extra guard.)

One refinement matters enough to justify its own function. `fit_sine`
requires the frequency; `fit_sine_tracked` *finds* it, starting from the
nominal value:

```cpp
    for (int iter = 0; iter < 4; ++iter) {
        const SineFit a = fit_sine(x.first(half), f);
        const SineFit b = fit_sine(x.subspan(half), f);
        // b.phase is relative to the second half's start; predict it from a.
        const double two_pi = 2.0 * std::numbers::pi;
        const double predicted = a.phase + two_pi * f * static_cast<double>(half);
        const double dphi = std::remainder(b.phase - predicted, two_pi);
        f += dphi / (two_pi * static_cast<double>(half));
    }
```

Fit each half of the window; if the assumed frequency is slightly wrong, the
second half's phase arrives shifted from where the first half's fit predicts
it; the shift, divided by the half-window's span, is the frequency error.
Four iterations converge far below the starting error.

The reason this exists is a property of the device under test. An ASRC's
rate estimate converges *asymptotically* — the Quiet-stage loop is
deliberately slow, so even after a 40-second run the estimate can sit a
fraction of a ppm off the true ratio. A rigid fit at the nominal frequency
would see the output tone microscopically detuned from the model and book
the mismatch as residual: a completely inaudible frequency offset, misread
as noise. Tracking the fundamental before measuring distortion is exactly
what commercial THD analyzers do, and the header's comment says so — the
test instrument follows metrology practice, not convenience.

But an instrument that *tracks* the signal could also *excuse* it: a
converter that genuinely played the wrong pitch would have its error
absorbed into the tracked frequency and measure clean. The suite closes
that hole with a guard on the tracker itself:

```cpp
    // The tracked frequency must still match the true clock ratio closely.
    EXPECT_NEAR(fit.freq_norm / nu_out_expected, 1.0, 2e-6);
```

The fit may refine the frequency, but only within 2 ppm of what the clock
ratio dictates — enough for servo convergence tails, nowhere near enough to
hide a real pitch error. Every use of the tracked fit carries this check.
It is the measurement-code version of a lesson this book keeps repeating:
whenever you give a tool freedom, pin the freedom.

## Crosstalk that cannot hide, leakage that cannot masquerade

Single-channel quality metrics are structurally blind to a whole class of
multichannel bugs: swap two channels in the deinterleave, or bleed a percent
of channel 3 into channel 4, and every per-channel SNR still measures
perfect. `tests/test_multichannel.cpp` exists for exactly those bugs, and
its design is a small case study in adversarial measurement.

The setup: one converter instance, every channel carrying a *distinct* tone
— `600 + 731·c` Hz, non-harmonically related, all inside the flat passband
for up to 16 channels — with per-channel phase offsets to decorrelate the
waveforms. After conversion across the usual +200 ppm crossing, each channel
must contain its own tone at full quality and nothing measurable of any
other channel's. The deployment shapes are real: 12 channels is 7.1.4
surround, 16 is an AVB stream bundling reference microphones with the
program feed.

The subtlety is in the analysis order, and the file header explains it:

```cpp
// Method: own tone is removed by tracked least-squares fit; the other
// channels' frequencies are then fitted on the residual, so the own tone's
// spectral leakage (about -67 dB at these spacings over a 1 s rectangular
// window) cannot masquerade as crosstalk. The fit noise floor on the
// residual is ~43 dB below the residual RMS, far under every threshold.
```

Fit channel *k*'s frequency directly on channel *c*'s raw signal and the
finite one-second window makes channel *c*'s own tone leak energy into that
fit at about −67 dB — the test would "detect" crosstalk at −67 dB on a
converter with none, capping the assertable threshold right there. Removing
the own tone first (exact subtraction of the tracked fit) drops the
masquerade floor to the fit noise on the residual, far under every
threshold. Order of operations *is* the instrument here: same data, same
fits, and only one sequencing yields a measurement capable of asserting
−100 dB. The pinned claims follow the quality suite's convention: crosstalk
below −100 dB per channel for float (−72 dB for Q15, whose own quantization
floor is the binding constraint), with amplitude and SNR checked alongside.

One more design decision hides in the channel counts of the short variants:

```cpp
// Channels 5 and 7 are the only counts that reach the channel-parallel
// K=2 and K=1 remainder tiles (8/4/2/1 tiling: 5 = 4+1, 7 = 4+2+1) — the
// audit found those tiles had zero coverage.
```

The C6 optimization (Part III) processes channels in register-blocked tiles
of 8, 4, 2, and 1. Testing 2, 12, and 16 channels — every *deployment*
shape — exercises only the wide tiles. Five and seven channels are useless
deployment shapes and ideal test shapes: they force the remainder paths. An
audit found those tiles had zero coverage across the entire suite; the fix
was not more assertions but better-chosen *inputs*. Coverage lives in the
test matrix, not the expectation count.

## The bare-metal one-shot, and the filter that needed a test

On the Cortex-M55 and M33 CI legs, the suite runs as a bare-metal kernel
under `qemu-system-arm`: no OS, no filesystem, no command line. That
environment breaks three assumptions ordinary gtest runs lean on, and
`tests/bare_metal_main.cpp` plus `tests/CMakeLists.txt` repair them one by
one — each repair with a story.

**No argv** means no `--gtest_filter` from the harness, so the
emulation-appropriate filter is baked into a custom `main()`:

```cpp
    ::testing::GTEST_FLAG(filter) = "-AsrcQuality*:AsrcLock.*:Servo.*:Kaiser.*MeetsSpec:"
                                    "FixedPoint.AsrcQuality*:"
                                    "FixedPoint.FullScaleSineDoesNotWrapQ15:"
                                    "MultiChannel.*:Feasibility.*:Reset.*";
```

**No reliable exit codes** — semihosting does not dependably propagate a
process status through the emulator — means the run is judged on text.
CTest watches for a sentinel:

```cmake
    add_test(NAME srt_tests_emulated COMMAND srt_tests)
    set_tests_properties(srt_tests_emulated PROPERTIES
        PASS_REGULAR_EXPRESSION "SRT_TESTS_COMPLETE rc=0"
        FAIL_REGULAR_EXPRESSION "\\[  FAILED  \\]"
        TIMEOUT 1800)
```

The sentinel is printed as the *last* act of `main()`, after
`RUN_ALL_TESTS()` returns — deliberately, so a crash after gtest's own
summary (a static destructor, a late fault) cannot register as a pass. The
`FAIL_REGULAR_EXPRESSION` is a second, independent tripwire: even if a
mangled run somehow emitted the sentinel, any visible test-failure line
still fails the CTest.

**Nobody watching** is the third broken assumption, and its repair has the
best history. `RUN_ALL_TESTS()` returns 0 when every selected test passes —
including when the filter selects *zero* tests. A typo in that baked-in
filter string would produce an empty run, print the sentinel with `rc=0`,
and turn the entire on-target suite green forever. An infrastructure audit
realized this, and the guard went in:

```cpp
    const int selected = ::testing::UnitTest::GetInstance()->test_to_run_count();
    if (selected < 15) {
        std::printf("only %d tests selected (expected >= 15): filter is broken\n", selected);
        std::printf("SRT_TESTS_COMPLETE rc=1\n");
        return 1;
    }
```

Two details show the care level. The count is checked *after* the run,
because gtest applies the filter inside `RUN_ALL_TESTS()` — read it before
and it is always zero, which was verified on target rather than assumed.
And the bound is 15 against a selection of roughly 20, leaving headroom for
legitimate test removals without masking a typo.

The guard was not paranoia; the filter had *already* had a real bug. When
the 16 kHz quality suite (`AsrcQuality16k`) was added, the exclusion then
read `-AsrcQuality.*` — and in gtest filter syntax, unlike regex, `.` is a
literal character. `AsrcQuality.*` matches `AsrcQuality.Balanced997Hz` but
not `AsrcQuality16k.Balanced333Hz`, so the new two-minute simulations would
have quietly joined every bare-metal CI run, at emulation speed. The fix
widened the pattern to `AsrcQuality*` (no dot). Look back at the filter
string and you can now read its dots as deliberate: `MultiChannel.*` —
*with* the literal dot — excludes exactly the `MultiChannel` suite while
keeping `MultiChannelShort` in, which the comment beside it calls out as the
only on-target coverage of the N-channel deinterleave and wide-MAC dot_row
paths. The same character is a bug in one line and a scalpel in the next;
the difference is whether its meaning was chosen.

## What the emulated targets deliberately skip

The baked filter and its `ctest -E` sibling on the Hexagon leg exclude the
same family: the quality suites, the lock and servo simulations, the filter
design verification, the feasibility and reset sims — collectively, as the
file header puts it, "minutes of soft-float virtual audio that validate
target-independent control math already covered on every host platform."
That phrase is the policy. A 40-second sample-granular quality run is cheap
arithmetic on a Xeon and an eternity under instruction-set emulation — and
it would re-prove something that *cannot differ* on the target: the servo's
control law and the filter designer's mathematics are pure functions of
their inputs, identical on every conforming C++ implementation.

What *can* differ on target — and therefore what the on-target run keeps —
is the datapath: kernel accuracy on the target's arithmetic, the fixed-point
paths (including the SMLALD dual-MAC route on M33-class cores), the ring
buffer, the deinterleave, the end-to-end latency path. The exclusion list is
not a shortcut; it is a claim about *where target-dependence lives*, and the
short multichannel variants exist precisely because that claim would
otherwise have left the N > 2 datapath uncovered on the machines it was
written for.

One exclusion is different in kind, and the CI file is honest about it:
`ConfigValidation` is skipped on Hexagon not because it is slow but because
that leg's static-musl toolchain cannot unwind — the constructor throws
correctly, `EXPECT_THROW` never catches, and libc++abi terminates. The
limitation is recorded in `docs/PERFORMANCE.md` under known debt, with the
deployment guidance it implies (validate configs before constructing on that
toolchain). A skipped test with a documented reason is a specification too:
it specifies the boundary of what the platform supports.

## Verify it yourself

```sh
# The quality suite: watch the printed [ measured ] lines clear the
# thresholds by the documented few dB:
ctest --test-dir build -R AsrcQuality --output-on-failure

# The threshold convention, in the tests' own words:
grep -n -A4 "Thresholds sit" tests/test_asrc_quality.cpp tests/test_asrc_quality_16k.cpp

# Multichannel independence, long and short (per-channel crosstalk prints):
ctest --test-dir build -R MultiChannel --output-on-failure

# Determinism of the rig: run a quality test twice and diff the output.
ctest --test-dir build -R Balanced997 --output-on-failure  # (run it twice)

# The bare-metal one-shot, exactly as CI runs it (needs arm-none-eabi-gcc
# and qemu-system-arm):
cmake -B build-m55 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake
cmake --build build-m55 -j && ctest --test-dir build-m55 -V

# Break the empty-run guard on purpose: change the baked filter in
# tests/bare_metal_main.cpp to a typo like "NoSuchSuite.*", rebuild, and
# watch the run fail with "filter is broken" instead of passing green.
```

The last experiment is this chapter's thesis in miniature. A test suite is
only a specification if an empty, wrong, or stale version of it *fails* —
and every mechanism in this chapter, from pinned thresholds to the
fifteen-test floor, exists to make silence impossible to mistake for
success.
