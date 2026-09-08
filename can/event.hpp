#pragma once

#include "can/frame.hpp"

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
    // SocketCAN error-class bits without CAN_ERR_FLAG. EventType is the stable
    // semantic classification; detail preserves the source diagnostic facts.
    std::uint32_t detail{0};
    // Reserved for a native diagnostic code when the kernel defines one.
    // SocketCAN V1 error-frame decoding currently leaves it at zero.
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
