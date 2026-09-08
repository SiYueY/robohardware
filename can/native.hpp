#pragma once

// This is an implementation-only header shared by SocketCAN source and its unit tests.
// It is intentionally not part of the documented can public API.

#include "can/error.hpp"
#include "can/event.hpp"
#include "can/filter.hpp"
#include "can/frame.hpp"

#include <linux/can.h>

namespace can::native {

Result<void> validate(const Frame& frame) noexcept;
Result<void> validate(const Filter& filter) noexcept;
::can_frame to_native(const Frame& frame) noexcept;
Result<Frame> from_native(const ::can_frame& frame) noexcept;
::can_filter to_native(const Filter& filter) noexcept;
Event decode_error_frame(const ::can_frame& frame, Timestamp timestamp) noexcept;
State apply_event(State state, const Event& event) noexcept;
void update_stats(Stats& stats, const Event& event) noexcept;

}  // namespace can::native
