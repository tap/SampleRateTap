# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""ctypes bridge to the shipping RatioTap C++ through the C ABI
(capi/tap_sr_bridge_capi.h). Family convention: the notebooks measure the real
library, never a Python re-implementation. (Re)builds build_capi/ on import:
the build is incremental, and loading a library left over from an older
checkout would silently measure old code.

    from tap_sr_bridge_py import RatioConverter
    conv = RatioConverter(direction="down", profile="economy")
    y = conv.process(x)          # float32 in, float32 out
    tail = conv.flush()
    q = RatioConverter(direction="up", fmt="q15")   # int16 (Q0.15) in and out; "q31": int32
"""
import ctypes
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build_capi"


def _lib_path():
    names = {
        "linux": "libtap_sr_bridge_capi.so",
        "darwin": "libtap_sr_bridge_capi.dylib",
        "win32": "tap_sr_bridge_capi.dll",
    }
    for key, name in names.items():
        if sys.platform.startswith(key):
            return BUILD / name
    return BUILD / "libtap_sr_bridge_capi.so"


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
    path = _lib_path()
    lib = ctypes.CDLL(str(path))
    vp, u64, sz = ctypes.c_void_p, ctypes.c_uint64, ctypes.c_size_t
    fp = ctypes.POINTER(ctypes.c_float)
    p16 = ctypes.POINTER(ctypes.c_int16)
    p32 = ctypes.POINTER(ctypes.c_int32)
    sig = {
        "create": (vp, [ctypes.c_int, ctypes.c_int, ctypes.c_uint]),
        "create_format": (vp, [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]),
        "destroy": (None, [vp]),
        "format": (ctypes.c_int, [vp]),
        "outputs_for": (u64, [vp, u64]),
        "frames_needed": (u64, [vp, u64]),
        "process": (sz, [vp, fp, sz, fp]),
        "process_q15": (sz, [vp, p16, sz, p16]),
        "process_q31": (sz, [vp, p32, sz, p32]),
        "flush": (sz, [vp, fp]),
        "flush_q15": (sz, [vp, p16]),
        "flush_q31": (sz, [vp, p32]),
        "flush_output_frames": (u64, [vp]),
        "reset": (None, [vp]),
        "latency_input_frames": (ctypes.c_double, [vp]),
        "taps": (sz, [vp]),
    }
    for name, (res, args) in sig.items():
        f = getattr(lib, "tap_sr_bridge_" + name)
        f.restype = res
        f.argtypes = args
    lib.tap_sr_bridge_version.restype = ctypes.c_uint
    return lib


_LIB = _load()

_DIRS = {"up": 0, "down": 1}
_PROFILES = {"economy": 0, "transparent": 1, "balanced": 2, "super_economy": 3}
FORMATS = {"float": 0, "q15": 1, "q31": 2}  # the TAP_SR_BRIDGE_FORMAT_* constants

# Per format: numpy dtype, ctypes element, process and flush entry points.
_IO = {
    "float": ("float32", ctypes.c_float, "process", "flush"),
    "q15": ("int16", ctypes.c_int16, "process_q15", "flush_q15"),
    "q31": ("int32", ctypes.c_int32, "process_q31", "flush_q31"),
}


class RatioConverter:
    """One direction of the shipping converter (mono by default; interleaved
    frames in the converter's format: float32, or int16 / int32 for Q15 /
    Q31)."""

    def __init__(self, direction="down", profile="economy", channels=1, fmt="float"):
        import numpy as np  # local import keeps the bridge numpy-optional

        self._np = np
        self._channels = channels
        self.fmt = fmt
        dtype, elem, proc, fl = _IO[fmt]
        self._dtype = np.dtype(dtype)
        self._ptr = ctypes.POINTER(elem)
        self._process = getattr(_LIB, "tap_sr_bridge_" + proc)
        self._flush = getattr(_LIB, "tap_sr_bridge_" + fl)
        self._h = _LIB.tap_sr_bridge_create_format(_DIRS[direction], _PROFILES[profile], FORMATS[fmt], channels)
        if not self._h:
            raise ValueError("tap_sr_bridge_create_format failed")

    def __del__(self):
        if getattr(self, "_h", None):
            _LIB.tap_sr_bridge_destroy(self._h)
            self._h = None

    @property
    def latency_input_frames(self):
        return _LIB.tap_sr_bridge_latency_input_frames(self._h)

    @property
    def taps(self):
        return _LIB.tap_sr_bridge_taps(self._h)

    def outputs_for(self, in_frames):
        return _LIB.tap_sr_bridge_outputs_for(self._h, in_frames)

    def frames_needed(self, out_frames):
        return _LIB.tap_sr_bridge_frames_needed(self._h, out_frames)

    def process(self, x):
        np = self._np
        x = np.ascontiguousarray(x, dtype=self._dtype)
        frames = len(x) // self._channels
        y = np.empty(int(self.outputs_for(frames)) * self._channels, self._dtype)
        made = self._process(self._h, x.ctypes.data_as(self._ptr), frames, y.ctypes.data_as(self._ptr))
        return y[: made * self._channels]

    def flush(self):
        np = self._np
        y = np.empty(int(_LIB.tap_sr_bridge_flush_output_frames(self._h)) * self._channels, self._dtype)
        made = self._flush(self._h, y.ctypes.data_as(self._ptr))
        return y[: made * self._channels]

    def reset(self):
        _LIB.tap_sr_bridge_reset(self._h)


def version():
    v = _LIB.tap_sr_bridge_version()
    return (v >> 16, (v >> 8) & 0xFF, v & 0xFF)
