#include "realtime/periodic_task.hpp"

#include "realtime/linux_api.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sched.h>
#include <thread>
#include <vector>

namespace realtime {
namespace {
Error pthread_error(ErrorCode code, int native_code) noexcept {
    return {
        native_code == EPERM ? ErrorCode::PermissionDenied : code, NativeErrorDomain::Pthread,
        native_code};
}
Error errno_error(ErrorCode code) noexcept {
    const int native_code = errno;
    return {
        native_code == EPERM || native_code == EACCES ? ErrorCode::PermissionDenied : code,
        NativeErrorDomain::Errno, native_code};
}
timespec to_timespec(TimePoint time) noexcept {
    const auto ns = time.time_since_epoch().count();
    return {static_cast<time_t>(ns / 1000000000LL), static_cast<long>(ns % 1000000000LL)};
}
void update_min(std::atomic<std::int64_t>& target, std::int64_t value) noexcept {
    auto old = target.load(std::memory_order_relaxed);
    while (value < old && !target.compare_exchange_weak(old, value, std::memory_order_relaxed)) {
    }
}
void update_max(std::atomic<std::int64_t>& target, std::int64_t value) noexcept {
    auto old = target.load(std::memory_order_relaxed);
    while (value > old && !target.compare_exchange_weak(old, value, std::memory_order_relaxed)) {
    }
}
}  // namespace

struct PeriodicTask::Impl {
    explicit Impl(PeriodicTaskOptions value) : options(std::move(value)) {}
    PeriodicTaskOptions options;
    std::function<void(const CycleInfo&)> callback;
    std::vector<std::byte> prefault_storage;
    pthread_t thread{};
    std::mutex lifecycle_mutex;
    bool joinable{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> running{false};
    std::atomic<bool> realtime_scheduling{false};
    std::atomic<bool> memory_locked{false};
    std::atomic<bool> affinity_applied{false};
    std::atomic<int> effective_policy{static_cast<int>(SchedulingPolicy::Other)};
    std::atomic<int> effective_priority{0};
    std::atomic<int> effective_cpu{-1};
    std::mutex startup_mutex;
    std::condition_variable startup_cv;
    bool startup_ready{false};
    std::optional<Error> startup_error;
    std::optional<Error> runtime_error;
    std::atomic<std::uint64_t> cycles{0}, deadline_misses{0}, missed_periods{0};
    std::atomic<std::int64_t> min_lateness{0}, max_lateness{0}, min_execution{0}, max_execution{0};
    std::atomic<bool> has_samples{false};
};

namespace {
Result<void> validate(const PeriodicTaskOptions& options) noexcept {
    if (options.period <= Duration::zero()) return Error{ErrorCode::InvalidArgument};
    if (options.affinity.cpu && *options.affinity.cpu < 0) return Error{ErrorCode::InvalidArgument};
    if (options.scheduler.policy == SchedulingPolicy::Other) {
        if (options.scheduler.priority != 0) return Error{ErrorCode::InvalidArgument};
        return {};
    }
    const int min = sched_get_priority_min(SCHED_FIFO);
    const int max = sched_get_priority_max(SCHED_FIFO);
    if (min == -1 || max == -1 || options.scheduler.priority < min ||
        options.scheduler.priority > max) {
        return Error{ErrorCode::InvalidArgument};
    }
    return {};
}

void reset_runtime_state(PeriodicTask::Impl& impl) noexcept {
    impl.running.store(false, std::memory_order_release);
    impl.realtime_scheduling.store(false, std::memory_order_release);
    impl.memory_locked.store(false, std::memory_order_release);
    impl.affinity_applied.store(false, std::memory_order_release);
    impl.effective_policy.store(
        static_cast<int>(SchedulingPolicy::Other), std::memory_order_release);
    impl.effective_priority.store(0, std::memory_order_release);
    impl.effective_cpu.store(-1, std::memory_order_release);
    impl.cycles.store(0, std::memory_order_release);
    impl.deadline_misses.store(0, std::memory_order_release);
    impl.missed_periods.store(0, std::memory_order_release);
    impl.min_lateness.store(0, std::memory_order_release);
    impl.max_lateness.store(0, std::memory_order_release);
    impl.min_execution.store(0, std::memory_order_release);
    impl.max_execution.store(0, std::memory_order_release);
    impl.has_samples.store(false, std::memory_order_release);
    impl.runtime_error.reset();
}

bool apply_setup(PeriodicTask::Impl& impl, Error& error) noexcept {
    bool failed = false;
    const auto fail = [&](Error value) noexcept {
        if (!failed) error = value;
        failed = true;
    };
    if (impl.options.memory.lock_memory) {
        if (detail::memory_lock() == 0)
            impl.memory_locked.store(true, std::memory_order_release);
        else
            fail(errno_error(ErrorCode::MemoryLockFailed));
    }
    sched_param param{};
    param.sched_priority = impl.options.scheduler.priority;
    const int policy =
        impl.options.scheduler.policy == SchedulingPolicy::Fifo ? SCHED_FIFO : SCHED_OTHER;
    const int scheduler_rc = detail::set_scheduler(pthread_self(), policy, &param);
    if (scheduler_rc == 0) {
        impl.realtime_scheduling.store(
            impl.options.scheduler.policy == SchedulingPolicy::Fifo, std::memory_order_release);
        impl.effective_policy.store(
            static_cast<int>(impl.options.scheduler.policy), std::memory_order_release);
        impl.effective_priority.store(impl.options.scheduler.priority, std::memory_order_release);
    } else {
        fail(pthread_error(ErrorCode::SchedulingFailed, scheduler_rc));
    }
    if (impl.options.affinity.cpu) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(*impl.options.affinity.cpu, &cpuset);
        const int affinity_rc = detail::set_affinity(pthread_self(), sizeof(cpuset), &cpuset);
        if (affinity_rc == 0) {
            impl.affinity_applied.store(true, std::memory_order_release);
            impl.effective_cpu.store(*impl.options.affinity.cpu, std::memory_order_release);
        } else {
            fail(pthread_error(ErrorCode::AffinityFailed, affinity_rc));
        }
    }
    if (!impl.prefault_storage.empty()) {
        constexpr std::size_t page = 4096;
        volatile std::byte sink{};
        for (std::size_t i = 0; i < impl.prefault_storage.size(); i += page)
            sink = impl.prefault_storage[i];
        sink = impl.prefault_storage.back();
        (void)sink;
    }
    return !failed || impl.options.mode == RealtimeMode::BestEffort;
}

void update_stats(PeriodicTask::Impl& impl, const CycleInfo& cycle, Duration execution) noexcept {
    const auto late = cycle.lateness.count();
    const auto exec = execution.count();
    if (!impl.has_samples.exchange(true, std::memory_order_acq_rel)) {
        impl.min_lateness.store(late, std::memory_order_relaxed);
        impl.max_lateness.store(late, std::memory_order_relaxed);
        impl.min_execution.store(exec, std::memory_order_relaxed);
        impl.max_execution.store(exec, std::memory_order_relaxed);
    } else {
        update_min(impl.min_lateness, late);
        update_max(impl.max_lateness, late);
        update_min(impl.min_execution, exec);
        update_max(impl.max_execution, exec);
    }
    impl.cycles.fetch_add(1, std::memory_order_relaxed);
    impl.missed_periods.fetch_add(cycle.missed_periods, std::memory_order_relaxed);
    if (cycle.missed_periods > 0) impl.deadline_misses.fetch_add(1, std::memory_order_relaxed);
}

void* worker(void* opaque) noexcept {
    auto& impl = *static_cast<PeriodicTask::Impl*>(opaque);
    Error setup_error{ErrorCode::InvalidState};
    const bool setup_ok = apply_setup(impl, setup_error);
    {
        std::lock_guard<std::mutex> lock(impl.startup_mutex);
        impl.startup_ready = true;
        if (!setup_ok) impl.startup_error = setup_error;
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
            rc = detail::sleep_until(&target);
        } while (rc == EINTR);
        if (impl.stop_requested.load(std::memory_order_acquire)) break;
        if (rc != 0) {
            // clock_nanosleep returns a POSIX errno value directly rather than
            // setting errno. NativeErrorDomain has no separate POSIX-return domain,
            // so retain that errno-compatible root value under Errno.
            impl.runtime_error = Error{ErrorCode::ClockError, NativeErrorDomain::Errno, rc};
            break;
        }
        const TimePoint wakeup = Clock::now();
        const Duration lateness = wakeup > deadline ? wakeup - deadline : Duration::zero();
        const auto raw_missed = lateness / impl.options.period;
        using MissedCount = std::remove_cv_t<decltype(raw_missed)>;
        const auto missed =
            raw_missed > static_cast<MissedCount>(std::numeric_limits<std::uint32_t>::max())
                ? std::numeric_limits<std::uint32_t>::max()
                : static_cast<std::uint32_t>(raw_missed);
        CycleInfo cycle{sequence++, deadline, wakeup, lateness, missed};
        const TimePoint callback_start = Clock::now();
        impl.callback(cycle);
        update_stats(impl, cycle, Clock::now() - callback_start);
        deadline += impl.options.period * (static_cast<std::int64_t>(missed) + 1);
    }
    impl.running.store(false, std::memory_order_release);
    return nullptr;
}
}  // namespace

