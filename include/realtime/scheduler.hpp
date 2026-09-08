#pragma once

#include "realtime/error.hpp"

namespace realtime {

enum class Scheduler { Other, Fifo };

/// Sets the scheduling policy of the calling thread.
/// `Scheduler::Fifo` requires a valid FIFO priority and may require privileges.
Result<void> set_scheduler(Scheduler scheduler, int priority = 0) noexcept;

}  // namespace realtime
