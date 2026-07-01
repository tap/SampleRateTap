# Notebooks as calibrated instruments

The previous two chapters covered claims a machine can gate: thresholds in
tests, instruction counts in a ratchet. But some of this project's most
consequential claims are not pass/fail propositions. *How much worse is a
naive FIFO?* *What does block size cost in latency and pitch stability?*
*How does the converter measure against libsamplerate, soxr, and two
hardware ASRC chips, under one definition of THD+N?* Answering those takes
plots, long simulated runs, and a measurement methodology that itself needs
defending — which is to say, it takes a lab notebook. The repository has
three, under `notebooks/`, and they are treated with the same severity as
the test suite: **committed with their outputs, calibrated before they
measure, and pinned with assertions so that a regression fails the re-run.**

This chapter is about that discipline — and about five specific ways a
quality measurement can lie, each of which this project actually hit, and
each of which is now encoded in the notebooks as a guard, a docstring, or a
scar.

## Three instruments, one method

**`asrc_demo.ipynb`** is the front door: it loads the library through its C
ABI with `ctypes` (no Python bindings, ~80 lines of wrapper), reproduces
the naive-FIFO disaster, then walks lock acquisition, transparency,
spectrograms, latency, drift tracking, and dropout recovery. Its committed
outputs are where the README's "what does it sound like" numbers come from:
clicks roughly ten times per second at 29 dB SNR for the naive path,
126.4 dB for the converter under the notebook's instrument.

**`asrc_block_size_study.ipynb`** answers a deployment question: what
happens at block sizes 32, 64, and 240 frames? Its committed conclusion —
Track-stage operation turns block quantization into cent-scale, low-rate FM
over a 53–61 dB wideband floor, while designed latency scales as roughly
`2·B/fs + 0.5 ms` — is quoted by `docs/COMPARISON.md` whenever coarse-block
operation comes up.

**`asrc_comparison.ipynb`** is the adversarial one: a single AES17-style
measurement implementation applied identically to SampleRateTap,
libsamplerate `sinc_best`, soxr `VHQ`, and a naive FIFO, with the deck
deliberately stacked *against* the home team — the libraries are handed the
exact clock ratio as an oracle, while the converter must discover it from
FIFO occupancy and still gets measured on the result. Every software number
in `docs/COMPARISON.md`'s tables is a committed output of this notebook.

All three share a spine: the deterministic two-clock simulation from the
[tests chapter](tests.md), re-implemented in a few lines of Python around
the C ABI. Producer and consumer events interleave by next-event virtual
time, so a +200 ppm producer delivers its extra sample every 5 000 exactly,
and a re-run reproduces the committed outputs. Determinism is what makes
"committed outputs" meaningful — a notebook whose numbers wander between
runs is a screenshot, not an instrument.

Why notebooks at all, rather than more tests? Because the *output* here is
the plot and the number-in-context, not a boolean; because the runs are
minutes long and belong in a manually-triggered lab rather than every CI
push; and because a reader deciding whether to trust the library should be
able to see the methodology, the code, and the result in one document —
then re-execute it. The committed outputs are the published lab record; the
re-run is the replication.

## Calibrate the instrument before believing it

The core discipline, stated as a rule: **no measurement function in these
notebooks reports on the converter until it has first reported on a
synthetic signal with a known answer, in the same notebook, above the
measurement.**

The comparison notebook builds an AES17-style THD+N meter — and then
immediately feeds it a pure 997 Hz tone plus white noise injected at
exactly −100 and −130 dBFS, computes what a perfect meter must read (the
injected level, corrected for the fundamental's RMS and the fraction of
white noise falling in the 20 Hz–20 kHz integration band), and asserts
agreement within half a dB:

```python
    got, f0 = thdn_db(sig, 997.0)
    ...
    assert abs(got - expect) < 0.5
print("instrument calibrated")
```

Only after `instrument calibrated` prints does any subject get measured.
The block-size study does the same for a subtler instrument — a
decomposition of a near-sinusoid into low-rate pitch modulation (in cents)
and a wideband noise floor — by synthesizing a tone with *exactly* 1 cent
of 10 Hz FM over a −120 dB noise floor. The committed output reads:

```text
calibration: peak 1.000 cents (true 1.000), rms 0.707 (true 0.707),
wideband 111.0 dB (true ~111)
```

That calibration cell carries the project's most candid admission, in its
own markdown: "This cell earned its keep: three earlier formulations of the
split each leaked modulation into the noise figure, and the calibration
caught every one." One of those three failures survives as a docstring on
the low-pass filter inside the decomposition — a boxcar smoother's passband
droop left a percent-level copy of the modulation in the high-passed
remainder, silently bounding the measurable floor — and another as a
warning that reconstruct-and-subtract in the signal domain fails subtly
(sub-split phase errors multiply the carrier). Without the synthetic-signal
check, every one of those buggy instruments would have produced a plausible
wrong number about the converter, and the notebook would have published it
with a straight face. Calibration converts "my measurement code is probably
right" into a demonstrated property, at the cost of one cell.

