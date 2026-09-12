#include <realtime/process_memory.hpp>

#include <cerrno>
#include <sys/mman.h>
#include <system_error>

namespace realtime {

std::error_code lock_process_memory() noexcept {
  if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
    return {};
  }
  return {errno, std::system_category()};
}

}  // namespace realtime
