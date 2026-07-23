"""ctypes bridge to the shipping RatioTap C++ through the C ABI
(tools/capi/ratio_capi.h). Family convention: the notebooks measure the real
library, never a Python re-implementation. Builds build_capi/ on first import.

    from ratiotap_py import RatioConverter
    conv = RatioConverter(direction="down", profile="economy")
    y = conv.process(x)          # float32 in, float32 out
    tail = conv.flush()
"""
import ctypes
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build_capi"


def _lib_path():
    names = {
        "linux": "libratio_capi.so",
        "darwin": "libratio_capi.dylib",
        "win32": "ratio_capi.dll",
    }
    for key, name in names.items():
        if sys.platform.startswith(key):
            return BUILD / name
    return BUILD / "libratio_capi.so"


def _build():
    subprocess.run(
        ["cmake", "-S", str(ROOT / "tools" / "capi"), "-B", str(BUILD), "-DCMAKE_BUILD_TYPE=Release"],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(BUILD), "-j"], check=True)


def _load():
    path = _lib_path()
    if not path.exists():
        _build()
    lib = ctypes.CDLL(str(path))
    lib.ratio_create.restype = ctypes.c_void_p
    lib.ratio_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint]
    lib.ratio_destroy.argtypes = [ctypes.c_void_p]
    lib.ratio_outputs_for.restype = ctypes.c_uint64
    lib.ratio_outputs_for.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.ratio_frames_needed.restype = ctypes.c_uint64
    lib.ratio_frames_needed.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.ratio_process.restype = ctypes.c_size_t
    lib.ratio_process.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_float),
    ]
    lib.ratio_flush.restype = ctypes.c_size_t
    lib.ratio_flush.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float)]
    lib.ratio_flush_output_frames.restype = ctypes.c_uint64
    lib.ratio_flush_output_frames.argtypes = [ctypes.c_void_p]
    lib.ratio_reset.argtypes = [ctypes.c_void_p]
    lib.ratio_latency_input_frames.restype = ctypes.c_double
    lib.ratio_latency_input_frames.argtypes = [ctypes.c_void_p]
    lib.ratio_taps.restype = ctypes.c_size_t
    lib.ratio_taps.argtypes = [ctypes.c_void_p]
    lib.ratio_version.restype = ctypes.c_uint
    return lib


_LIB = _load()

_DIRS = {"up": 0, "down": 1}
_PROFILES = {"economy": 0, "transparent": 1}


class RatioConverter:
    """One direction of the shipping converter (float, mono by default)."""

    def __init__(self, direction="down", profile="economy", channels=1):
        import numpy as np  # local import keeps the bridge numpy-optional

        self._np = np
        self._channels = channels
        self._h = _LIB.ratio_create(_DIRS[direction], _PROFILES[profile], channels)
        if not self._h:
            raise ValueError("ratio_create failed")

    def __del__(self):
        if getattr(self, "_h", None):
            _LIB.ratio_destroy(self._h)
            self._h = None

    @property
    def latency_input_frames(self):
        return _LIB.ratio_latency_input_frames(self._h)

    @property
    def taps(self):
        return _LIB.ratio_taps(self._h)

    def outputs_for(self, in_frames):
        return _LIB.ratio_outputs_for(self._h, in_frames)

    def frames_needed(self, out_frames):
        return _LIB.ratio_frames_needed(self._h, out_frames)

    def process(self, x):
        np = self._np
        x = np.ascontiguousarray(x, dtype=np.float32)
        frames = len(x) // self._channels
        y = np.empty(int(self.outputs_for(frames)) * self._channels, np.float32)
        made = _LIB.ratio_process(
            self._h,
            x.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
            frames,
            y.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
        )
        return y[: made * self._channels]

    def flush(self):
        np = self._np
        y = np.empty(int(_LIB.ratio_flush_output_frames(self._h)) * self._channels, np.float32)
        made = _LIB.ratio_flush(self._h, y.ctypes.data_as(ctypes.POINTER(ctypes.c_float)))
        return y[: made * self._channels]

    def reset(self):
        _LIB.ratio_reset(self._h)


def version():
    v = _LIB.ratio_version()
    return (v >> 16, (v >> 8) & 0xFF, v & 0xFF)
