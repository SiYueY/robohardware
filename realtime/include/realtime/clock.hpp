#pragma once

#include <chrono>
#include <cstdint>

namespace realtime {

struct Clock final {
  using rep = std::int64_t;
  using period = std::nano;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<Clock>;

  static constexpr bool is_steady = true;

  [[nodiscard]] static time_point now() noexcept;
};

using Duration = Clock::duration;
using TimePoint = Clock::time_point;

}  // namespace realtime
