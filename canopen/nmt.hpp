#pragma once
#include <cstdint>
namespace canopen {
enum class NmtState : std::uint8_t {
    Initializing = 0,
    Stopped = 4,
    Operational = 5,
    PreOperational = 127
};
enum class NmtCommand : std::uint8_t {
    Start = 1,
    Stop = 2,
    EnterPreOperational = 128,
    ResetNode = 129,
    ResetCommunication = 130
};
}  // namespace canopen
