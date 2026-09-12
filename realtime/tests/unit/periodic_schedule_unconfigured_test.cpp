#include <realtime/periodic_schedule.hpp>

#include <iostream>
#include <system_error>

int main() {
  realtime::PeriodicSchedule schedule;
  const auto first = schedule.wait_next();
  const auto second = schedule.wait_next();

  if (first || second || first.error != std::errc::operation_not_permitted ||
      second.error != std::errc::operation_not_permitted ||
      schedule.is_configured()) {
    std::cerr << "unconfigured schedule did not return stable operation_not_permitted\n";
    return 1;
  }

  return 0;
}
