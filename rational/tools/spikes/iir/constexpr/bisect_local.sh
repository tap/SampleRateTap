#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# IIR-0: the smallest constexpr step (Clang) or operation (GCC) limit each
# design compiles under, by bisection, with the compilers on this host.
#   bisect_local.sh [g++-13] [clang++-18]
# A design that compiles under limit L and not L - 1 needed L steps. The
# CMake project beside this script (probe_steps.cmake) does the same on any
# host, MSVC included, through CMake alone.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
gxx="${1:-g++-13}"
clangxx="${2:-clang++-18}"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp:?}"' EXIT

compiles() { # compiler flagprefix which limit
    "$1" -std=c++20 -fsyntax-only "$2$4" -DIIR_CX_WHICH="$3" "$here/designs_main.cpp" > /dev/null 2>&1
}
bisect() { # compiler flagprefix which
    local lo=1 hi=1
    while ! compiles "$1" "$2" "$3" "$hi"; do
        lo=$hi
        hi=$((hi * 2))
        if [ "$hi" -gt 1073741824 ]; then echo "none"; return; fi
    done
    while [ $((hi - lo)) -gt 1 ]; do
        local mid=$(((lo + hi) / 2))
        if compiles "$1" "$2" "$3" "$mid"; then hi=$mid; else lo=$mid; fi
    done
    echo "$hi"
}
names=("" "half-band N17 (transparent)" "half-band N9 (economy)" "Chebyshev II order 11, L-inf over 256" \
       "Chebyshev II order 5, L-inf over 256" "Chebyshev II order 11, unity DC per section")
echo "| design | $clangxx steps (-fconstexpr-steps) | $gxx operations (-fconstexpr-ops-limit) |"
echo "|---|---:|---:|"
for w in 1 2 3 4 5; do
    c=$(bisect "$clangxx" "-fconstexpr-steps=" "$w")
    g=$(bisect "$gxx" "-fconstexpr-ops-limit=" "$w")
    echo "| ${names[$w]} | $c | $g |"
done
