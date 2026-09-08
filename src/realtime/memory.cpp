#include "realtime/memory.hpp"

#include <cerrno>
#include <sys/mman.h>

namespace realtime {
Result<void> lock_memory() noexcept {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) return {};
    const int code = errno;
    return Error{
        (code == EPERM || code == EACCES) ? ErrorCode::PermissionDenied
                                          : ErrorCode::MemoryLockFailed,
        NativeErrorDomain::Errno, code};
}
}  // namespace realtime
