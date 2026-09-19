#pragma once

#include <cstdint>

namespace serial {

/**
 * @brief Stable domain errors reported by the Serial module.
 *
 * Native operating-system failures are translated inside the implementation;
 * the public API never exposes native error numbers.
 */
enum class Error : std::uint8_t {
    InvalidArgument,   ///< An argument or configuration value is invalid.
    InvalidState,      ///< The operation is invalid in the Port's current state.
    AlreadyOpen,       ///< The Port already owns an open TTY.
    NotOpen,           ///< The Port does not own an open TTY.
    WouldBlock,        ///< An immediate operation cannot make progress.
    TimedOut,          ///< The operation's deadline expired without progress.
    Unsupported,       ///< The requested capability is not supported.
    PermissionDenied,  ///< Access was denied by the operating system.
    DeviceNotFound,    ///< The requested serial device does not exist.
    NotTerminal,       ///< The path does not refer to a terminal device.
    Disconnected,      ///< The opened device became unavailable.
    Busy,              ///< The device is already in use.
    OutOfMemory,       ///< Required control-plane allocation failed.
    Io,                ///< An unclassified I/O failure occurred.
};
}  // namespace serial
