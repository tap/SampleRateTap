/// @file spsc_ring.h
/// @brief Lock-free single-producer single-consumer ring buffer.
///
/// Bulk-transfer oriented (read/write move whole blocks in at most two memcpy
/// segments), never allocates after construction, and exposes an exact
/// consumer-side occupancy — which the ASRC servo uses as its phase detector.
/// Uses the cached cross-index technique (each side caches the other side's
/// index and refreshes it only when apparently full/empty) to minimize
/// cache-line ping-pong between the two threads.
// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

namespace srt {

    // ANCHOR: contract
    /// Lock-free SPSC ring buffer of trivially copyable elements.
    ///
    /// Thread contract: write() and writeAvailable() may only be called from the
    /// single producer thread; read(), readAvailable() and discard() only from the
    /// single consumer thread. Construction/destruction must not overlap either.
    /// Indices are monotonic and wrapped by a power-of-two mask, so the full
    /// capacity is usable. Their unsigned wraparound (at 2^32 on 32-bit
    /// targets) is benign: occupancy is always a difference of the two
    /// indices, exact while capacity < 2^(bits-1).
    template <typename T>
    class spsc_ring {
        static_assert(std::is_trivially_copyable_v<T>);
        // The lock-free claim of the whole audio path rests on these indices.
        static_assert(std::atomic<std::size_t>::is_always_lock_free);
        // ANCHOR_END: contract

      public:
        /// Allocates the buffer; capacity is rounded up to a power of two.
        explicit spsc_ring(std::size_t min_capacity)
            : m_buf(std::bit_ceil(std::max<std::size_t>(min_capacity, 2)))
            , m_mask(m_buf.size() - 1) {}

        spsc_ring(const spsc_ring&)            = delete;
        spsc_ring& operator=(const spsc_ring&) = delete;

        std::size_t capacity() const noexcept { return m_buf.size(); }

        // ANCHOR: write
        /// Producer: append up to n elements; returns the number actually written.
        std::size_t write(const T* src, std::size_t n) noexcept {
            const std::size_t head = m_head.load(std::memory_order_relaxed);
            std::size_t       free = capacity() - (head - m_tail_cache);
            if (free < n) {
                m_tail_cache = m_tail.load(std::memory_order_acquire);
                free         = capacity() - (head - m_tail_cache);
            }
            n = std::min(n, free);
            if (n != 0) {
                const std::size_t idx   = head & m_mask;
                const std::size_t first = std::min(n, capacity() - idx);
                std::memcpy(m_buf.data() + idx, src, first * sizeof(T));
                std::memcpy(m_buf.data(), src + first, (n - first) * sizeof(T));
                m_head.store(head + n, std::memory_order_release);
            }
            return n;
        }

        // ANCHOR_END: write

        /// Producer: exact free space at the time of the call.
        std::size_t write_available() noexcept {
            m_tail_cache = m_tail.load(std::memory_order_acquire);
            return capacity() - (m_head.load(std::memory_order_relaxed) - m_tail_cache);
        }

        // ANCHOR: read
        /// Consumer: remove up to n elements; returns the number actually read.
        std::size_t read(T* dst, std::size_t n) noexcept {
            const std::size_t tail  = m_tail.load(std::memory_order_relaxed);
            std::size_t       avail = m_head_cache - tail;
            if (avail < n) {
                m_head_cache = m_head.load(std::memory_order_acquire);
                avail        = m_head_cache - tail;
            }
            n = std::min(n, avail);
            if (n != 0) {
                const std::size_t idx   = tail & m_mask;
                const std::size_t first = std::min(n, capacity() - idx);
                std::memcpy(dst, m_buf.data() + idx, first * sizeof(T));
                std::memcpy(dst + first, m_buf.data(), (n - first) * sizeof(T));
                m_tail.store(tail + n, std::memory_order_release);
            }
            return n;
        }

        // ANCHOR_END: read

        /// Consumer: exact occupancy at the time of the call.
        std::size_t read_available() noexcept {
            m_head_cache = m_head.load(std::memory_order_acquire);
            return m_head_cache - m_tail.load(std::memory_order_relaxed);
        }

        /// Consumer: drop up to n elements without copying (hard resync path).
        /// Returns the number actually dropped.
        std::size_t discard(std::size_t n) noexcept {
            const std::size_t tail  = m_tail.load(std::memory_order_relaxed);
            std::size_t       avail = m_head_cache - tail;
            if (avail < n) {
                m_head_cache = m_head.load(std::memory_order_acquire);
                avail        = m_head_cache - tail;
            }
            n = std::min(n, avail);
            m_tail.store(tail + n, std::memory_order_release);
            return n;
        }

      private:
        // ANCHOR: layout
        // 64-byte separation to keep producer- and consumer-owned state on
        // distinct cache lines (std::hardware_destructive_interference_size is
        // deliberately avoided: it is ABI-fragile and warns on GCC). The
        // read-only members (buf_, mask_) lead so they share a line with each
        // other, not with either side's mutable state.
        static constexpr std::size_t k_cache_line = 64;

        std::vector<T> m_buf;
        std::size_t    m_mask;
        alignas(k_cache_line) std::atomic<std::size_t> m_head{0}; // written by producer
        alignas(k_cache_line) std::size_t m_tail_cache{0};        // producer's view of tail
        alignas(k_cache_line) std::atomic<std::size_t> m_tail{0}; // written by consumer
        alignas(k_cache_line) std::size_t m_head_cache{0};        // consumer's view of head
        // ANCHOR_END: layout
    };

} // namespace srt
