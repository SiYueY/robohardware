#pragma once

#include "realtime/affinity.hpp"
#include "realtime/clock.hpp"
#include "realtime/error.hpp"
#include "realtime/memory.hpp"
#include "realtime/scheduler.hpp"
#include "realtime/statistics.hpp"
#include "realtime/status.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace realtime {

struct PeriodicTaskOptions {
    Duration period{};
    SchedulerConfig scheduler{};
    AffinityConfig affinity{};
    MemoryConfig memory{};
    RealtimeMode mode{RealtimeMode::Required};
};

struct CycleInfo {
    std::uint64_t sequence{0};
    TimePoint scheduled_time{};
    TimePoint wakeup_time{};
    Duration lateness{};
    std::uint32_t missed_periods{0};
};

class PeriodicTask {
public:
    // Opaque implementation type; its definition remains private to the library.
    struct Impl;
    explicit PeriodicTask(PeriodicTaskOptions options);
    ~PeriodicTask();
    PeriodicTask(const PeriodicTask&) = delete;
    PeriodicTask& operator=(const PeriodicTask&) = delete;

    template <typename Callback>
    Result<void> start(Callback&& callback) {
        static_assert(
            std::is_nothrow_invocable_r_v<void, Callback&, const CycleInfo&>,
            "PeriodicTask callback must be noexcept and return void");
        return start_impl(std::function<void(const CycleInfo&)>(std::forward<Callback>(callback)));
    }
    Result<void> stop();
    Status status() const noexcept;
    Stats stats() const noexcept;

private:
    Result<void> start_impl(std::function<void(const CycleInfo&)> callback);
    std::unique_ptr<Impl> impl_;
};

}  // namespace realtime
