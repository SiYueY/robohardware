#pragma once

#include <cstdint>

namespace serial {

enum class Error : std::uint8_t {
    InvalidArgument,
    AlreadyOpen,
    NotOpen,
    TimedOut,
    Unsupported,
    PermissionDenied,
    DeviceNotFound,
    Disconnected,
    Busy,
    Io,
};
}  // namespace serial
