#pragma once

#include <system_error>

namespace realtime {

[[nodiscard]] std::error_code lock_process_memory() noexcept;

}  // namespace realtime
