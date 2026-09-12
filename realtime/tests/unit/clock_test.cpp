#include <realtime/clock.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <type_traits>

namespace {

bool expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "clock test failed: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using realtime::Clock;
  using realtime::Duration;
  using realtime::TimePoint;

  static_assert(std::is_same_v<Clock::rep, std::int64_t>);
  static_assert(std::is_same_v<Clock::period, std::nano>);
  static_assert(std::is_same_v<Clock::duration, Duration>);
  static_assert(std::is_same_v<Clock::time_point, TimePoint>);
  static_assert(Clock::is_steady);

  const TimePoint first = Clock::now();
  const TimePoint second = Clock::now();

  return expect(second >= first, "monotonic clock moved backwards") ? 0 : 1;
}
