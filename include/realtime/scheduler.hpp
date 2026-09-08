#pragma once

namespace realtime {

enum class SchedulingPolicy { Other, Fifo };

struct SchedulerConfig {
    SchedulingPolicy policy{SchedulingPolicy::Fifo};
    int priority{0};
};

}  // namespace realtime
