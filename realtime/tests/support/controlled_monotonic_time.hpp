#pragma once

#include <realtime/clock.hpp>

#include <cstddef>
#include <initializer_list>

namespace realtime::test::controlled_monotonic_time {

void reset() noexcept;
void set_read_times(std::initializer_list<TimePoint> values);
void set_sleep_results(std::initializer_list<int> values);

[[nodiscard]] std::size_t sleep_call_count() noexcept;
[[nodiscard]] TimePoint sleep_target(std::size_t index) noexcept;

}  // namespace realtime::test::controlled_monotonic_time
