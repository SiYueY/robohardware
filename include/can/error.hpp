#pragma once
#include <cstdint>
namespace can {
enum class Error : std::uint8_t {
    InvalidArgument,
    AlreadyOpen,
    NotOpen,
    TimedOut,
    PermissionDenied,
    InterfaceNotFound,
    Busy,
    Disconnected,
    Io
};
}
