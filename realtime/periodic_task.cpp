#include "realtime/periodic_task.hpp"

#include "realtime/affinity.hpp"

#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <pthread.h>

namespace realtime {
namespace {
timespec to_timespec(TimePoint time) noexcept {
    const auto ns = time.time_since_epoch().count();
    return {static_cast<time_t>(ns / 1000000000LL), static_cast<long>(ns % 1000000000LL)};
}
void update_max(std::atomic<std::int64_t>& target, std::int64_t value) noexcept {
    auto old = target.load(std::memory_order_relaxed);
    while (value > old && !target.compare_exchange_weak(old, value, std::memory_order_relaxed)) {
    }
}
}  // namespace

struct PeriodicTask::Impl {
    explicit Impl(Options value) : options(std::move(value)) {}
    Options options;
    std::function<void(const CycleInfo&)> callback;
    pthread_t thread{};
    std::mutex lifecycle_mutex;
    bool joinable{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> running{false};
    std::mutex startup_mutex;
    std::condition_variable startup_cv;
    bool startup_ready{false};
    std::optional<Error> startup_error;
    std::optional<Error> runtime_error;
    std::atomic<std::uint64_t> cycles{0}, deadline_misses{0}, missed_periods{0};
    std::atomic<std::int64_t> max_lateness{0}, max_execution{0};
};

namespace {
void reset(PeriodicTask::Impl& impl) noexcept {
    impl.stop_requested.store(false, std::memory_order_release);
    impl.running.store(false, std::memory_order_release);
    impl.cycles.store(0, std::memory_order_release);
    impl.deadline_misses.store(0, std::memory_order_release);
    impl.missed_periods.store(0, std::memory_order_release);
    impl.max_lateness.store(0, std::memory_order_release);
    impl.max_execution.store(0, std::memory_order_release);
    impl.runtime_error.reset();
}
bool setup(PeriodicTask::Impl& impl, Error& error) noexcept {
    const auto scheduler = set_scheduler(impl.options.scheduler, impl.options.priority);
    if (!scheduler) {
        error = scheduler.error();
        if (impl.options.required) return false;
    }
    if (impl.options.cpu) {
        const auto affinity = set_affinity(*impl.options.cpu);
        if (!affinity) {
            error = affinity.error();
            if (impl.options.required) return false;
        }
    }
    return true;
}
void update_stats(PeriodicTask::Impl& impl, const CycleInfo& cycle, Duration execution) noexcept {
    update_max(impl.max_lateness, cycle.lateness.count());
    update_max(impl.max_execution, execution.count());
    impl.cycles.fetch_add(1, std::memory_order_relaxed);
    impl.missed_periods.fetch_add(cycle.missed_periods, std::memory_order_relaxed);
    if (cycle.missed_periods > 0) impl.deadline_misses.fetch_add(1, std::memory_order_relaxed);
}
void* worker(void* opaque) noexcept {
    auto& impl = *static_cast<PeriodicTask::Impl*>(opaque);
    Error error{ErrorCode::InvalidState};
    const bool setup_ok = setup(impl, error);
    {
        std::lock_guard<std::mutex> lock(impl.startup_mutex);
        impl.startup_ready = true;
        if (!setup_ok) impl.startup_error = error;
    }
    impl.startup_cv.notify_one();
    if (!setup_ok) return nullptr;
    impl.running.store(true, std::memory_order_release);
    TimePoint deadline = Clock::now() + impl.options.period;
    std::uint64_t sequence = 0;
    while (!impl.stop_requested.load(std::memory_order_acquire)) {
        const timespec target = to_timespec(deadline);
        int rc;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, nullptr);
        } while (rc == EINTR);
        if (impl.stop_requested.load(std::memory_order_acquire)) break;
        if (rc != 0) {
            impl.runtime_error = Error{ErrorCode::ClockError, NativeErrorDomain::Errno, rc};
            break;
        }
        const TimePoint wakeup = Clock::now();
        const Duration lateness = wakeup > deadline ? wakeup - deadline : Duration::zero();
        const auto periods = lateness / impl.options.period;
        const auto missed = periods > std::numeric_limits<std::uint32_t>::max()
                                ? std::numeric_limits<std::uint32_t>::max()
                                : static_cast<std::uint32_t>(periods);
        const CycleInfo cycle{sequence++, deadline, wakeup, lateness, missed};
        const TimePoint begin = Clock::now();
        impl.callback(cycle);
        update_stats(impl, cycle, Clock::now() - begin);
        deadline += impl.options.period * (static_cast<std::int64_t>(missed) + 1);
    }
    impl.running.store(false, std::memory_order_release);
    return nullptr;
}
}  // namespace

PeriodicTask::PeriodicTask(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
PeriodicTask::~PeriodicTask() { (void)stop(); }

Result<void> PeriodicTask::start_impl(std::function<void(const CycleInfo&)> callback) {
    if (impl_->options.period <= Duration::zero() ||
        (impl_->options.cpu && *impl_->options.cpu < 0) ||
        (impl_->options.scheduler == Scheduler::Other && impl_->options.priority != 0) ||
        (impl_->options.scheduler != Scheduler::Other &&
         impl_->options.scheduler != Scheduler::Fifo))
        return Error{ErrorCode::InvalidArgument};
    std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
    if (impl_->joinable) return Error{ErrorCode::InvalidState};
    impl_->callback = std::move(callback);
    reset(*impl_);
    impl_->startup_ready = false;
    impl_->startup_error.reset();
    const int rc = pthread_create(&impl_->thread, nullptr, worker, impl_.get());
    if (rc != 0) return Error{ErrorCode::ThreadCreateFailed, NativeErrorDomain::Pthread, rc};
    impl_->joinable = true;
    std::unique_lock<std::mutex> setup_lock(impl_->startup_mutex);
    impl_->startup_cv.wait(setup_lock, [&] { return impl_->startup_ready; });
    if (!impl_->startup_error) return {};
    const Error error = *impl_->startup_error;
    setup_lock.unlock();
    pthread_join(impl_->thread, nullptr);
    impl_->joinable = false;
    return error;
}
Result<void> PeriodicTask::stop() {
    std::unique_lock<std::mutex> lock(impl_->lifecycle_mutex);
    if (!impl_->joinable || pthread_equal(pthread_self(), impl_->thread))
        return Error{ErrorCode::InvalidState};
    impl_->stop_requested.store(true, std::memory_order_release);
    const pthread_t thread = impl_->thread;
    lock.unlock();
    const int rc = pthread_join(thread, nullptr);
    lock.lock();
    impl_->joinable = false;
    impl_->callback = {};
    if (rc != 0) return Error{ErrorCode::InvalidState, NativeErrorDomain::Pthread, rc};
    if (impl_->runtime_error) return *impl_->runtime_error;
    return {};
}
bool PeriodicTask::running() const noexcept {
    return impl_->running.load(std::memory_order_acquire);
}
Stats PeriodicTask::stats() const noexcept {
    return {
        impl_->cycles.load(std::memory_order_relaxed),
        impl_->deadline_misses.load(std::memory_order_relaxed),
        impl_->missed_periods.load(std::memory_order_relaxed),
        Duration(impl_->max_lateness.load(std::memory_order_relaxed)),
        Duration(impl_->max_execution.load(std::memory_order_relaxed))};
}
}  // namespace realtime
