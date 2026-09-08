#include "realtime/periodic_task.hpp"
#include "realtime/buffer.hpp"
#include "realtime/queue.hpp"
#include "realtime/linux_api.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <sched.h>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace {
using realtime::Duration;

realtime::PeriodicTaskOptions options(Duration period = 2ms) {
    realtime::PeriodicTaskOptions value;
    value.period = period;
    value.scheduler = {realtime::SchedulingPolicy::Other, 0};
    value.memory.lock_memory = false;
    value.mode = realtime::RealtimeMode::Required;
    return value;
}

class HookReset {
public:
    ~HookReset() {
        realtime::detail::test_hooks.mlockall_errno.store(0);
        realtime::detail::test_hooks.setsched_rc.store(0);
        realtime::detail::test_hooks.setaffinity_rc.store(0);
        realtime::detail::test_hooks.clock_nanosleep_rc.store(0);
    }
};

TEST(Clock, IsMonotonicAndChronoCompatible) {
    const auto before = realtime::Clock::now();
    std::this_thread::sleep_for(1ms);
    EXPECT_GT(realtime::Clock::now(), before);
    EXPECT_TRUE(realtime::Clock::is_steady);
}

TEST(PeriodicTask, RejectsInvalidSchedulerAndPeriod) {
    auto value = options();
    value.period = Duration::zero();
    realtime::PeriodicTask task(value);
    EXPECT_EQ(
        task.start([](const auto&) noexcept {}).error().code, realtime::ErrorCode::InvalidArgument);
    value = options();
    value.scheduler = {realtime::SchedulingPolicy::Other, 1};
    realtime::PeriodicTask other(value);
    EXPECT_EQ(
        other.start([](const auto&) noexcept {}).error().code,
        realtime::ErrorCode::InvalidArgument);
    value = options();
    value.affinity.cpu = -1;
    realtime::PeriodicTask affinity(value);
    EXPECT_EQ(
        affinity.start([](const auto&) noexcept {}).error().code,
        realtime::ErrorCode::InvalidArgument);
}

TEST(PeriodicTask, RequiredAndBestEffortExposeMemoryLockFailure) {
    HookReset reset;
    realtime::detail::test_hooks.mlockall_errno.store(EPERM);
    auto required_options = options();
    required_options.memory.lock_memory = true;
    realtime::PeriodicTask required(required_options);
    const auto failure = required.start([](const auto&) noexcept {});
    ASSERT_FALSE(failure);
    EXPECT_EQ(failure.error().code, realtime::ErrorCode::PermissionDenied);
    EXPECT_EQ(failure.error().native_domain, realtime::NativeErrorDomain::Errno);
    EXPECT_EQ(failure.error().native_code, EPERM);

    auto best_effort_options = required_options;
    best_effort_options.mode = realtime::RealtimeMode::BestEffort;
    realtime::PeriodicTask best_effort(best_effort_options);
    ASSERT_TRUE(best_effort.start([](const auto&) noexcept {}));
    EXPECT_FALSE(best_effort.status().memory_locked);
    EXPECT_TRUE(best_effort.stop());
}

TEST(PeriodicTask, AffinitySuccessAndPthreadFailure) {
    auto value = options();
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    ASSERT_EQ(sched_getaffinity(0, sizeof(allowed), &allowed), 0);
    int cpu = 0;
    while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &allowed)) ++cpu;
    ASSERT_LT(cpu, CPU_SETSIZE);
    value.affinity.cpu = cpu;
    realtime::PeriodicTask success(value);
    ASSERT_TRUE(success.start([](const auto&) noexcept {}));
    EXPECT_TRUE(success.status().affinity_applied);
    EXPECT_EQ(success.status().cpu, cpu);
    EXPECT_TRUE(success.stop());

    HookReset reset;
    realtime::detail::test_hooks.setaffinity_rc.store(EINVAL);
    realtime::PeriodicTask failed(value);
    const auto result = failed.start([](const auto&) noexcept {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, realtime::ErrorCode::AffinityFailed);
    EXPECT_EQ(result.error().native_domain, realtime::NativeErrorDomain::Pthread);
}

TEST(PeriodicTask, RestartResetsStatusAndStats) {
    auto value = options(2ms);
    value.mode = realtime::RealtimeMode::BestEffort;
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    ASSERT_EQ(sched_getaffinity(0, sizeof(allowed), &allowed), 0);
    int cpu = 0;
    while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &allowed)) ++cpu;
    ASSERT_LT(cpu, CPU_SETSIZE);
    value.affinity.cpu = cpu;
    realtime::PeriodicTask task(value);
    ASSERT_TRUE(task.start([](const auto&) noexcept {}));
    std::this_thread::sleep_for(8ms);
    ASSERT_TRUE(task.stop());
    ASSERT_TRUE(task.status().affinity_applied);
    ASSERT_GT(task.stats().cycles, 0U);

    HookReset reset;
    realtime::detail::test_hooks.setaffinity_rc.store(EINVAL);
    ASSERT_TRUE(task.start([](const auto&) noexcept {}));
    EXPECT_FALSE(task.status().affinity_applied);
    EXPECT_FALSE(task.status().cpu.has_value());
    EXPECT_EQ(task.stats().cycles, 0U);
    EXPECT_TRUE(task.stop());
}

