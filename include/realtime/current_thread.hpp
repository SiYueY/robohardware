#pragma once

#include <hardware/result.hpp>
#include <realtime/error.hpp>

namespace realtime {

enum class SchedulingPolicy {
    Normal,
    Fifo,
    RoundRobin,
};

[[nodiscard]] hardware::Result<void, Error> set_current_thread_scheduling(
    SchedulingPolicy policy, int priority) noexcept;

[[nodiscard]] hardware::Result<void, Error> set_current_thread_affinity(
    unsigned int cpu_index) noexcept;

}  // namespace realtime
