#include <realtime/current_thread.hpp>

#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <system_error>

namespace realtime {
namespace {

[[nodiscard]] std::error_code invalid_argument() noexcept {
  return std::make_error_code(std::errc::invalid_argument);
}

[[nodiscard]] int native_policy(SchedulingPolicy policy) noexcept {
  switch (policy) {
    case SchedulingPolicy::Normal:
      return SCHED_OTHER;
    case SchedulingPolicy::Fifo:
      return SCHED_FIFO;
    case SchedulingPolicy::RoundRobin:
      return SCHED_RR;
    default:
      return -1;
  }
}

}  // namespace

std::error_code set_current_thread_scheduling(
    SchedulingPolicy policy,
    int priority) noexcept {
  const int native = native_policy(policy);
  if (native == -1) {
    return invalid_argument();
  }

  if (policy == SchedulingPolicy::Normal) {
    if (priority != 0) {
      return invalid_argument();
    }
  } else {
    errno = 0;
    const int minimum = sched_get_priority_min(native);
    if (minimum == -1) {
      return {errno, std::system_category()};
    }

    errno = 0;
    const int maximum = sched_get_priority_max(native);
    if (maximum == -1) {
      return {errno, std::system_category()};
    }

    if (priority < minimum || priority > maximum) {
      return invalid_argument();
    }
  }

  sched_param parameters{};
  parameters.sched_priority = priority;
  const int error = pthread_setschedparam(pthread_self(), native, &parameters);
  return error == 0 ? std::error_code{} : std::error_code{error, std::system_category()};
}

std::error_code set_current_thread_affinity(unsigned int cpu_index) noexcept {
  if (cpu_index >= CPU_SETSIZE) {
    return invalid_argument();
  }

  cpu_set_t set{};
  CPU_ZERO(&set);
  CPU_SET(cpu_index, &set);
  const int error = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  return error == 0 ? std::error_code{} : std::error_code{error, std::system_category()};
}

}  // namespace realtime