## Pin the result, or the notebook is a brochure

Every notebook ends its key measurements with `assert`. The demo, after
measuring transparency:

```python
assert snr_asrc > 125.0, "transparency regression"
```

The comparison, after the full table:

```python
first = names[0]
assert thdn[first] < -130 and dr24[first] > 130
```

The block-size study, after the FM decomposition — with a comment that
names the philosophy:

```python
    # Documented behavior as of this measurement: FM peaks stay below the
    # ~5-8 cent audibility region (B=240 gets closest) and the wideband
    # floor stays above 50 dB. These pin behavior, not aspiration.
    assert metrics[B]["cents_peak"] < 5.0, f"FM at B={B} reached audibility"
```

This is the notebook version of the test suite's
thresholds-just-under-measured convention. A notebook without assertions
degrades into marketing: it gets re-run after some future change, a plot
looks subtly worse, and nobody's eye catches it. With assertions, re-running
the notebook *is* a regression test — `docs/COMPARISON.md` says exactly
this in its caveats: software figures "regenerate by re-running the
comparison notebook; its assertions pin SampleRateTap's results so
regressions fail the run." The notebook is simultaneously the lab record
and the gate on its own claims.

The rest of this chapter is the honest-measurement traps those assertions
and calibrations exist to catch — each one a mistake this project made, or
nearly made, with the receipt still in the file.

## Trap one: your window lies about the floor

A 997 Hz fundamental at −1 dBFS sits some 130 dB above the residual being
measured. Take a plain FFT of that and the window function smears the
fundamental's energy across the spectrum at the window's sidelobe level —
with common windows, far above the thing you are trying to see. The
notebooks handle this on two fronts. For *display*, the demo's spectrum
helper documents its choice: a Kaiser window with β = 24, "sidelobes
~−190 dB, so a −130 dB noise floor is actually visible." For *measurement*,
no window is trusted at all: the comparison notebook refines the
fundamental's frequency by the phase-slope method (per-window phase of a
least-squares fit, regressed against time — "precision far beyond FFT bins,
which a 130 dB measurement needs," as its markdown puts it), then removes
the fundamental by a single global least-squares fit, *exactly*, before any
spectrum is taken. Only the residual — fundamental already subtracted —
meets an FFT, and then only for integration. A ±20 Hz notch around the
fundamental catches what the fit leaves; the notebook notes this notch is
far *narrower* than AES17 permits hardware testers, a conservatism that
works against the software subjects in every comparison.

This is the same decision the test suite made with its tracked sine fit,
arrived at for the same reason: at these dynamic ranges, subtraction is
exact and windows are not.

## Trap two: measure the converter, not its transient

An ASRC has stages. Fresh from a cold start it acquires; once locked it
tracks; given sample-granular occupancy data for long enough, the servo
promotes to its low-bandwidth Quiet stage — and the residual keeps
improving for tens of seconds as the loop forgets its own acquisition. A
measurement window placed too early reads the servo's history, not the
converter's quality.

