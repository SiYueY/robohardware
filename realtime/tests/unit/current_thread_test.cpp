#include <realtime/current_thread.hpp>

#include <iostream>
#include <limits>
#include <system_error>

int main() {
  using realtime::SchedulingPolicy;
  using realtime::set_current_thread_affinity;
  using realtime::set_current_thread_scheduling;

  const auto invalid_policy = set_current_thread_scheduling(
      static_cast<SchedulingPolicy>(-1), 0);
  const auto invalid_normal = set_current_thread_scheduling(SchedulingPolicy::Normal, 1);
  const auto invalid_cpu = set_current_thread_affinity(std::numeric_limits<unsigned int>::max());

  if (invalid_policy != std::errc::invalid_argument ||
      invalid_normal != std::errc::invalid_argument ||
      invalid_cpu != std::errc::invalid_argument) {
    std::cerr << "invalid current-thread configuration was not rejected\n";
    return 1;
  }

  return 0;
}
