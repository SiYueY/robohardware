#pragma once

#include <cstdint>

namespace serial {

enum class Error : std::uint8_t {
    InvalidArgument,
    InvalidState,
    AlreadyOpen,
    NotOpen,
    WouldBlock,
    TimedOut,
    Unsupported,
    PermissionDenied,
    DeviceNotFound,
    NotTerminal,
    Disconnected,
    Busy,
    OutOfMemory,
    Io,
};
}  // namespace serial
