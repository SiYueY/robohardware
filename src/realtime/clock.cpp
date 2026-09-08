#include "realtime/clock.hpp"

#include <ctime>

namespace realtime {
Clock::time_point Clock::now() noexcept {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return time_point{};
    return time_point{std::chrono::seconds(value.tv_sec) + std::chrono::nanoseconds(value.tv_nsec)};
}
}  // namespace realtime
