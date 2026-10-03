# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""ctypes binding to the shipping rational C++ through its C ABI
(capi/tap_sr_rational_capi.h). Family convention: the notebooks measure the
real library, never a Python re-implementation. (Re)builds build_capi/ on
import: the build is incremental, and loading a library left over from an
older checkout would silently measure old code.

    from tap_sr_rational_py import Chain, Stage
    c = Chain("down_3_down_8_down_2", profile="economy")
    y = c.process(x)                    # float32 in, float32 out
    q = Chain("down_2", fmt="q15")      # int16 (Q0.15) in and out; "q31": int32
    s = Stage(2, 1, divisor=(147, 160))  # the up 2 after bridge in 48 -> 88.2
    c.latency_output_frames             # Fraction
    c.macs_per_output                   # Fraction

What is constructed is named, never looked up from a rate (D12): a chain is
a chain constant of the C ABI, a stage a ratio of the vocabulary at a design
divisor (PLAN.md 3.1).
"""
import ctypes
import pathlib
import subprocess
import sys
from fractions import Fraction

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build_capi"

CHAINS = [  # the TAP_SR_RATIONAL_* chain constants, in value order
    "up_2", "down_2", "up_3", "down_3", "ratio_3_2", "ratio_2_3", "ratio_4_3", "ratio_3_4",
    "up_2_up_2", "up_2_up_3", "up_2_ratio_4_3_up_3", "up_3_ratio_8_3", "up_2_up_6", "up_2_up_8",
    "up_2_up_6_up_2", "up_2_up_8_up_2", "up_2_up_8_up_3", "up_2_ratio_4_3", "ratio_3_4_down_2",
    "down_2_down_2", "down_3_down_2", "down_3_ratio_3_4_down_2", "ratio_3_8_down_3", "down_6_down_2",
    "down_8_down_2", "down_2_down_6_down_2", "down_2_down_8_down_2", "down_3_down_8_down_2",
]
PROFILES = {"economy": 0, "transparent": 1, "balanced": 2, "super_economy": 3}
FORMATS = {"float": 0, "q15": 1, "q31": 2}  # the TAP_SR_RATIONAL_FORMAT_* constants


def _lib_path():
    names = {
        "linux": "libtap_sr_rational_capi.so",
        "darwin": "libtap_sr_rational_capi.dylib",
        "win32": "tap_sr_rational_capi.dll",
    }
    for key, name in names.items():
        if sys.platform.startswith(key):
            return BUILD / name
    return BUILD / "libtap_sr_rational_capi.so"


def _run(cmd):
    # Quiet on success: the build log would otherwise land in the executed
    # notebook's outputs, where it varies with the machine and toolchain.
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        raise RuntimeError("command failed: " + " ".join(cmd))


def _build():
    _run(["cmake", "-S", str(ROOT / "capi"), "-B", str(BUILD), "-DCMAKE_BUILD_TYPE=Release"])
    _run(["cmake", "--build", str(BUILD), "-j"])


def _load():
    _build()
    lib = ctypes.CDLL(str(_lib_path()))
    vp, u64, sz = ctypes.c_void_p, ctypes.c_uint64, ctypes.c_size_t
    fp = ctypes.POINTER(ctypes.c_float)
    p16 = ctypes.POINTER(ctypes.c_int16)
    p32 = ctypes.POINTER(ctypes.c_int32)
    pu64 = ctypes.POINTER(ctypes.c_uint64)
    pun = ctypes.POINTER(ctypes.c_uint)
    sig = {
        "create": (vp, [ctypes.c_int, ctypes.c_int, ctypes.c_uint]),
        "create_stage": (vp, [ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32,
                              ctypes.c_uint]),
        "create_format": (vp, [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]),
        "create_stage_format": (vp, [ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32,
                                     ctypes.c_int, ctypes.c_uint]),
        "format": (ctypes.c_int, [vp]),
        "process_q15": (sz, [vp, p16, sz, p16]),
        "process_q31": (sz, [vp, p32, sz, p32]),
        "flush_q15": (sz, [vp, p16]),
        "flush_q31": (sz, [vp, p32]),
        "destroy": (None, [vp]),
        "ratio": (None, [vp, pun, pun]),
        "outputs_for": (u64, [vp, u64]),
        "frames_needed": (u64, [vp, u64]),
        "process": (sz, [vp, fp, sz, fp]),
        "flush": (sz, [vp, fp]),
        "flush_output_frames": (u64, [vp]),
        "reset": (None, [vp]),
        "latency_output_frames": (None, [vp, pu64, pu64]),
        "latency_seconds": (ctypes.c_double, [vp, ctypes.c_double]),
        "macs_per_output": (None, [vp, pu64, pu64]),
        "stages": (sz, [vp]),
        "stage_taps": (sz, [vp, sz]),
        "version": (ctypes.c_uint, []),
    }
    for name, (res, args) in sig.items():
        f = getattr(lib, "tap_sr_rational_" + name)
        f.restype = res
        f.argtypes = args
    return lib


_LIB = _load()


# Per format: numpy dtype, ctypes element, process and flush entry points.
_IO = {
    "float": ("float32", ctypes.c_float, "process", "flush"),
    "q15": ("int16", ctypes.c_int16, "process_q15", "flush_q15"),
    "q31": ("int32", ctypes.c_int32, "process_q31", "flush_q31"),
}


class _Converter:
    """The shared surface of a chain and a stage (interleaved frames in the
    converter's format: float32, or int16 / int32 for Q15 / Q31)."""

    def __init__(self, handle, channels, fmt="float"):
        import numpy as np  # local import keeps the binding numpy-optional

        if not handle:
            raise ValueError("tap_sr_rational create failed (invalid arguments)")
        self._np = np
        self._h = handle
        self._channels = channels
        self.fmt = fmt
        dtype, elem, proc, fl = _IO[fmt]
        self._dtype = np.dtype(dtype)
        self._ptr = ctypes.POINTER(elem)
        self._process = getattr(_LIB, "tap_sr_rational_" + proc)
        self._flush = getattr(_LIB, "tap_sr_rational_" + fl)

    def __del__(self):
        if getattr(self, "_h", None):
            _LIB.tap_sr_rational_destroy(self._h)
            self._h = None

    @property
    def ratio(self):
        l, m = ctypes.c_uint(), ctypes.c_uint()
        _LIB.tap_sr_rational_ratio(self._h, ctypes.byref(l), ctypes.byref(m))
        return Fraction(l.value, m.value)

    def _fraction(self, fn):
        n, d = ctypes.c_uint64(), ctypes.c_uint64()
        fn(self._h, ctypes.byref(n), ctypes.byref(d))
        return Fraction(n.value, d.value)

    @property
    def latency_output_frames(self):
        return self._fraction(_LIB.tap_sr_rational_latency_output_frames)

    @property
    def macs_per_output(self):
        return self._fraction(_LIB.tap_sr_rational_macs_per_output)

    def latency_seconds(self, out_rate_hz):
        return _LIB.tap_sr_rational_latency_seconds(self._h, out_rate_hz)

    @property
    def stage_taps(self):
        return [_LIB.tap_sr_rational_stage_taps(self._h, i) for i in range(_LIB.tap_sr_rational_stages(self._h))]

    def outputs_for(self, in_frames):
        return _LIB.tap_sr_rational_outputs_for(self._h, in_frames)

    def frames_needed(self, out_frames):
        return _LIB.tap_sr_rational_frames_needed(self._h, out_frames)

    def process(self, x):
        np = self._np
        x = np.ascontiguousarray(x, dtype=self._dtype)
        frames = len(x) // self._channels
        y = np.empty(int(self.outputs_for(frames)) * self._channels, self._dtype)
        made = self._process(self._h, x.ctypes.data_as(self._ptr), frames, y.ctypes.data_as(self._ptr))
        return y[: made * self._channels]

    def flush(self):
        np = self._np
        y = np.empty(int(_LIB.tap_sr_rational_flush_output_frames(self._h)) * self._channels, self._dtype)
        made = self._flush(self._h, y.ctypes.data_as(self._ptr))
        return y[: made * self._channels]

    def reset(self):
        _LIB.tap_sr_rational_reset(self._h)


class Chain(_Converter):
    """A named within-family chain (a chain constant of the C ABI)."""

    def __init__(self, name, profile="economy", channels=1, fmt="float"):
        super().__init__(
            _LIB.tap_sr_rational_create_format(CHAINS.index(name), PROFILES[profile], FORMATS[fmt], channels),
            channels,
            fmt,
        )
        self.name = name


class Stage(_Converter):
    """One stage at ratio L/M, designed at the profile relaxed by a divisor."""

    def __init__(self, L, M, profile="economy", divisor=(1, 1), channels=1, fmt="float"):
        n, d = divisor
        super().__init__(
            _LIB.tap_sr_rational_create_stage_format(L, M, PROFILES[profile], n, d, FORMATS[fmt], channels),
            channels,
            fmt,
        )


def version():
    v = _LIB.tap_sr_rational_version()
    return (v >> 16, (v >> 8) & 0xFF, v & 0xFF)
