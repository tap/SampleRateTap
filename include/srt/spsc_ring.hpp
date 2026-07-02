/// \file spsc_ring.hpp
/// \brief Lock-free single-producer single-consumer ring buffer.
///
/// Bulk-transfer oriented (read/write move whole blocks in at most two memcpy
/// segments), never allocates after construction, and exposes an exact
/// consumer-side occupancy — which the ASRC servo uses as its phase detector.
/// Uses the cached cross-index technique (each side caches the other side's
/// index and refreshes it only when apparently full/empty) to minimize
/// cache-line ping-pong between the two threads.
#ifndef SRT_SPSC_RING_HPP
#define SRT_SPSC_RING_HPP

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
class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>);
    // The lock-free claim of the whole audio path rests on these indices.
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    // ANCHOR_END: contract

public:
    /// Allocates the buffer; capacity is rounded up to a power of two.
    explicit SpscRing(std::size_t minCapacity)
        : buf_(std::bit_ceil(std::max<std::size_t>(minCapacity, 2))), mask_(buf_.size() - 1) {}

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    std::size_t capacity() const noexcept { return buf_.size(); }

    // ANCHOR: write
    /// Producer: append up to n elements; returns the number actually written.
    std::size_t write(const T* src, std::size_t n) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t free = capacity() - (head - tailCache_);
        if (free < n) {
            tailCache_ = tail_.load(std::memory_order_acquire);
            free = capacity() - (head - tailCache_);
        }
        n = std::min(n, free);
        if (n != 0) {
            const std::size_t idx = head & mask_;
            const std::size_t first = std::min(n, capacity() - idx);
            std::memcpy(buf_.data() + idx, src, first * sizeof(T));
            std::memcpy(buf_.data(), src + first, (n - first) * sizeof(T));
            head_.store(head + n, std::memory_order_release);
        }
        return n;
    }

    // ANCHOR_END: write

    /// Producer: exact free space at the time of the call.
    std::size_t writeAvailable() noexcept {
        tailCache_ = tail_.load(std::memory_order_acquire);
        return capacity() - (head_.load(std::memory_order_relaxed) - tailCache_);
    }

    // ANCHOR: read
    /// Consumer: remove up to n elements; returns the number actually read.
    std::size_t read(T* dst, std::size_t n) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        std::size_t avail = headCache_ - tail;
        if (avail < n) {
            headCache_ = head_.load(std::memory_order_acquire);
            avail = headCache_ - tail;
        }
        n = std::min(n, avail);
        if (n != 0) {
            const std::size_t idx = tail & mask_;
            const std::size_t first = std::min(n, capacity() - idx);
            std::memcpy(dst, buf_.data() + idx, first * sizeof(T));
            std::memcpy(dst + first, buf_.data(), (n - first) * sizeof(T));
            tail_.store(tail + n, std::memory_order_release);
        }
        return n;
    }

    // ANCHOR_END: read

    /// Consumer: exact occupancy at the time of the call.
    std::size_t readAvailable() noexcept {
        headCache_ = head_.load(std::memory_order_acquire);
        return headCache_ - tail_.load(std::memory_order_relaxed);
    }

    /// Consumer: drop up to n elements without copying (hard resync path).
    /// Returns the number actually dropped.
    std::size_t discard(std::size_t n) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        std::size_t avail = headCache_ - tail;
        if (avail < n) {
            headCache_ = head_.load(std::memory_order_acquire);
            avail = headCache_ - tail;
        }
        n = std::min(n, avail);
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

private:
    // ANCHOR: layout
    // 64-byte separation to keep producer- and consumer-owned state on
    // distinct cache lines (std::hardware_destructive_interference_size is
    // deliberately avoided: it is ABI-fragile and warns on GCC). The
    // read-only members (buf_, mask_) lead so they share a line with each
    // other, not with either side's mutable state.
    static constexpr std::size_t kCacheLine = 64;

    std::vector<T> buf_;
    std::size_t mask_;
    alignas(kCacheLine) std::atomic<std::size_t> head_{0}; // written by producer
    alignas(kCacheLine) std::size_t tailCache_{0};         // producer's view of tail
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; // written by consumer
    alignas(kCacheLine) std::size_t headCache_{0};         // consumer's view of head
    // ANCHOR_END: layout
};

} // namespace srt

#endif // SRT_SPSC_RING_HPP