TEST(PeriodicTask, ClockSleepFailureIsReturnedFromStop) {
    HookReset reset;
    realtime::detail::test_hooks.clock_nanosleep_rc.store(EINVAL);
    realtime::PeriodicTask task(options());
    ASSERT_TRUE(task.start([](const auto&) noexcept {}));
    std::this_thread::sleep_for(1ms);
    const auto result = task.stop();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, realtime::ErrorCode::ClockError);
    EXPECT_EQ(result.error().native_domain, realtime::NativeErrorDomain::Errno);
    EXPECT_EQ(result.error().native_code, EINVAL);
}

TEST(PeriodicTask, LifecycleAndSynchronousStop) {
    std::atomic<unsigned> calls{0};
    realtime::PeriodicTask task(options(10ms));
    ASSERT_TRUE(task.start([&](const auto&) noexcept { calls.fetch_add(1); }));
    EXPECT_EQ(
        task.start([](const auto&) noexcept {}).error().code, realtime::ErrorCode::InvalidState);
    std::this_thread::sleep_for(25ms);
    EXPECT_TRUE(task.stop());
    EXPECT_FALSE(task.status().running);
    EXPECT_GT(calls.load(), 0U);
    EXPECT_EQ(task.stop().error().code, realtime::ErrorCode::InvalidState);
}

TEST(PeriodicTask, StopDuringAbsoluteSleepCompletesWorker) {
    realtime::PeriodicTask task(options(80ms));
    ASSERT_TRUE(task.start([](const auto&) noexcept {}));
    std::this_thread::sleep_for(5ms);
    const auto before = std::chrono::steady_clock::now();
    ASSERT_TRUE(task.stop());
    EXPECT_LT(std::chrono::steady_clock::now() - before, 150ms);
    EXPECT_FALSE(task.status().running);
}

TEST(PeriodicTask, AbsoluteDeadlineOverrunAndStats) {
    std::vector<realtime::CycleInfo> cycles;
    realtime::PeriodicTask task(options(2ms));
    ASSERT_TRUE(task.start([&](const realtime::CycleInfo& cycle) noexcept {
        cycles.push_back(cycle);
        if (cycle.sequence == 0) std::this_thread::sleep_for(7ms);
    }));
    std::this_thread::sleep_for(25ms);
    ASSERT_TRUE(task.stop());
    ASSERT_GE(cycles.size(), 2U);
    EXPECT_EQ(
        cycles[1].scheduled_time - cycles[0].scheduled_time,
        options(2ms).period * (static_cast<std::int64_t>(cycles[0].missed_periods) + 1));
    bool found_skip = false;
    for (const auto& cycle : cycles) found_skip |= cycle.missed_periods > 0;
    EXPECT_TRUE(found_skip);
    const auto stats = task.stats();
    EXPECT_EQ(stats.cycles, cycles.size());
    EXPECT_GE(stats.missed_periods, 1U);
    EXPECT_GE(stats.deadline_misses, 1U);
}

TEST(PeriodicTask, CallbackResourcesLiveUntilStopAnd500HzFunctional) {
    struct Resource {
        std::atomic<int> calls{0};
    } resource;
    realtime::PeriodicTask task(options(2ms));
    ASSERT_TRUE(task.start([&](const auto&) noexcept { resource.calls.fetch_add(1); }));
    std::this_thread::sleep_for(60ms);
    ASSERT_TRUE(task.stop());
    EXPECT_GE(resource.calls.load(), 15);
    EXPECT_GE(task.stats().cycles, 15U);
}

TEST(Buffer, InitiallyEmptyAndBasicLatestValue) {
    realtime::Buffer<int> buffer;
    int value = 0;
    EXPECT_FALSE(buffer.read(value));
    EXPECT_TRUE(buffer.write(3));
    EXPECT_TRUE(buffer.write(7));
    EXPECT_TRUE(buffer.read(value));
    EXPECT_EQ(value, 7);
}

TEST(Queue, EmptyFullAndWrapAround) {
    realtime::Queue<int, 2> queue;
    int value = 0;
    EXPECT_FALSE(queue.try_pop(value));
    EXPECT_TRUE(queue.try_push(1));
    EXPECT_TRUE(queue.try_push(2));
    EXPECT_FALSE(queue.try_push(3));
    ASSERT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.try_push(3));
    ASSERT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 2);
    ASSERT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 3);
}
}  // namespace
