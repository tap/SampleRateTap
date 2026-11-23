# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""The measurements the recursive stages' notebooks and the book's Part VII
figure share (docs/IIR_PLAN.md I11): iir_comparison.ipynb, guard_band_demo.ipynb
and scripts/book_figures.py's guard-band figure all call this module, which runs
the shipping C++ through the C ABI (tap_sr_rational_py), never a Python
re-implementation. The one exception is labelled where it is made: the guard
band filter placed before the decimator, at the high rate, is no named setting
of the C ABI (named settings only), so reference_before() runs it as a scipy
design, as IIR-0's notebook did.

The rates are labels: 96 kHz in, 48 kHz out for the decimators (the guard band
filter at 48 kHz), 48 kHz in, 96 kHz out for the interpolators. At economy the
passband edge is 3/8 of the low rate (18 kHz), the guard band filter's stop edge
0.4530 of it (21.7 kHz, the order-7 Chebyshev II's), and the low Nyquist 24 kHz.
"""
import math

import numpy as np
from scipy import signal

import tap_sr_rational_py as rp

RATE_HIGH = 96000.0
RATE_LOW = 48000.0
RIPPLE_DB = 0.01  # the guard band filter's passband ripple

# The profiles' numbers (rational's profile, guard_band_settings): A dB and the
# passband edge as a fraction of the low rate; and the named guard band
# settings' Chebyshev II order behind each half-band kind.
PROFILES = {
    "super_economy": (70.0, 1.0 / 3.0),
    "economy": (70.0, 3.0 / 8.0),
    "balanced": (70.0, 19.0 / 48.0),
    "transparent": (120.0, 5.0 / 12.0),
}
GUARD_ORDER = {  # (profile, half-band kind) -> order (guard_band.h's table)
    ("super_economy", "nonlinear"): 5, ("super_economy", "fir"): 7, ("super_economy", "near_linear"): 7,
    ("economy", "nonlinear"): 7, ("economy", "fir"): 7, ("economy", "near_linear"): 7,
    ("balanced", "nonlinear"): 7, ("balanced", "fir"): 7, ("balanced", "near_linear"): 7,
    ("transparent", "nonlinear"): 7, ("transparent", "fir"): 11, ("transparent", "near_linear"): 11,
}
# The half-band variants: the converter each runs and the kind its guard band
# filter's setting is named for.
VARIANTS = {"FIR": "fir", "IIR nonlinear": "nonlinear", "IIR near-linear": "near_linear"}
FORMS = {"Chebyshev II": "nonlinear", "linear form": "linear"}


def stop_edge(profile="economy", kind="nonlinear"):
    """The guard band filter's stop edge, a fraction of the low rate: the
    order-n Chebyshev II's closed form, Os = Op cosh(acosh(D) / n), Op =
    tan(pi p), D^2 = (10^(A/10) - 1) / (10^(0.01/10) - 1) (Antoniou 2006; the
    same formula guard_band.h's design runs)."""
    a, p = PROFILES[profile]
    n = GUARD_ORDER[(profile, kind)]
    d = math.sqrt((10.0 ** (a / 10.0) - 1.0) / (10.0 ** (RIPPLE_DB / 10.0) - 1.0))
    return math.atan(math.tan(math.pi * p) * math.cosh(math.acosh(d) / n)) / math.pi


def half_band(variant, direction, profile="economy"):
    """The half-band converter by 2: the FIR Nyquist stage (a named chain) or
    a recursive stage in either variant."""
    if variant == "FIR":
        return rp.Chain("down2" if direction == "down" else "up2", profile=profile)
    return rp.IirStage(direction, 2, VARIANTS[variant], profile=profile)


def guard(form, variant, profile="economy"):
    """The guard band filter in a form, at the named setting behind the
    half-band variant it serves."""
    return rp.GuardBand(FORMS[form], VARIANTS[variant], profile=profile)


def configurations(direction="down", profile="economy"):
    """Every configuration by name: each half-band variant alone and with each
    guard band form, the filter after the decimator or before the
    interpolator (as a tap::dsp::chain composes them). Each value is the list
    of converters in signal order."""
    out = {}
    for variant in VARIANTS:
        out[f"{variant} alone"] = [half_band(variant, direction, profile)]
        for form in FORMS:
            hb, g = half_band(variant, direction, profile), guard(form, variant, profile)
            out[f"{variant} + {form}"] = [hb, g] if direction == "down" else [g, hb]
    return out


def run(parts, x):
    """Streams x through the converters in order (float32 through the C ABI),
    as a tap::dsp::chain would: each stage's every output into the next."""
    y = np.asarray(x, dtype=np.float32)
    for c in parts:
        c.reset()
        y = c.process(y)
    return y


def macs_per_output(parts, direction="down"):
    """Multiplies per output frame per channel, the C ABI's exact rationals:
    the guard band filter's at the low rate, over 2 going up."""
    total = 0.0
    for c in parts:
        m = float(c.macs_per_output)
        total += m / 2.0 if (direction == "up" and c.ratio == 1) else m
    return total


def group_delay(parts, f, direction="down"):
    """The configuration's group delay at f (cycles per output frame) in output
    frames: the parts' C ABI group delays converted to the output rate and
    summed (tap::dsp::chain's rule). Going up, the guard band filter runs at
    the input rate: its delay at 2 f, doubled."""
    total = 0.0
    for c in parts:
        if direction == "up" and c.ratio == 1:
            total += 2.0 * c.group_delay_output_frames(2.0 * f)
        else:
            total += c.group_delay_output_frames(f)
    return total


def nominal_latency(parts, direction="down"):
    """The configuration's nominal latency in output frames (I5)."""
    total = 0.0
    for c in parts:
        scale = 2.0 if (direction == "up" and c.ratio == 1) else 1.0
        total += scale * c.nominal_latency_output_frames
    return total


def exact_latency(parts, direction="down"):
    """The exact latency in output frames where every part has one, else None."""
    total = 0
    for c in parts:
        lat = c.latency_output_frames
        if lat is None:
            return None
        total += 2 * lat if (direction == "up" and c.ratio == 1) else lat
    return total


# ----------------------------------------------------------------------------
# Tones: the response, the aliases and the images, coherently sampled
# ----------------------------------------------------------------------------

N_FFT = 4096  # output frames analysed per tone
N_WARM = 4096  # output frames discarded first (the transients)


def _tone(cycles_per_frame, frames, amplitude=0.5):
    n = np.arange(frames)
    return (amplitude * np.cos(2.0 * np.pi * cycles_per_frame * n)).astype(np.float32)


def _bin_amplitude(y, k):
    """The amplitude of the output's bin k over its last N_FFT frames (a
    coherent tone: no window)."""
    seg = np.asarray(y[-N_FFT:], dtype=np.float64)
    return 2.0 * abs(np.dot(seg, np.exp(-2j * np.pi * k * np.arange(N_FFT) / N_FFT))) / N_FFT


def decimator_response(parts, m_values):
    """A decimator by 2's response to tones at m / (2 N_FFT) cycles per input
    frame (m = 1 ... N_FFT - 1, 0 to the input Nyquist): each tone's level at
    its (aliased) output bin over its input level, in dB. Above the low
    Nyquist this is the alias the decimator leaves."""
    out = []
    frames_in = 2 * (N_WARM + N_FFT)
    for m in m_values:
        x = _tone(m / (2.0 * N_FFT), frames_in)
        y = run(parts, x)
        k = m if m <= N_FFT // 2 else N_FFT - m
        out.append(20.0 * np.log10(max(_bin_amplitude(y, k) / 0.5, 1e-15)))
    return np.array(out)


def interpolator_response(parts, m_values):
    """An interpolator by 2's response to low-rate tones at m / N_FFT cycles
    per input frame (m = 1 ... N_FFT / 2 - 1): the passband level at output bin
    m and the image at N_FFT - m (of 2 N_FFT), each over the input level, in
    dB."""
    passband, image = [], []
    frames_in = N_WARM + N_FFT
    for m in m_values:
        x = _tone(m / N_FFT, frames_in)
        y = run(parts, x)
        seg = np.asarray(y[-2 * N_FFT:], dtype=np.float64)
        n = np.arange(2 * N_FFT)
        amp = lambda k: 2.0 * abs(np.dot(seg, np.exp(-2j * np.pi * k * n / (2 * N_FFT)))) / (2 * N_FFT)
        passband.append(20.0 * np.log10(max(amp(m) / 0.5, 1e-15)))
        image.append(20.0 * np.log10(max(amp(2 * N_FFT // 2 - m) / 0.5, 1e-15)))
    return np.array(passband), np.array(image)


# ----------------------------------------------------------------------------
# The sweep, its spectrograms and the worst-alias table (the demonstration)
# ----------------------------------------------------------------------------

SWEEP_SECONDS = 3.0
SWEEP_LEVEL = 0.5  # -6 dBFS


def sweep(rate=RATE_HIGH, seconds=SWEEP_SECONDS, f0=20.0, f1=None, level=SWEEP_LEVEL):
    """A logarithmic sine sweep from f0 to just below the rate's Nyquist at
    -6 dBFS, float32, and its instantaneous frequency in Hz."""
    f1 = 0.995 * rate / 2.0 if f1 is None else f1
    t = np.arange(int(rate * seconds)) / rate
    k = math.log(f1 / f0)
    x = level * np.sin(2.0 * np.pi * f0 * seconds / k * (np.exp(t / seconds * k) - 1.0))
    return x.astype(np.float32), f0 * np.exp(t / seconds * k)


def spectrogram(y, rate=RATE_LOW, nperseg=2048, noverlap=1536):
    """Frequencies (Hz), times (s) and power in dB re a full-scale sine
    (Blackman-Harris, the spectrum scaling: 0 dB is amplitude 1)."""
    f, t, s = signal.spectrogram(np.asarray(y, dtype=np.float64), fs=rate, window="blackmanharris",
                                 nperseg=nperseg, noverlap=noverlap, scaling="spectrum")
    return f, t, 10.0 * np.log10(np.maximum(2.0 * s, 1e-30))


def alias_table(y, x, inst, profile="economy", kind="nonlinear"):
    """The worst alias a decimator by 2 leaves of the sweep, in dB re the
    input, by where the alias lands: the passband (to p), the guard band (p to
    the guard band filter's stop edge) and above that edge (to the low
    Nyquist). In every 21 ms frame where the input is above the low Nyquist,
    the power summed over the band the alias crosses during that frame,
    against the input's summed over the band it crosses (both Blackman-Harris
    frames, so the scalings cancel): IIR-0's measure, on the shipping C++."""
    _, p = PROFILES[profile]
    pass_hz, stop_hz, nyq = p * RATE_LOW, stop_edge(profile, kind) * RATE_LOW, RATE_LOW / 2.0
    f, tt, s = signal.spectrogram(np.asarray(y, np.float64), fs=RATE_LOW, window="blackmanharris", nperseg=1024,
                                  noverlap=768, scaling="spectrum")
    fin, tin, sin = signal.spectrogram(np.asarray(x, np.float64), fs=RATE_HIGH, window="blackmanharris",
                                       nperseg=2048, noverlap=1536, scaling="spectrum")
    t = np.arange(len(x)) / RATE_HIGH
    df = f[1] - f[0]
    half = 512.0 / RATE_LOW
    bands = {"passband": [], "guard band": [], "above the stop edge": []}
    for j, tj in enumerate(tt):
        fa, fb = np.interp([tj - half, tj + half], t, inst)
        if fa <= nyq or fb >= 0.995 * RATE_HIGH / 2.0:
            continue
        lo, hi = RATE_LOW - fb - 3 * df, RATE_LOW - fa + 3 * df
        sel = (f >= lo) & (f <= hi)
        ref = (fin >= fa - 3 * df) & (fin <= fb + 3 * df)
        k = int(np.argmin(np.abs(tin - tj)))
        level = 10.0 * np.log10(max(s[sel, j].sum(), 1e-30) / sin[ref, k].sum())
        mid = RATE_LOW - 0.5 * (fa + fb)
        key = "passband" if mid <= pass_hz else "guard band" if mid < stop_hz else "above the stop edge"
        bands[key].append(level)
    return {k: (max(v) if v else float("nan")) for k, v in bands.items()}


def image_table(y, x_low, inst_low, profile="economy", kind="nonlinear"):
    """The worst image an interpolator by 2 leaves of a low-rate sweep, in dB
    re the input, by where its source sits (the passband, the guard band,
    above the stop edge): the power summed over the band the image (at the low
    rate minus the source) crosses in each 21 ms frame of the output, against
    the input's summed over the band the source crosses in the same span of
    the input (both Blackman-Harris frames, so the scalings cancel). Against
    the input, not the output's own source: a filter before the interpolator
    attenuates both."""
    _, p = PROFILES[profile]
    pass_hz, stop_hz = p * RATE_LOW, stop_edge(profile, kind) * RATE_LOW
    f, tt, s = signal.spectrogram(np.asarray(y, np.float64), fs=RATE_HIGH, window="blackmanharris", nperseg=2048,
                                  noverlap=1536, scaling="spectrum")
    fin, tin, sin = signal.spectrogram(np.asarray(x_low, np.float64), fs=RATE_LOW, window="blackmanharris",
                                       nperseg=1024, noverlap=768, scaling="spectrum")
    t = np.arange(len(inst_low)) / RATE_LOW
    df = f[1] - f[0]
    half = 1024.0 / RATE_HIGH
    bands = {"passband": [], "guard band": [], "above the stop edge": []}
    for j, tj in enumerate(tt):
        fa, fb = np.interp([tj - half, tj + half], t, inst_low)
        if fa < 100.0 or fb >= 0.995 * RATE_LOW / 2.0:
            continue
        ref = (fin >= fa - 3 * df) & (fin <= fb + 3 * df)
        img = (f >= RATE_LOW - fb - 3 * df) & (f <= RATE_LOW - fa + 3 * df)
        k = int(np.argmin(np.abs(tin - tj)))
        level = 10.0 * np.log10(max(s[img, j].sum(), 1e-30) / sin[ref, k].sum())
        mid = 0.5 * (fa + fb)
        key = "passband" if mid <= pass_hz else "guard band" if mid < stop_hz else "above the stop edge"
        bands[key].append(level)
    return {k: (max(v) if v else float("nan")) for k, v in bands.items()}


def reference_before(order, profile="economy"):
    """REFERENCE, not the shipping C++: a Chebyshev II at the high rate (scipy's
    signal.cheby2, second-order sections, run by sosfilt in double), the guard
    band filter placed before the decimator, which no named setting of the C
    ABI covers. Flat to the passband edge (18 kHz at economy), A dB from the
    stop edge its order sets at the high rate."""
    a, p = PROFILES[profile]
    p_high = p / 2.0
    d = math.sqrt((10.0 ** (a / 10.0) - 1.0) / (10.0 ** (RIPPLE_DB / 10.0) - 1.0))
    edge = math.atan(math.tan(math.pi * p_high) * math.cosh(math.acosh(d) / order)) / math.pi
    return signal.cheby2(order, a, 2.0 * edge, output="sos"), edge


def matching_order_before(profile="economy", kind="nonlinear"):
    """The lowest odd Chebyshev II order at the high rate whose stop edge is
    the low-rate filter's (the same band removed before the decimator as
    after it): scipy's cheb2ord on the high rate's grid."""
    a, p = PROFILES[profile]
    n, _ = signal.cheb2ord(p, stop_edge(profile, kind), RIPPLE_DB, a)
    return n + 1 - n % 2
