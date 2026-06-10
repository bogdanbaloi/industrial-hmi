// [utest->req~arch-010~1]
// Covers REQ-ARCH-010 (lock-free SPSC ring buffer): single-threaded
// logic (round-trip, full/empty, FIFO, wrap-around) plus a real
// two-thread producer/consumer stress test that must run clean under
// ThreadSanitizer (-fsanitize=thread, the REQ-ARCH-008 CI gate).
//
// The queue reserves one slot to distinguish full from empty, so a
// SpscQueue<T, N> holds at most N-1 items.

#include "src/core/SpscQueue.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <thread>

namespace {

using app::core::SpscQueue;

// Small power-of-two capacity for deterministic full/wrap tests.
// Usable depth = 4 - 1 = 3.
constexpr std::size_t kCap = 4;

TEST(SpscQueueTest, EmptyOnConstruction) {
    SpscQueue<int, kCap> q;
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0u);
    int out = -1;
    EXPECT_FALSE(q.pop(out));
    EXPECT_EQ(out, -1) << "pop() on empty must not touch out";
}

TEST(SpscQueueTest, PushAndPopRoundTrip) {
    SpscQueue<int, kCap> q;
    EXPECT_TRUE(q.push(42));
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1u);

    int out = 0;
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, 42);
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueueTest, FillToCapacityThenReject) {
    SpscQueue<int, kCap> q;
    // Usable depth is kCap - 1 = 3.
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_TRUE(q.full());
    EXPECT_FALSE(q.push(4)) << "push on a full queue must fail, not block";
    EXPECT_EQ(q.size(), kCap - 1);
}

TEST(SpscQueueTest, FullQueueDropLeavesContentsIntact) {
    SpscQueue<int, kCap> q;
    EXPECT_TRUE(q.push(10));
    EXPECT_TRUE(q.push(20));
    EXPECT_TRUE(q.push(30));
    EXPECT_FALSE(q.push(99));  // dropped

    int out = 0;
    EXPECT_TRUE(q.pop(out)); EXPECT_EQ(out, 10);
    EXPECT_TRUE(q.pop(out)); EXPECT_EQ(out, 20);
    EXPECT_TRUE(q.pop(out)); EXPECT_EQ(out, 30);
    EXPECT_FALSE(q.pop(out));
}

TEST(SpscQueueTest, FifoOrdering) {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 7; ++i) {
        ASSERT_TRUE(q.push(i));
    }
    for (int i = 0; i < 7; ++i) {
        int out = -1;
        ASSERT_TRUE(q.pop(out));
        EXPECT_EQ(out, i) << "items must come out in push order";
    }
}

TEST(SpscQueueTest, WrapAroundAcrossPowerOfTwoBoundary) {
    // Push/pop in waves so the free-running indices cross the kCap
    // wrap point several times; the power-of-two mask must keep
    // addressing the right slot.
    SpscQueue<int, kCap> q;
    int out = 0;
    for (int wave = 0; wave < 10; ++wave) {
        ASSERT_TRUE(q.push(wave * 2));
        ASSERT_TRUE(q.push(wave * 2 + 1));
        ASSERT_TRUE(q.pop(out)); EXPECT_EQ(out, wave * 2);
        ASSERT_TRUE(q.pop(out)); EXPECT_EQ(out, wave * 2 + 1);
    }
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueueTest, SizeReflectsContents) {
    SpscQueue<int, 8> q;
    EXPECT_EQ(q.size(), 0u);
    ASSERT_TRUE(q.push(1)); EXPECT_EQ(q.size(), 1u);
    ASSERT_TRUE(q.push(2)); EXPECT_EQ(q.size(), 2u);
    int out = 0;
    ASSERT_TRUE(q.pop(out)); EXPECT_EQ(q.size(), 1u);
    ASSERT_TRUE(q.pop(out)); EXPECT_EQ(q.size(), 0u);
}

TEST(SpscQueueTest, CapacityIsCompileTimeConstant) {
    EXPECT_EQ((SpscQueue<int, 64>::capacity()), 64u);
}

// The real proof: one producer thread + one consumer thread hammering
// the queue concurrently. Each pushed value is a sequential counter; the
// consumer sums every value it pops. After both threads join, the sum
// must equal the triangular number 0+1+...+(N-1) -- proving no item was
// lost, duplicated, or torn. Must be ThreadSanitizer-clean.
TEST(SpscQueueTest, StressProducerConsumer) {
    // Skip under Valgrind memcheck: N=1M push/pop spins ~20-50x slower
    // and would tip the 300s per-test ctest --memcheck timeout. The
    // race this proves is a TSan concern, not a leak concern, so
    // memcheck adds no signal (same guard as
    // ConfigManagerTest.ConcurrentReadersDuringReload).
    if (const char* v = std::getenv("RUNNING_UNDER_VALGRIND");
        v != nullptr && std::string_view(v) == "1") {
        GTEST_SKIP() << "StressProducerConsumer skipped under Valgrind "
                        "memcheck (TSan covers this case).";
    }

    constexpr std::uint64_t kN = 1'000'000;
    SpscQueue<std::uint64_t, 1024> q;

    std::jthread producer([&q] {
        for (std::uint64_t i = 0; i < kN; ++i) {
            // Spin until the consumer frees a slot -- the queue never
            // blocks, so back-pressure is the caller's job.
            while (!q.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::uint64_t sum = 0;
    std::jthread consumer([&q, &sum] {
        std::uint64_t seen = 0;
        std::uint64_t value = 0;
        while (seen < kN) {
            if (q.pop(value)) {
                sum += value;
                ++seen;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // 0 + 1 + ... + (kN-1) = kN*(kN-1)/2.
    const std::uint64_t expected = kN * (kN - 1) / 2;
    EXPECT_EQ(sum, expected)
        << "lost, duplicated, or torn items under concurrency";
}

}  // namespace
