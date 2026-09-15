#include <realtime/process_memory.hpp>

#include <cerrno>
#include <sys/mman.h>

namespace realtime {

hardware::Result<void, Error> lock_process_memory() noexcept {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        return hardware::Result<void, Error>::success();
    }
    return hardware::Result<void, Error>::failure(
        errno == EPERM ? Error::PermissionDenied : Error::System);
}

}  // namespace realtime
