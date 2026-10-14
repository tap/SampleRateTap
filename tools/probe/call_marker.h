// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The per-call markers of the worst-single-call workloads (the roadmap's A6;
// <engine>/bench/icount/callmax_main.cpp). The counting plugin
// (tools/qemu_insn_plugin/insn_count.c) finds these two functions by symbol
// and reports the most instructions executed between a call to
// tap_sr_call_begin() and the next tap_sr_call_end(). Bench harness code,
// not shipped.
//
// They are extern "C" so the symbols are the plain names the plugin looks
// for, noinline so each is a real call the guest executes, and different
// (each stores its own value) so no identical-code folding can make them one
// address. Each is a store and a return, so the plugin's count of a bracketed
// call includes the end marker's few instructions, the same in every call.
#pragma once

#include <cstdint>

namespace tap::sr::probe::detail {
    inline volatile std::uint32_t g_call_marker = 0;
} // namespace tap::sr::probe::detail

extern "C" {
// NOLINTBEGIN(readability-identifier-naming)
[[gnu::noinline]] inline void tap_sr_call_begin() noexcept {
    tap::sr::probe::detail::g_call_marker = 1;
}
[[gnu::noinline]] inline void tap_sr_call_end() noexcept {
    tap::sr::probe::detail::g_call_marker = 2;
}
// NOLINTEND(readability-identifier-naming)
}
