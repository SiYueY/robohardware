#include <realtime/clock.hpp>

#include "monotonic_time.hpp"

#include <ctime>
#include <cstdlib>
#include <limits>

namespace realtime {
namespace {

[[nodiscard]] std::int64_t to_nanoseconds(const timespec& value) noexcept {
  constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;

  if (value.tv_sec < 0 || value.tv_nsec < 0 ||
      value.tv_nsec >= kNanosecondsPerSecond ||
      value.tv_sec > std::numeric_limits<std::int64_t>::max() /
                         kNanosecondsPerSecond) {
    std::abort();
  }

  return static_cast<std::int64_t>(value.tv_sec) * kNanosecondsPerSecond +
         static_cast<std::int64_t>(value.tv_nsec);
}

}  // namespace

Clock::time_point Clock::now() noexcept {
  timespec value{};
  if (monotonic_time::read(value) != 0) {
    std::abort();
  }

  return time_point{duration{to_nanoseconds(value)}};
}

}  // namespace realtime
