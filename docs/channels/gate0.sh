#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# Gate 0 of docs/CHANNELS_PLAN.md (section 3): the reference (this tree's
# engines) and the variant tree (make_variant.py) measured in one job, on the
# host and on the M55, M33 and Hexagon, with and without exceptions and RTTI,
# plus the multi-count probe; compare.py applies the gate's rule.
#
# Toolchains as CI's icount-ratchet job installs them (.github/workflows/ci.yml):
# g++-13 and valgrind; gcc-arm-none-eabi and qemu-system-arm from Ubuntu 24.04;
# the pinned CodeLinaro Hexagon toolchain under ~/hexagon
# (scripts/fetch_hexagon_toolchain.sh); qemu-hexagon 8.2.2 built with
# --enable-plugins in ~/qemu-hexagon-plugins; the counting plugin built from
# tools/qemu_insn_plugin.
#
# Usage: docs/channels/gate0.sh <work dir> <plugin .so>
set -u
R=$(cd "$(dirname "$0")/../.." && pwd)
W=$(mkdir -p "$1" && cd "$1" && pwd)
P=$2
HEXBIN=$(dirname "$(find ~/hexagon -name 'hexagon-unknown-linux-musl-clang++' | head -1)")
export PATH="$HOME/qemu-hexagon-plugins:$HEXBIN:$PATH"
python3 "$R/docs/channels/make_variant.py" "$R" "$W/variant" || exit 1

measure() { # target tree name option
  local t=$1 T=$2 N=$3 opt=$4 d="$W/b-$1-$3-$4" tc
  rm -rf "${d:?}"
  if [ "$t" = host ]; then
    cmake -S "$T" -B "$d" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DTAP_SR_BUILD_TESTS=OFF \
      -DTAP_SR_BUILD_EXAMPLES=OFF -DTAP_SR_BUILD_ICOUNT_BENCH=ON -DTAP_SR_NO_EXCEPTIONS_RTTI="$opt" > "$d.cfg" 2>&1 || return 1
    cmake --build "$d" -j 4 --target $(cmake --build "$d" --target help | sed -n 's/^\(tap_sr_[a-z]*_hostcount_[a-z0-9_]*\):.*/\1/p') > "$d.build" 2>&1 || return 1
  else
    case $t in m55) tc=cmake/arm-cortex-m55-mps3.cmake ;; m33) tc=cmake/arm-cortex-m33-mps2.cmake ;; hexagon) tc=cmake/hexagon-linux-musl.cmake ;; esac
    cmake -S "$T" -B "$d" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$T/$tc" -DTAP_SR_BUILD_TESTS=OFF \
      -DTAP_SR_BUILD_EXAMPLES=OFF -DTAP_SR_BUILD_ICOUNT_BENCH=ON -DTAP_SR_NO_EXCEPTIONS_RTTI="$opt" > "$d.cfg" 2>&1 || return 1
    cmake --build "$d" -j 4 > "$d.build" 2>&1 || return 1
  fi
  (cd "$R" && for e in async bridge rational; do
    python3 scripts/icount.py --engine $e --target "$t" --build-dir "$d" --plugin "$P" --json-out "$d-$e.json" > "$d-$e.out" 2>&1
    echo "$t $N noexc=$opt $e: icount=$?"
    if [ "$opt" = OFF ] && [ "$t" != host ]; then
      cp "$e/bench/ceilings.json" "$d-$e.ceil.json"
      python3 scripts/ceilings.py measure --engine $e --target "$t" --build-dir "$d" --ceilings "$d-$e.ceil.json" --update > "$d-$e.ceil.out" 2>&1
      echo "$t $N $e: ceilings=$?"
    fi
  done)
}
for t in host m55 m33 hexagon; do
  for opt in OFF ON; do
    measure "$t" "$R" ref "$opt" || echo "$t ref $opt BUILD FAILED"
    measure "$t" "$W/variant" var "$opt" || echo "$t var $opt BUILD FAILED"
  done
done

out="$W/multicount.tsv"; : > "$out"
for t in m33 m55 hexagon; do
  case $t in m55) tc=cmake/arm-cortex-m55-mps3.cmake; sz=arm-none-eabi-size ;; m33) tc=cmake/arm-cortex-m33-mps2.cmake; sz=arm-none-eabi-size ;; hexagon) tc=cmake/hexagon-linux-musl.cmake; sz=llvm-size ;; esac
  for bt in Release MinSizeRel; do
    d="$W/mc-$t-$bt"; rm -rf "${d:?}"
    cmake -S "$R/docs/channels/multicount" -B "$d" -DCMAKE_BUILD_TYPE=$bt -DCMAKE_TOOLCHAIN_FILE="$R/$tc" -DTODAY="$R" -DVARIANT_TREE="$W/variant" -DVARIANT_API=prototype > "$d.cfg" 2>&1 &&
      cmake --build "$d" -j 4 > "$d.build" 2>&1 || { echo "multicount $t $bt FAILED"; continue; }
    for o in $(find "$d" -path '*/CMakeFiles/p_*.dir/*' \( -name '*.o' -o -name '*.obj' \) | sort); do
      printf "%s\t%s\t%s\t%s\n" "$t" "$bt" "$(basename "$(dirname "$o")" .dir)" \
        "$($sz -A "$o" | awk '$1 ~ /^\.text/ {s+=$2} END {print s}')" >> "$out"
    done
  done
done
# The placements (make_variant.py's switches), bridge and rational with
# exceptions on: where the per-count block is compiled.
for spec in "out:--bridge-walk out --rational-block out" "decimators:--bridge-walk out --rational-block decimators"; do
  n=${spec%%:*}
  python3 "$R/docs/channels/make_variant.py" "$R" "$W/tree-$n" ${spec#*:} || continue
  for t in m55 m33 hexagon; do
    case $t in m55) tc=cmake/arm-cortex-m55-mps3.cmake ;; m33) tc=cmake/arm-cortex-m33-mps2.cmake ;; hexagon) tc=cmake/hexagon-linux-musl.cmake ;; esac
    d="$W/x-$t-$n"; rm -rf "${d:?}"
    cmake -S "$W/tree-$n" -B "$d" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$W/tree-$n/$tc" -DTAP_SR_BUILD_TESTS=OFF \
      -DTAP_SR_BUILD_EXAMPLES=OFF -DTAP_SR_BUILD_ICOUNT_BENCH=ON > "$d.cfg" 2>&1 &&
      cmake --build "$d" -j 4 --target $(cmake --build "$d" --target help | sed -n 's/^\(tap_sr_\(bridge\|rational\)_\(icount\|callmax\)_[a-z0-9_]*\):.*/\1/p') > "$d.build" 2>&1 ||
      { echo "placement $n $t BUILD FAILED"; continue; }
    (cd "$R" && for e in bridge rational; do
      python3 scripts/icount.py --engine $e --target $t --build-dir "$d" --plugin "$P" --json-out "$d-$e.json" > "$d-$e.out" 2>&1
    done)
  done
done
python3 "$R/docs/channels/compare.py" "$W"