PeriodicTask::PeriodicTask(PeriodicTaskOptions options)
: impl_(std::make_unique<Impl>(std::move(options))) {}
PeriodicTask::~PeriodicTask() { (void)stop(); }

Result<void> PeriodicTask::start_impl(std::function<void(const CycleInfo&)> callback) {
    if (auto valid = validate(impl_->options); !valid) return valid;
    std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
    if (impl_->joinable) return Error{ErrorCode::InvalidState};
    impl_->callback = std::move(callback);
    reset_runtime_state(*impl_);
    impl_->prefault_storage.assign(impl_->options.memory.prefault_bytes, std::byte{});
    impl_->stop_requested.store(false, std::memory_order_release);
    impl_->startup_ready = false;
    impl_->startup_error.reset();
    const int rc = pthread_create(&impl_->thread, nullptr, &worker, impl_.get());
    if (rc != 0) return pthread_error(ErrorCode::ThreadCreateFailed, rc);
    impl_->joinable = true;
    {
        std::unique_lock<std::mutex> setup_lock(impl_->startup_mutex);
        impl_->startup_cv.wait(setup_lock, [&] { return impl_->startup_ready; });
        if (!impl_->startup_error) return {};
    }
    const auto error = *impl_->startup_error;
    pthread_join(impl_->thread, nullptr);
    impl_->joinable = false;
    return error;
}

