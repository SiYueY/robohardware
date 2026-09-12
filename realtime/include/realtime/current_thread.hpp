#pragma once

#include <system_error>

namespace realtime {

enum class SchedulingPolicy {
  Normal,
  Fifo,
  RoundRobin,
};

[[nodiscard]] std::error_code set_current_thread_scheduling(
    SchedulingPolicy policy,
    int priority) noexcept;

[[nodiscard]] std::error_code set_current_thread_affinity(
    unsigned int cpu_index) noexcept;

}  // namespace realtime
