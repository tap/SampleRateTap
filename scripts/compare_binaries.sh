#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# The comparison workloads compare.yml measures for one engine (the
# argument: async, bridge or rational), as build-tree-relative paths, one per
# line: the engine's comparison binaries at 2 s and 4 s (the difference is
# steady state, the remainder construction). The lists mirror the engines'
# bench/icount/CMakeLists.txt; a binary named here that CMake does not build
# fails the workflow loudly, which is the point.
case "${1:-}" in
    async)
        for e in srt_float srt_q15 lsr_medium lsr_best r8b_120 r8b_120_tb8 \
                 speex_fixed_q10 speex_float_q10 speex_fixed_q4 speex_float_q4; do
            echo "async/bench/icount/cmp_icount_$e"
            echo "async/bench/icount/cmp_icount_${e}_4s"
        done ;;
    bridge)
        for d in up down; do
            for e in srt_eco_float srt_eco_q15 srt_tr_float lsr_medium lsr_best r8b_eco r8b_tr \
                     speex_fixed_q3 speex_float_q3 speex_float_q9; do
                echo "bridge/bench/icount/cmp_bridge_icount_${d}_$e"
                echo "bridge/bench/icount/cmp_bridge_icount_${d}_${e}_4s"
            done
        done ;;
    rational)
        for r in up2 down2 r32 r23; do
            for e in srt_eco_float srt_eco_q15 srt_tr_float lsr_medium lsr_best r8b_eco r8b_tr \
                     speex_fixed_q3 speex_float_q2 speex_float_q9; do
                echo "rational/bench/icount/cmp_rational_icount_${r}_$e"
                echo "rational/bench/icount/cmp_rational_icount_${r}_${e}_4s"
            done
        done ;;
    *)
        echo "usage: $0 async|bridge|rational" >&2
        exit 2 ;;
esac
