#pragma once

#include "realtime/scheduler.hpp"

#include <optional>

namespace realtime {
enum class RealtimeMode { Required, BestEffort };

struct Status {
    bool running{false};
    bool realtime_scheduling{false};
    bool memory_locked{false};
    bool affinity_applied{false};
    SchedulingPolicy policy{SchedulingPolicy::Other};
    int priority{0};
    std::optional<int> cpu;
};
}  // namespace realtime
