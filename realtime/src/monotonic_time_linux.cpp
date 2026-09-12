#include "monotonic_time.hpp"

#include <cerrno>
#include <ctime>

namespace realtime::monotonic_time {

int read(timespec& value) noexcept {
  if (clock_gettime(CLOCK_MONOTONIC, &value) == 0) {
    return 0;
  }
  return errno;
}

int sleep_until(const timespec& target) noexcept {
  return clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, nullptr);
}

}  // namespace realtime::monotonic_time
