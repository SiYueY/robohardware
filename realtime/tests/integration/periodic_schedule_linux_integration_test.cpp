#include <realtime/clock.hpp>
#include <realtime/periodic_schedule.hpp>

#include <iostream>

int main() {
  using realtime::Clock;
  using realtime::Duration;
  using realtime::MissedPeriodPolicy;
  using realtime::PeriodicSchedule;

  constexpr Duration period{1'000'000};
  PeriodicSchedule schedule;
  const auto first_release = Clock::now() + period;
  const auto configuration =
      schedule.configure(first_release, period, MissedPeriodPolicy::CatchUp);
  if (configuration) {
    std::cerr << "periodic schedule configuration failed: " << configuration.message() << '\n';
    return 1;
  }

  const auto result = schedule.wait_next();
  if (!result) {
    std::cerr << "periodic schedule wait failed: " << result.error.message() << '\n';
    return 1;
  }
  if (result.scheduled_time != first_release || result.period != period ||
      result.wake_time < result.scheduled_time || result.lateness < Duration::zero()) {
    std::cerr << "periodic schedule returned an invalid monotonic observation\n";
    return 1;
  }

  return 0;
}
