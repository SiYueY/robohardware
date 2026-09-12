#pragma once

#include <ctime>

namespace realtime::monotonic_time {

[[nodiscard]] int read(timespec& value) noexcept;
[[nodiscard]] int sleep_until(const timespec& target) noexcept;

}  // namespace realtime::monotonic_time
