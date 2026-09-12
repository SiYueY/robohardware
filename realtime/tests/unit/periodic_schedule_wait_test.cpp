#include <realtime/periodic_schedule.hpp>

#include "controlled_monotonic_time.hpp"

#include <cerrno>
#include <iostream>
#include <limits>
#include <system_error>

namespace {

using realtime::Duration;
using realtime::MissedPeriodPolicy;
using realtime::PeriodicSchedule;
using realtime::TimePoint;

bool expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "periodic schedule wait test failed: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using realtime::test::controlled_monotonic_time::reset;
  using realtime::test::controlled_monotonic_time::set_read_times;
  using realtime::test::controlled_monotonic_time::set_sleep_results;
  using realtime::test::controlled_monotonic_time::sleep_call_count;
  using realtime::test::controlled_monotonic_time::sleep_target;

  bool passed = true;

  reset();
  set_read_times({TimePoint{Duration{50}}, TimePoint{Duration{100}}});
  set_sleep_results({EINTR, EINTR, 0});

  PeriodicSchedule schedule;
  passed &= expect(!schedule.configure(
                       TimePoint{Duration{100}}, Duration{10}, MissedPeriodPolicy::CatchUp),
                   "CatchUp configuration failed");
  const auto waited = schedule.wait_next();
  passed &= expect(waited && waited.scheduled_time == TimePoint{Duration{100}} &&
                       waited.wake_time == TimePoint{Duration{100}} &&
                       waited.missed_releases == 0,
                   "EINTR wait did not return the scheduled observation");
  passed &= expect(sleep_call_count() == 3,
                   "EINTR did not retry the absolute wait");
  passed &= expect(sleep_target(0) == TimePoint{Duration{100}} &&
                       sleep_target(1) == TimePoint{Duration{100}} &&
                       sleep_target(2) == TimePoint{Duration{100}},
                   "EINTR retry changed the absolute target");

  reset();
  set_read_times({TimePoint{Duration{100}},
                  TimePoint{Duration{119}},
                  TimePoint{Duration{130}},
                  TimePoint{Duration{150}}});

  PeriodicSchedule catching_up;
  passed &= expect(!catching_up.configure(
                       TimePoint{Duration{100}}, Duration{10}, MissedPeriodPolicy::CatchUp),
                   "CatchUp progression configuration failed");
  const auto on_time = catching_up.wait_next();
  const auto just_late = catching_up.wait_next();
  const auto one_period_late = catching_up.wait_next();
  const auto multiple_periods_late = catching_up.wait_next();
  passed &= expect(on_time && on_time.lateness == Duration::zero() &&
                       on_time.missed_releases == 0 && on_time.period == Duration{10},
                   "on-time CatchUp observation is wrong");
  passed &= expect(just_late && just_late.scheduled_time == TimePoint{Duration{110}} &&
                       just_late.lateness == Duration{9} && just_late.missed_releases == 0,
                   "sub-period CatchUp lateness is wrong");
  passed &= expect(one_period_late && one_period_late.scheduled_time == TimePoint{Duration{120}} &&
                       one_period_late.lateness == Duration{10} &&
                       one_period_late.missed_releases == 1,
                   "one-period CatchUp lateness is wrong");
  passed &= expect(
      multiple_periods_late &&
          multiple_periods_late.scheduled_time == TimePoint{Duration{130}} &&
          multiple_periods_late.lateness == Duration{20} &&
          multiple_periods_late.missed_releases == 2,
      "CatchUp did not preserve its anchored release grid while late");
  passed &= expect(sleep_call_count() == 0,
                   "past or equal CatchUp target attempted an absolute sleep");

  reset();
  set_read_times({TimePoint{Duration{135}}, TimePoint{Duration{140}}});

  PeriodicSchedule skipping;
  passed &= expect(!skipping.configure(
                       TimePoint{Duration{100}}, Duration{10}, MissedPeriodPolicy::SkipMissed),
                   "SkipMissed configuration failed");
  const auto first = skipping.wait_next();
  const auto second = skipping.wait_next();
  passed &= expect(first && first.lateness == Duration{35} && first.missed_releases == 3,
                   "SkipMissed count at release boundary is wrong");
  passed &= expect(second && second.scheduled_time == TimePoint{Duration{140}},
                   "SkipMissed did not advance to the first release after wake time");
  passed &= expect(sleep_call_count() == 0,
                   "past schedule target attempted an absolute sleep");

  reset();
  set_read_times(
      {TimePoint{Duration{50}}, TimePoint{Duration{50}}, TimePoint{Duration{100}}});
  set_sleep_results({EIO, 0});

  PeriodicSchedule retry_after_sleep_failure;
  passed &= expect(
      !retry_after_sleep_failure.configure(
          TimePoint{Duration{100}}, Duration{10}, MissedPeriodPolicy::CatchUp),
      "sleep failure retry configuration failed");
  const auto sleep_failure = retry_after_sleep_failure.wait_next();
  const auto retry_success = retry_after_sleep_failure.wait_next();
  passed &= expect(
      sleep_failure.error == std::error_code{EIO, std::system_category()},
      "unexpected sleep error was not returned as a system error");
  passed &= expect(
      retry_success && retry_success.scheduled_time == TimePoint{Duration{100}},
      "sleep failure advanced the schedule target");
  passed &= expect(
      sleep_call_count() == 2 && sleep_target(0) == TimePoint{Duration{100}} &&
          sleep_target(1) == TimePoint{Duration{100}},
      "sleep failure retry did not retain the absolute target");

  reset();
  const auto maximum_time =
      TimePoint{Duration{std::numeric_limits<Duration::rep>::max()}};
  set_read_times({maximum_time, maximum_time});

  PeriodicSchedule overflow;
  passed &= expect(!overflow.configure(maximum_time, Duration{1}, MissedPeriodPolicy::CatchUp),
                   "overflow configuration failed");
  const auto first_overflow = overflow.wait_next();
  const auto second_overflow = overflow.wait_next();
  const auto value_too_large = std::make_error_code(std::errc::value_too_large);
  passed &= expect(first_overflow.error == value_too_large &&
                       second_overflow.error == value_too_large,
                   "next-release overflow was not reported");

  reset();
  set_read_times({TimePoint{Duration{100}}});
  passed &= expect(!overflow.configure(
                       TimePoint{Duration{100}}, Duration{10}, MissedPeriodPolicy::CatchUp),
                   "reconfiguration after overflow failed");
  const auto recovered = overflow.wait_next();
  passed &= expect(recovered && recovered.scheduled_time == TimePoint{Duration{100}},
                   "reconfiguration did not recover from overflow failure");

  return passed ? 0 : 1;
}
