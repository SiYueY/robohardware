#include <realtime/current_thread.hpp>

#include <cerrno>
#include <pthread.h>
#include <sched.h>

namespace realtime {
namespace {

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

hardware::Result<void, Error> set_current_thread_scheduling(
    SchedulingPolicy policy, int priority) noexcept {
    const int native = native_policy(policy);
    if (native == -1) {
        return hardware::Result<void, Error>::failure(Error::InvalidArgument);
    }

    if (policy == SchedulingPolicy::Normal) {
        if (priority != 0) {
            return hardware::Result<void, Error>::failure(Error::InvalidArgument);
        }
    } else {
        errno = 0;
        const int minimum = sched_get_priority_min(native);
        if (minimum == -1) {
            return hardware::Result<void, Error>::failure(Error::System);
        }

        errno = 0;
        const int maximum = sched_get_priority_max(native);
        if (maximum == -1) {
            return hardware::Result<void, Error>::failure(Error::System);
        }

        if (priority < minimum || priority > maximum) {
            return hardware::Result<void, Error>::failure(Error::InvalidArgument);
        }
    }

    sched_param parameters{};
    parameters.sched_priority = priority;
    const int error = pthread_setschedparam(pthread_self(), native, &parameters);
    return error == 0 ? hardware::Result<void, Error>::success()
                      : hardware::Result<void, Error>::failure(
                            error == EPERM ? Error::PermissionDenied : Error::System);
}

hardware::Result<void, Error> set_current_thread_affinity(unsigned int cpu_index) noexcept {
    if (cpu_index >= CPU_SETSIZE) {
        return hardware::Result<void, Error>::failure(Error::InvalidArgument);
    }

    cpu_set_t set{};
    CPU_ZERO(&set);
    CPU_SET(cpu_index, &set);
    const int error = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    return error == 0 ? hardware::Result<void, Error>::success()
                      : hardware::Result<void, Error>::failure(
                            error == EPERM ? Error::PermissionDenied : Error::System);
}

}  // namespace realtime
