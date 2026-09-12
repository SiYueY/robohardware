#include <realtime/periodic_schedule.hpp>

#include "monotonic_time.hpp"

#include <cerrno>
#include <ctime>
#include <cstdlib>
#include <limits>
#include <system_error>

namespace realtime {
namespace {

constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;

[[nodiscard]] PeriodicWaitResult failed_wait(std::error_code error) noexcept {
  PeriodicWaitResult result{};
  result.error = error;
  return result;
}

[[nodiscard]] bool checked_add(
    Duration::rep left,
    Duration::rep right,
    Duration::rep& result) noexcept {
  if ((right > 0 && left > std::numeric_limits<Duration::rep>::max() - right) ||
      (right < 0 && left < std::numeric_limits<Duration::rep>::min() - right)) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool to_timespec(TimePoint value, timespec& result) noexcept {
  const auto nanoseconds = value.time_since_epoch().count();
  if (nanoseconds < 0) {
    return false;
  }

  result.tv_sec = static_cast<time_t>(nanoseconds / kNanosecondsPerSecond);
  result.tv_nsec = static_cast<long>(nanoseconds % kNanosecondsPerSecond);
  return static_cast<Duration::rep>(result.tv_sec) ==
         nanoseconds / kNanosecondsPerSecond;
}

[[nodiscard]] bool calculate_next_release(
    TimePoint scheduled_time,
    TimePoint wake_time,
    Duration period,
    MissedPeriodPolicy policy,
    std::uint64_t& missed_releases,
    TimePoint& next_release) noexcept {
  const auto scheduled = scheduled_time.time_since_epoch().count();
  const auto wake = wake_time.time_since_epoch().count();
  const auto period_count = period.count();

  if (wake < scheduled || period_count <= 0) {
    return false;
  }

  if (scheduled < 0 && wake > std::numeric_limits<Duration::rep>::max() + scheduled) {
    return false;
  }

  const auto elapsed = wake - scheduled;
  missed_releases = static_cast<std::uint64_t>(elapsed / period_count);

  Duration::rep next_count{};
  switch (policy) {
    case MissedPeriodPolicy::CatchUp:
      if (!checked_add(scheduled, period_count, next_count)) {
        return false;
      }
      break;
    case MissedPeriodPolicy::SkipMissed: {
      const auto remainder = elapsed % period_count;
      const auto advance = period_count - remainder;
      if (!checked_add(wake, advance, next_count)) {
        return false;
      }
      break;
    }
    default:
      std::abort();
  }

  next_release = TimePoint{Duration{next_count}};
  return true;
}

}  // namespace

std::error_code PeriodicSchedule::configure(
    TimePoint first_release,
    Duration period,
    MissedPeriodPolicy policy) noexcept {
  if (period <= Duration::zero()) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  switch (policy) {
    case MissedPeriodPolicy::CatchUp:
    case MissedPeriodPolicy::SkipMissed:
      break;
    default:
      return std::make_error_code(std::errc::invalid_argument);
  }

  next_release_ = first_release;
  period_ = period;
  policy_ = policy;
  configured_ = true;
  return {};
}

bool PeriodicSchedule::is_configured() const noexcept { return configured_; }

PeriodicWaitResult PeriodicSchedule::wait_next() noexcept {
  if (!configured_) {
    return failed_wait(std::make_error_code(std::errc::operation_not_permitted));
  }

  const TimePoint scheduled_time = next_release_;
  const Duration period = period_;
  const MissedPeriodPolicy policy = policy_;
  TimePoint wake_time = Clock::now();

  if (wake_time < scheduled_time) {
    timespec target{};
    if (!to_timespec(scheduled_time, target)) {
      return failed_wait(std::make_error_code(std::errc::value_too_large));
    }

    int sleep_error = 0;
    do {
      sleep_error = monotonic_time::sleep_until(target);
    } while (sleep_error == EINTR);

    if (sleep_error != 0) {
      return failed_wait(std::error_code{sleep_error, std::system_category()});
    }

    wake_time = Clock::now();
    if (wake_time < scheduled_time) {
      std::abort();
    }
  }

  std::uint64_t missed_releases{};
  TimePoint next_release{};
  if (!calculate_next_release(
          scheduled_time, wake_time, period, policy, missed_releases, next_release)) {
    return failed_wait(std::make_error_code(std::errc::value_too_large));
  }

  PeriodicWaitResult result{};
  result.scheduled_time = scheduled_time;
  result.wake_time = wake_time;
  result.period = period;
  result.lateness = wake_time - scheduled_time;
  result.missed_releases = missed_releases;

  next_release_ = next_release;
  return result;
}

}  // namespace realtime