Result<void> PeriodicTask::stop() {
    std::unique_lock<std::mutex> lock(impl_->lifecycle_mutex);
    if (!impl_->joinable) return Error{ErrorCode::InvalidState};
    if (pthread_equal(pthread_self(), impl_->thread)) return Error{ErrorCode::InvalidState};
    impl_->stop_requested.store(true, std::memory_order_release);
    const pthread_t thread = impl_->thread;
    lock.unlock();
    const int rc = pthread_join(thread, nullptr);
    lock.lock();
    impl_->joinable = false;
    impl_->callback = {};
    if (rc != 0) return pthread_error(ErrorCode::InvalidState, rc);
    if (impl_->runtime_error) return *impl_->runtime_error;
    return {};
}

Status PeriodicTask::status() const noexcept {
    Status result;
    result.running = impl_->running.load(std::memory_order_acquire);
    result.realtime_scheduling = impl_->realtime_scheduling.load(std::memory_order_acquire);
    result.memory_locked = impl_->memory_locked.load(std::memory_order_acquire);
    result.affinity_applied = impl_->affinity_applied.load(std::memory_order_acquire);
    result.policy =
        static_cast<SchedulingPolicy>(impl_->effective_policy.load(std::memory_order_acquire));
    result.priority = impl_->effective_priority.load(std::memory_order_acquire);
    const int cpu = impl_->effective_cpu.load(std::memory_order_acquire);
    if (cpu >= 0) result.cpu = cpu;
    return result;
}

Stats PeriodicTask::stats() const noexcept {
    Stats result;
    result.cycles = impl_->cycles.load(std::memory_order_relaxed);
    result.deadline_misses = impl_->deadline_misses.load(std::memory_order_relaxed);
    result.missed_periods = impl_->missed_periods.load(std::memory_order_relaxed);
    if (impl_->has_samples.load(std::memory_order_acquire)) {
        result.min_lateness = Duration(impl_->min_lateness.load(std::memory_order_relaxed));
        result.max_lateness = Duration(impl_->max_lateness.load(std::memory_order_relaxed));
        result.min_execution = Duration(impl_->min_execution.load(std::memory_order_relaxed));
        result.max_execution = Duration(impl_->max_execution.load(std::memory_order_relaxed));
    }
    return result;
}
}  // namespace realtime
