// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Forced include for the r8brain-free-src comparison workloads only
// (cmp_icount_r8b_*, docs/COMPARISON.md). r8brain guards its process-wide
// filter-design cache with std::mutex and offers no hook to replace it;
// newlib toolchains built without thread support (arm-none-eabi) declare no
// std::mutex, so r8brain does not compile bare-metal as shipped. The icount
// workload is single-threaded, so a no-op lock is behaviorally exact — it
// only removes the lock/unlock calls around the one-time filter-design cache
// lookups, never touching the per-sample path. Hosted toolchains (Hexagon's
// musl, x86) keep the real std::mutex: this header is a no-op there.
#pragma once

#include <mutex>

#if defined(__GLIBCXX__) && !defined(_GLIBCXX_HAS_GTHREADS)
namespace std {
    struct mutex {
        constexpr mutex() noexcept     = default;
        mutex(const mutex&)            = delete;
        mutex& operator=(const mutex&) = delete;
        void   lock() noexcept {}
        void   unlock() noexcept {}
    };
} // namespace std
#endif
