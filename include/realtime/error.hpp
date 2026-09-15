#pragma once
#include <cstdint>
namespace realtime {
enum class Error : std::uint8_t {
    InvalidArgument,
    NotConfigured,
    PermissionDenied,
    ResourceUnavailable,
    ValueOverflow,
    Unsupported,
    System
};
}
