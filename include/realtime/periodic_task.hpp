#pragma once

#include "realtime/clock.hpp"
#include "realtime/error.hpp"
#include "realtime/scheduler.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace realtime {

struct CycleInfo {
    std::uint64_t sequence{0};
    TimePoint scheduled_time{};
    TimePoint wakeup_time{};
    Duration lateness{};
    std::uint32_t missed_periods{0};
};

struct Stats {
    std::uint64_t cycles{};
    std::uint64_t deadline_misses{};
    std::uint64_t missed_periods{};
    Duration max_lateness{};
    Duration max_execution{};
};

class PeriodicTask {
public:
    struct Options {
        Duration period{};
        Scheduler scheduler{Scheduler::Other};
        int priority{0};
        std::optional<int> cpu;
        bool required{true};
    };

    // Opaque implementation type; its definition remains private to the library.
    struct Impl;
    explicit PeriodicTask(Options options);
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
    bool running() const noexcept;
    Stats stats() const noexcept;

private:
    Result<void> start_impl(std::function<void(const CycleInfo&)> callback);
    std::unique_ptr<Impl> impl_;
};

}  // namespace realtime
