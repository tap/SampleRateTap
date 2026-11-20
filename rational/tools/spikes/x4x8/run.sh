#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0: runs every host program built by ./build.sh and writes its
# output beside its source (<dir>/<name>.txt), then the generator harness's
# modes (generator/<mode>.txt). PYTHON must have numpy (matrix.py needs it);
# default python3. The instruction counts (icount/) and the FFT timer
# (fft/) are run on their own: see ../README.md.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${here}/build"
py="${PYTHON:-python3}"

for dir in programs audit; do
    for src in "${here}/${dir}"/*.cpp; do
        name="$(basename "${src}" .cpp)"
        echo "run ${dir}/${name}"
        "${out}/${name}" > "${here}/${dir}/${name}.txt"
    done
done
"${out}/demo_var" 205 1 > "${here}/audit/demo_var_t_plus_1.txt" # 6.6's removal with T + 1
"${out}/brute" > "${here}/programs/brute.txt"
"${out}/x16" > "${here}/programs/x16.txt"
for name in recheck pertier rows48; do
    echo "run audit/${name}.py"
    "${py}" "${here}/audit/${name}.py" > "${here}/audit/${name}.txt"
done

for mode in diff pertier rows by beta; do
    echo "run generator/x0_matrix.py ${mode}"
    "${py}" "${here}/generator/x0_matrix.py" "${mode}" > "${here}/generator/${mode}.txt"
done
"${py}" "${here}/generator/x0_matrix.py" keys > "${out}/keys_all.txt"
: > "${here}/generator/margin_trick.txt"
for x in 0 1 2 3 4; do
    "${out}/margin_trick" "${x}" "${out}/pins_dA${x}.txt" < "${out}/keys_all.txt" >> "${here}/generator/margin_trick.txt"
done
