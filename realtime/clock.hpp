#pragma once

#include <chrono>
#include <cstdint>

namespace realtime {

class Clock {
public:
    using rep = std::int64_t;
    using period = std::nano;
    using duration = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<Clock, duration>;

    static constexpr bool is_steady = true;
    static time_point now() noexcept;
};

using TimePoint = Clock::time_point;
using Duration = Clock::duration;

}  // namespace realtime
