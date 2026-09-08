#include "realtime/affinity.hpp"

#include <cerrno>
#include <pthread.h>
#include <sched.h>

namespace realtime {
Result<void> set_affinity(int cpu) noexcept {
    if (cpu < 0 || cpu >= CPU_SETSIZE) return Error{ErrorCode::InvalidArgument};
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    const int result = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (result == 0) return {};
    return Error{
        result == EPERM ? ErrorCode::PermissionDenied : ErrorCode::AffinityFailed,
        NativeErrorDomain::Pthread, result};
}
}  // namespace realtime
