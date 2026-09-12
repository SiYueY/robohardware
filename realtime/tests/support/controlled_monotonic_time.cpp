#include "controlled_monotonic_time.hpp"

#include "monotonic_time.hpp"

#include <cerrno>
#include <cstdint>
#include <vector>

namespace realtime::test::controlled_monotonic_time {
namespace {

constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;

std::vector<TimePoint> read_times;
std::size_t read_index{0};
std::vector<int> sleep_results;
std::size_t sleep_result_index{0};
std::vector<TimePoint> sleep_targets;

[[nodiscard]] timespec to_timespec(TimePoint value) noexcept {
  const auto count = value.time_since_epoch().count();
  return {static_cast<time_t>(count / kNanosecondsPerSecond),
          static_cast<long>(count % kNanosecondsPerSecond)};
}

[[nodiscard]] TimePoint from_timespec(const timespec& value) noexcept {
  return TimePoint{Duration{static_cast<std::int64_t>(value.tv_sec) *
                                 kNanosecondsPerSecond +
                             static_cast<std::int64_t>(value.tv_nsec)}};
}

}  // namespace

void reset() noexcept {
  read_times.clear();
  read_index = 0;
  sleep_results.clear();
  sleep_result_index = 0;
  sleep_targets.clear();
}

void set_read_times(std::initializer_list<TimePoint> values) {
  read_times.assign(values);
  read_index = 0;
}

void set_sleep_results(std::initializer_list<int> values) {
  sleep_results.assign(values);
  sleep_result_index = 0;
}

std::size_t sleep_call_count() noexcept { return sleep_targets.size(); }

TimePoint sleep_target(std::size_t index) noexcept { return sleep_targets.at(index); }

}  // namespace realtime::test::controlled_monotonic_time

namespace realtime::monotonic_time {

int read(timespec& value) noexcept {
  using namespace test::controlled_monotonic_time;
  if (read_index == read_times.size()) {
    return EIO;
  }
  value = to_timespec(read_times[read_index]);
  ++read_index;
  return 0;
}

int sleep_until(const timespec& target) noexcept {
  using namespace test::controlled_monotonic_time;
  sleep_targets.push_back(from_timespec(target));
  if (sleep_result_index == sleep_results.size()) {
    return 0;
  }
  const int result = sleep_results[sleep_result_index];
  ++sleep_result_index;
  return result;
}

}  // namespace realtime::monotonic_time
