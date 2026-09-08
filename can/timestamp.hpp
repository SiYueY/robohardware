#pragma once

#include <chrono>

namespace can {

using Timestamp = std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>;

struct RxInfo {
    Timestamp received_at{};
};

}  // namespace can
