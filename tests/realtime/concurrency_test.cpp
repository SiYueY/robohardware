#include "realtime/buffer.hpp"
#include "realtime/queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

namespace {
struct Payload {
    std::uint64_t sequence{0};
    std::uint64_t complement{~std::uint64_t{0}};
    std::uint64_t checksum{0};
};
static_assert(std::is_nothrow_copy_assignable_v<Payload>);
Payload payload(std::uint64_t sequence) noexcept {
    return {sequence, ~sequence, sequence ^ (~sequence) ^ 0xa5a5a5a5a5a5a5a5ULL};
}
bool valid(const Payload& value) noexcept {
    return value.complement == ~value.sequence &&
           value.checksum == (value.sequence ^ value.complement ^ 0xa5a5a5a5a5a5a5a5ULL);
}

TEST(BufferConcurrency, SpscPayloadIntegrityAndSlotReuse) {
    constexpr std::uint64_t kCount = 5000000;
    realtime::Buffer<Payload> buffer;
    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> writes{0};
    std::thread producer([&] {
        for (std::uint64_t i = 1; i <= kCount;) {
            if (buffer.write(payload(i))) {
                writes.store(i, std::memory_order_release);
                ++i;
            }
        }
        done.store(true, std::memory_order_release);
    });
    std::uint64_t last = 0;
    std::uint64_t reads = 0;
    while (!done.load(std::memory_order_acquire) || last < writes.load(std::memory_order_acquire)) {
        Payload value;
        if (!buffer.read(value)) continue;
        ASSERT_TRUE(valid(value));
        ASSERT_GE(value.sequence, last);
        last = value.sequence;
        ++reads;
    }
    producer.join();
    EXPECT_GT(reads, 0U);
    EXPECT_EQ(last, kCount);
}

TEST(QueueConcurrency, SpscContinuityUnderProducerAndConsumerImbalance) {
    constexpr std::uint64_t kCount = 5000000;
    realtime::Queue<Payload, 31> queue;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (std::uint64_t i = 1; i <= kCount;)
            if (queue.try_push(payload(i))) ++i;
        done.store(true, std::memory_order_release);
    });
    std::uint64_t expected = 1;
    while (!done.load(std::memory_order_acquire) || expected <= kCount) {
        Payload value;
        if (!queue.try_pop(value)) continue;
        ASSERT_TRUE(valid(value));
        ASSERT_EQ(value.sequence, expected++);
        if ((expected & 63U) == 0U) std::this_thread::yield();
    }
    producer.join();
    EXPECT_EQ(expected, kCount + 1);
}
}  // namespace
