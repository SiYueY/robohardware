#pragma once

#include "can/timestamp.hpp"

#include <cstdint>

namespace can {

enum class State : std::uint8_t {
    Unknown,
    Active,
    Warning,
    Passive,
    BusOff,
};

enum class EventType : std::uint8_t {
    Warning,
    Passive,
    BusOff,
    Restarted,
    ArbitrationLost,
    ControllerError,
    ProtocolError,
    RxOverflow,
    Unknown,
};

struct Event {
    EventType type{EventType::Unknown};
    Timestamp timestamp{};
    std::uint32_t detail{0};
    int native_code{0};
};

struct Stats {
    std::uint64_t rx_frames{0};
    std::uint64_t tx_frames{0};
    std::uint64_t rx_errors{0};
    std::uint64_t tx_errors{0};
    std::uint64_t error_frames{0};
    std::uint64_t rx_overruns{0};
    std::uint64_t dropped_events{0};
};

}  // namespace can
