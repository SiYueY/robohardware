#include "realtime/scheduler.hpp"

#include <cerrno>
#include <pthread.h>
#include <sched.h>

namespace realtime {
namespace {
Error pthread_error(ErrorCode code, int native_code) noexcept {
    return {
        native_code == EPERM ? ErrorCode::PermissionDenied : code, NativeErrorDomain::Pthread,
        native_code};
}
}  // namespace

Result<void> set_scheduler(Scheduler scheduler, int priority) noexcept {
    int policy = SCHED_OTHER;
    if (scheduler == Scheduler::Other) {
        if (priority != 0) return Error{ErrorCode::InvalidArgument};
    } else if (scheduler == Scheduler::Fifo) {
        const int minimum = sched_get_priority_min(SCHED_FIFO);
        const int maximum = sched_get_priority_max(SCHED_FIFO);
        if (minimum == -1 || maximum == -1 || priority < minimum || priority > maximum)
            return Error{ErrorCode::InvalidArgument};
        policy = SCHED_FIFO;
    } else {
        return Error{ErrorCode::InvalidArgument};
    }
    sched_param param{};
    param.sched_priority = priority;
    const int result = pthread_setschedparam(pthread_self(), policy, &param);
    if (result == 0) return {};
    return pthread_error(ErrorCode::SchedulingFailed, result);
}
}  // namespace realtime
