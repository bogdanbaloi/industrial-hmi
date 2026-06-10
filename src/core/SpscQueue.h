#ifndef APP_CORE_SPSC_QUEUE_H
#define APP_CORE_SPSC_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace app::core {

/// Bounded, lock-free single-producer / single-consumer ring buffer.
///
/// One thread calls `push()` (the producer), one OTHER thread calls
/// `pop()` (the consumer). That is the entire contract: exactly one
/// producer and exactly one consumer, never more. Under that
/// restriction the queue needs no mutex -- two atomics (`head_`,
/// `tail_`) coordinate the two threads with acquire/release ordering.
///
/// @rationale Used to decouple a latency-sensitive producer (e.g. the
/// Modbus poll thread doing blocking socket I/O on a tight cadence)
/// from a consumer that dispatches into slower downstream code. The
/// producer pushes and returns immediately instead of blocking on a
/// shared mutex; see ADR-0018 for why this seam justifies lock-free
/// while the fan-in seams (MQTT multi-topic, the GTK-thread historian
/// bridge) deliberately stay mutex-based.
///
/// @memory_ordering The Lamport / Dekker SPSC protocol. Each side owns
/// one index and only ever *reads* the other side's index:
///   - producer writes `tail_`, reads `head_`;
///   - consumer writes `head_`, reads `tail_`.
/// The store that publishes new data is RELEASE; the load that observes
/// it is ACQUIRE. This pairs the producer's element write with the
/// consumer's element read so the consumer never sees a slot's payload
/// before the producer finished writing it. A `relaxed` load on either
/// observed index would be a data race ThreadSanitizer flags.
///
/// @capacity Must be a power of two so the index-to-slot map is a cheap
/// mask (`& (kCapacity - 1)`) instead of a modulo. One slot is
/// effectively reserved to distinguish "full" from "empty" using the
/// monotonically-increasing index scheme, so the usable depth is
/// `kCapacity - 1`. Indices are free-running `std::size_t` that wrap
/// naturally; only the masked low bits address the buffer.
///
/// @thread_safety push() producer-thread-only; pop() consumer-thread-
/// only. `size()` / `empty()` / `full()` are advisory snapshots safe to
/// call from either thread but may be stale the instant they return.
/// Non-copyable, non-movable (the atomics make it neither).
template <typename T, std::size_t kCapacity>
class SpscQueue {
public:
    static_assert(kCapacity >= 2,
                  "SpscQueue needs at least 2 slots");
    static_assert((kCapacity & (kCapacity - 1)) == 0,
                  "SpscQueue capacity must be a power of two");
    static_assert(std::is_nothrow_move_constructible_v<T> ||
                      std::is_nothrow_copy_constructible_v<T>,
                  "SpscQueue<T> requires T to be nothrow-constructible "
                  "so push()/pop() can be noexcept");

    SpscQueue() = default;
    SpscQueue(const SpscQueue&)            = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&)                 = delete;
    SpscQueue& operator=(SpscQueue&&)      = delete;

    /// Enqueue a copy. PRODUCER THREAD ONLY.
    /// @return true if stored; false if the queue was full (caller
    ///         decides whether to drop or retry -- the queue never
    ///         blocks).
    [[nodiscard]] bool push(const T& item) noexcept {
        return emplaceImpl(item);
    }

    /// Enqueue by move. PRODUCER THREAD ONLY.
    [[nodiscard]] bool push(T&& item) noexcept {
        return emplaceImpl(std::move(item));
    }

    /// Dequeue into `out`. CONSUMER THREAD ONLY.
    /// @return true if an item was moved into `out`; false if the queue
    ///         was empty (`out` is left untouched).
    [[nodiscard]] bool pop(T& out) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        // ACQUIRE: observe the producer's published tail (and, via the
        // release/acquire pair, the element it wrote).
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = std::move(buffer_[head & kMask]);
        // RELEASE: publish the freed slot to the producer.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Advisory: true when the queue currently holds no items.
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /// Advisory: true when a push() would currently fail.
    [[nodiscard]] bool full() const noexcept { return size() >= kCapacity - 1; }

    /// Advisory snapshot of the number of buffered items.
    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t head = head_.load(std::memory_order_acquire);
        return tail - head;  // free-running indices; unsigned wrap is fine
    }

    /// Total slot count (compile-time). Usable depth is capacity() - 1.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return kCapacity;
    }

private:
    static constexpr std::size_t kMask = kCapacity - 1;

    template <typename U>
    [[nodiscard]] bool emplaceImpl(U&& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = tail + 1;
        // Full when the producer would catch the consumer's head. ACQUIRE
        // so we see the consumer's most recent slot release.
        if (next - head_.load(std::memory_order_acquire) > kCapacity - 1) {
            return false;  // full -- drop, never block
        }
        buffer_[tail & kMask] = std::forward<U>(item);
        // RELEASE: publish the element BEFORE the consumer can observe
        // the advanced tail.
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // head_ (consumer-owned) and tail_ (producer-owned) live on separate
    // 64-byte cache lines so the two threads never invalidate each
    // other's line writing their own index (false-sharing elimination).
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::array<T, kCapacity> buffer_{};
};

}  // namespace app::core

#endif  // APP_CORE_SPSC_QUEUE_H
