#pragma once

#include "realtime/error.hpp"

namespace realtime {

/// Pins the calling thread to `cpu`.
Result<void> set_affinity(int cpu) noexcept;

}  // namespace realtime
