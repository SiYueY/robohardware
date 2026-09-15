#pragma once
#include <cstdint>
namespace spi {
enum class Error : std::uint8_t {
    InvalidArgument,
    AlreadyOpen,
    NotOpen,
    Unsupported,
    ConfigurationMismatch,
    PermissionDenied,
    DeviceNotFound,
    Busy,
    Io
};
}
