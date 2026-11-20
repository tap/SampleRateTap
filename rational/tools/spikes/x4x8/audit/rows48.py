# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0: committed from the session scratchpad (audit/redteam/rows48.py); see ../README.md.
# Red-team scratch: the 1/48 and 48/1 rows and the x8 transparent cost under X3, from matrix.py.
import importlib.util
import pathlib  # spike X-0: matrix.py is found from this file
spec = importlib.util.spec_from_file_location("matrix", pathlib.Path(__file__).resolve().parents[3] / "coverage" / "matrix.py")
mx = importlib.util.module_from_spec(spec); spec.loader.exec_module(mx)
for admit in (False, True):
    mx.NO_SINGLE_STAGE = set() if admit else {4}
    for (s, d) in ((384000, 8000), (8000, 384000), (384000, 48000), (48000, 384000)):
        c, mn = mx.best_chain(s, d, "economy")
        mt, lt, _ = mx.cost_chain(c[3], s, d, "transparent")
        print("band4" if admit else "today", s, "->", d, mx.shape(c[3]), "eco", float(c[0]), "lat", float(c[1]),
              "| tr", float(mt), "| minimal", mx.shape(mn[3]), float(mn[0]))