The numbers make the point better than prose. The comparison notebook runs
32 seconds and discards the first 25 before analyzing ("we analyze its
output well after the servo's Quiet stage engages," its markdown says). The
48 kHz quality tests run 40 seconds and analyze the final one. And when the
16 kHz suite was built by scaling the servo bandwidths with the sample rate,
the settle time scaled *inversely*: the quiet loop lands at ~0.017 Hz, and
the suite had to run **120 seconds** — the same number of samples, the same
number of loop time constants as 40 s at 48 kHz — with the test's comment
recording that a 40-second run still sits ~15 dB above the settled
residual. Fifteen decibels is the difference between a correct claim and an
embarrassing one, controlled entirely by *when you look*.

The flip side matters equally: the block-size study measures the Track
stage *on purpose*, because block-fed deployments never reach Quiet — that
is the regime under study. Neither window placement is "right"; what is
right is that each notebook states which regime it is measuring and why.

## Trap three: the flush at the end of the stream

The comparison notebook hands each competitor the same input and analyzes a
window of its output. Where you cut that window turned out to matter more
than anything else in the file:

```python
def mid_window(y, analyze_s, guard_s=1.0):
    """Trim both ends: one-shot converters flush a filter tail at the end of
    the stream, and including it poisons the measurement by ~60 dB (found
    the hard way; a control experiment at 2:1 exposed it)."""
    y = np.asarray(y, dtype=np.float32)
    end = len(y) - int(guard_s * FS)
    return y[end - int(analyze_s * FS):end]
```

A one-shot resampler API, given the whole stream at once, drains its filter
state at the end — a tail of samples that are not steady-state conversion
output. Include that tail in the analysis window and the measured THD+N
degrades by roughly **60 dB**: enough to turn soxr's −150 dB into an
apparently mediocre converter. The bug was found "the hard way," and the
docstring preserves how: a control experiment at a 2:1 ratio — where the
correct answer was known independently — read absurdly wrong, and the
investigation traced it to the tail. Every one-shot subject is therefore
measured on a mid-stream window with a one-second guard at each end.

Note whose numbers this guard protects: the *competitors'*. An honest
comparison has to be most careful about errors that flatter the home team,
and an unguarded tail window would have been exactly that kind of error.

## Trap four: comparing float software to 24-bit silicon

The comparison's final tables land next to datasheet values for the AD1896
and SRC4392 — hardware ASRCs measured at their pins, which are 24 bits
wide. A float32 pipeline has no fixed noise floor at all (its noise scales
down with the signal), so its "native" dynamic range mostly measures the
arithmetic format, not the converter. Quoting float numbers against silicon
datasheets would be a category error dressed as a benchmark.

The notebook's equalizer is four lines:

```python
def q24(y):
    """Round to a 24-bit interface, undithered -- what a hardware ASRC
    presents at its pins. The equalizer that makes software and silicon
    numbers directly comparable."""
    return np.round(np.asarray(y, np.float64) * 8388608.0) / 8388608.0
```

Every subject's output is measured both ways, and `docs/COMPARISON.md`
leads with the 24-bit columns as the chip-comparable condition. The result
reads differently than bravado would: at that interface the oracle-fed
libraries measure at the 24-bit format ceiling itself (~−143.5 dB THD+N),
all three real converters share the identical 149.1 dB A-weighted
dynamic-range ceiling, and SampleRateTap's −132.1 dB sits ~11 dB behind the
oracles — a gap the document does not explain away but *prices*: it is the
measured cost of solving the clock-recovery half of the problem, which the
libraries do not attempt. Even so, the caveats refuse the flattering frame
in the other direction too: datasheet numbers come from analog test loops
with wider notches, and "a pristine-digital software measurement and a
bench measurement of a chip are comparable in definition, not in
environment."

## Trap five: the summary cell nobody executes

The last trap is the quietest, and this project walked into it. The demo
notebook's measurement cell printed, in its committed output:

```text
ASRC SNR: 126.4 dB   |   naive: 29.4 dB   |   improvement: 97 dB
```

with `assert snr_asrc > 125.0` enforcing it. The *summary table* at the
bottom of the same notebook claimed "SNR > 130 dB." Nothing failed. Nothing
could fail: markdown does not execute, so no assertion, calibration, or
re-run will ever check a number typed into prose. The two cells sat a few
screens apart, one measured and one remembered, disagreeing by 4 dB — the
one place a documentation audit found the repository overstating its own
results. (The measured 135 dB figure from the test suite is real, but it is
a *different instrument* — a tracked global fit over a different window —
and a summary must quote its own cell, not the best number available
elsewhere in the repo.) The fix was the boring, correct one: the summary
now states 126.4 dB and points at the assertion.

The lesson generalizes beyond notebooks: **summaries drift from cells the
same way READMEs drift from benchmarks and comments drift from code.**
Executable claims stay honest by execution; prose claims stay honest only
by audit. This project's response operates at both levels — push every
number it can into asserted, regenerated, machine-checked form (the test
thresholds, the icount table's regenerate-and-diff gate, the notebook
assertions), and schedule adversarial audits for the residue that only
prose can carry. This book is itself downstream of that lesson: the code
you read here is included live from the headers, because an author's
summary of code is just one more markdown cell.

## Verify it yourself

```sh
# Build the C ABI once; the notebooks find (or build) it themselves:
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSRT_BUILD_CAPI=ON
cmake --build build --target srt_capi -j

# Re-run each instrument end to end; any pinned regression fails the run
# (deps: numpy, matplotlib, plus samplerate and soxr for the comparison):
jupyter nbconvert --to notebook --execute notebooks/asrc_demo.ipynb
jupyter nbconvert --to notebook --execute notebooks/asrc_block_size_study.ipynb
jupyter nbconvert --to notebook --execute notebooks/asrc_comparison.ipynb

# Watch a calibration catch a broken instrument: in asrc_comparison.ipynb,
# widen the notch (notch_hz=20.0 -> 2000.0) or in the block-size study
# replace lowpass_fft with a boxcar mean — the synthetic-signal cell fails
# before any subject is measured.

# The traps, in the sources' own words:
grep -rn "poisons the measurement" notebooks/asrc_comparison.ipynb
grep -rn "earned its keep" notebooks/asrc_block_size_study.ipynb
grep -rn "pin behavior, not aspiration" notebooks/asrc_block_size_study.ipynb
```

The demo notebook's summary table is the one artifact in this chapter that
no command can verify — which is the point. Read it next to the measurement
cell above it, check that the numbers agree, and you will have performed,
by hand, the audit that fixed it.
