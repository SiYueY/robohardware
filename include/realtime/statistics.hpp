#pragma once

#include "realtime/clock.hpp"

#include <cstdint>

namespace realtime {
struct Stats {
    std::uint64_t cycles{0};
    std::uint64_t deadline_misses{0};
    std::uint64_t missed_periods{0};
    Duration min_lateness{};
    Duration max_lateness{};
    Duration min_execution{};
    Duration max_execution{};
};
}  // namespace realtime
