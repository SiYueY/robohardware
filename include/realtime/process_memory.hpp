#pragma once

#include <hardware/result.hpp>
#include <realtime/error.hpp>

namespace realtime {

[[nodiscard]] hardware::Result<void, Error> lock_process_memory() noexcept;

}  // namespace realtime
