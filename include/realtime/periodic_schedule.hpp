#pragma once

#include <realtime/clock.hpp>

#include <cstdint>
#include <hardware/result.hpp>
#include <realtime/error.hpp>

namespace realtime {

enum class MissedPeriodPolicy {
    CatchUp,
    SkipMissed,
};

struct PeriodicWait final {
    TimePoint scheduled_time{};
    TimePoint wake_time{};
    Duration period{};
    Duration lateness{};
    std::uint64_t missed_releases{0};
};

class PeriodicSchedule final {
public:
    PeriodicSchedule() noexcept = default;

    PeriodicSchedule(const PeriodicSchedule&) = delete;
    PeriodicSchedule& operator=(const PeriodicSchedule&) = delete;
    PeriodicSchedule(PeriodicSchedule&&) = delete;
    PeriodicSchedule& operator=(PeriodicSchedule&&) = delete;

    [[nodiscard]] hardware::Result<void, Error> configure(
        TimePoint first_release, Duration period, MissedPeriodPolicy policy) noexcept;

    [[nodiscard]] bool is_configured() const noexcept;
    [[nodiscard]] hardware::Result<PeriodicWait, Error> wait_next() noexcept;

private:
    TimePoint next_release_{};
    Duration period_{};
    MissedPeriodPolicy policy_{MissedPeriodPolicy::CatchUp};
    bool configured_{false};
};

}  // namespace realtime
