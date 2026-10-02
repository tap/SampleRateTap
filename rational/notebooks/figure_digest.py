# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Printed digests of every figure's plotted data, for A/B comparison.

A committed notebook's figures are PNGs, which a text comparison cannot
see and which are not byte-stable across matplotlib versions. So that a
changed curve still shows up as a changed *text* output, install() wraps
plt.show(): before each figure renders, it prints one line

    [ figure ] digest <16 hex digits> (<n> arrays)

hashing the data drawn on the figure (line data, collection offsets and
values, images, bar geometry), each array quantized to 9 significant
digits of its largest magnitude so last-bit floating-point noise does not
register. The digest is only meaningful against another run in the same
environment (requirements.lock pins it): the monorepo migration's notebook
gate re-executes a reference and a candidate tree in one job and compares
these lines.
"""
import hashlib

import matplotlib.pyplot as plt
import numpy as np


def _rounded(values) -> np.ndarray:
    # Quantize to 9 significant digits of the array's largest magnitude, so
    # noise far below the plotted scale (including tiny values next to an
    # exact zero) does not change the digest. Non-finite values pass through.
    a = np.asarray(np.ma.filled(np.ma.asarray(values, dtype=float), np.nan), dtype=float).ravel()
    finite = np.isfinite(a)
    out = a.copy()
    peak = np.max(np.abs(a[finite])) if finite.any() else 0.0
    if peak > 0.0:
        step = 10.0 ** (np.floor(np.log10(peak)) - 8)
        out[finite] = np.round(a[finite] / step) * step + 0.0  # + 0.0 folds -0.0
    return out


def _arrays(fig):
    for ax in fig.axes:
        for line in ax.get_lines():
            yield line.get_xydata()
        for coll in ax.collections:
            offsets = coll.get_offsets()
            if len(offsets):
                yield offsets
            values = coll.get_array()
            if values is not None:
                yield values
        for image in ax.get_images():
            yield image.get_array()
        for patch in ax.patches:
            yield np.asarray(patch.get_bbox().bounds)


def digest(fig):
    """Return (hex digest, number of arrays) for one figure."""
    h, n = hashlib.blake2b(digest_size=8), 0
    for values in _arrays(fig):
        h.update(_rounded(values).tobytes())
        n += 1
    return h.hexdigest(), n


def install():
    """Make plt.show() print a digest line for every open figure first."""
    if getattr(plt.show, "_figure_digest", False):
        return
    original = plt.show

    def show(*args, **kwargs):
        for num in plt.get_fignums():
            hexdigest, n = digest(plt.figure(num))
            print(f"[ figure ] digest {hexdigest} ({n} arrays)")
        return original(*args, **kwargs)

    show._figure_digest = True
    plt.show = show
