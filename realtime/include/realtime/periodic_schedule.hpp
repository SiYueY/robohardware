#pragma once

#include <realtime/clock.hpp>

#include <cstdint>
#include <system_error>

namespace realtime {

enum class MissedPeriodPolicy {
  CatchUp,
  SkipMissed,
};

struct PeriodicWaitResult final {
  std::error_code error{};

  TimePoint scheduled_time{};
  TimePoint wake_time{};
  Duration period{};
  Duration lateness{};
  std::uint64_t missed_releases{0};

  [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class PeriodicSchedule final {
 public:
  PeriodicSchedule() noexcept = default;

  PeriodicSchedule(const PeriodicSchedule&) = delete;
  PeriodicSchedule& operator=(const PeriodicSchedule&) = delete;
  PeriodicSchedule(PeriodicSchedule&&) = delete;
  PeriodicSchedule& operator=(PeriodicSchedule&&) = delete;

  [[nodiscard]] std::error_code configure(
      TimePoint first_release,
      Duration period,
      MissedPeriodPolicy policy) noexcept;

  [[nodiscard]] bool is_configured() const noexcept;
  [[nodiscard]] PeriodicWaitResult wait_next() noexcept;

 private:
  TimePoint next_release_{};
  Duration period_{};
  MissedPeriodPolicy policy_{MissedPeriodPolicy::CatchUp};
  bool configured_{false};
};

}  // namespace realtime
