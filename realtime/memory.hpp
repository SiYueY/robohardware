#pragma once

#include "realtime/error.hpp"

namespace realtime {
/// Locks current and future process memory with mlockall().
Result<void> lock_memory() noexcept;
}  // namespace realtime
