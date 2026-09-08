#include "realtime/affinity.hpp"
#include "realtime/periodic_task.hpp"
#include "realtime/queue.hpp"
#include "realtime/scheduler.hpp"
#include "realtime/value.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;
namespace {
TEST(Scheduler, OtherAndInvalidPriority) {
    EXPECT_TRUE(realtime::set_scheduler(realtime::Scheduler::Other));
    EXPECT_EQ(
        realtime::set_scheduler(realtime::Scheduler::Other, 1).error().code,
        realtime::ErrorCode::InvalidArgument);
}
TEST(Affinity, InvalidCpu) {
    EXPECT_EQ(realtime::set_affinity(-1).error().code, realtime::ErrorCode::InvalidArgument);
}
TEST(PeriodicTask, LifecycleAndStats) {
    realtime::PeriodicTask task({2ms});
    std::atomic<unsigned> calls{0};
    ASSERT_TRUE(task.start([&](const realtime::CycleInfo&) noexcept { ++calls; }));
    EXPECT_TRUE(task.running());
    std::this_thread::sleep_for(12ms);
    EXPECT_TRUE(task.stop());
    EXPECT_FALSE(task.running());
    EXPECT_GT(calls.load(), 0U);
    EXPECT_EQ(task.stats().cycles, calls.load());
}
TEST(PeriodicTask, RejectsInvalidOptions) {
    realtime::PeriodicTask task({});
    EXPECT_EQ(
        task.start([](const auto&) noexcept {}).error().code, realtime::ErrorCode::InvalidArgument);
}
TEST(Value, LatestValue) {
    realtime::Value<int> value;
    int output = 0;
    EXPECT_FALSE(value.read(output));
    value.write(3);
    value.write(7);
    EXPECT_TRUE(value.read(output));
    EXPECT_EQ(output, 7);
}
TEST(Queue, FullAndFifo) {
    realtime::Queue<int, 2> queue;
    int output = 0;
    EXPECT_TRUE(queue.try_push(1));
    EXPECT_TRUE(queue.try_push(2));
    EXPECT_FALSE(queue.try_push(3));
    EXPECT_TRUE(queue.try_pop(output));
    EXPECT_EQ(output, 1);
}
}  // namespace
