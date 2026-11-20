#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0: builds every host program of this spike against the tree's own
# headers (rational/include, submodules/dsptap/include) into build/, which
# the repository's .gitignore already ignores. Not part of any CMake build.
#   ./build.sh            # then ./run.sh to regenerate the committed outputs
# CXX (default g++) and JOBS (default 4) may be set. -O2 for the programs,
# -O3 -DNDEBUG (CMake's Release) for the FFT timer.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../../.." && pwd)"
out="${here}/build"
export X0_CXX="${CXX:-g++}"
export X0_INC="-I ${root}/rational/include -I ${root}/submodules/dsptap/include"
export X0_OUT="${out}"
jobs="${JOBS:-4}"
mkdir -p "${out}"

python3 "${here}/programs/gen_brute.py" "${out}/brute.cpp"
python3 "${here}/programs/gen16.py" "${out}/x16.cpp"

build_one() {
    local src="$1" name opt="-O2"
    name="$(basename "${src}" .cpp)"
    if [ "${name}" = "fft_cost" ]; then
        opt="-O3 -DNDEBUG"
    fi
    # shellcheck disable=SC2086 # the flags are word lists on purpose
    "${X0_CXX}" -std=c++20 ${opt} -pthread ${X0_INC} "${src}" -o "${X0_OUT}/${name}"
    echo "built ${name}"
}
export -f build_one

printf '%s\n' "${here}"/programs/*.cpp "${here}"/audit/*.cpp "${here}"/generator/*.cpp "${here}/fft/fft_cost.cpp" \
    "${out}/brute.cpp" "${out}/x16.cpp" | xargs -P "${jobs}" -I{} bash -c 'build_one "$1"' _ {}
