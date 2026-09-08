#include "can/event.hpp"

#include <linux/can.h>
#include <linux/can/error.h>

namespace {

[[maybe_unused]] can::Event decode_error_frame(
    const ::can_frame& frame, can::Timestamp timestamp) noexcept {
    const std::uint32_t flags = frame.can_id & CAN_ERR_MASK;
    can::Event event{can::EventType::Unknown, timestamp, flags, 0};

    if ((flags & CAN_ERR_BUSOFF) != 0U) {
        event.type = can::EventType::BusOff;
    } else if ((flags & CAN_ERR_RESTARTED) != 0U) {
        event.type = can::EventType::Restarted;
    } else if (
        (flags & CAN_ERR_CRTL) != 0U &&
        (frame.data[1] & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE)) != 0U) {
        event.type = can::EventType::Passive;
    } else if (
        (flags & CAN_ERR_CRTL) != 0U &&
        (frame.data[1] & (CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING)) != 0U) {
        event.type = can::EventType::Warning;
    } else if ((flags & CAN_ERR_LOSTARB) != 0U) {
        event.type = can::EventType::ArbitrationLost;
    } else if ((flags & CAN_ERR_PROT) != 0U) {
        event.type = can::EventType::ProtocolError;
    } else if ((flags & (CAN_ERR_CRTL | CAN_ERR_TRX | CAN_ERR_TX_TIMEOUT)) != 0U) {
        event.type = can::EventType::ControllerError;
    }
    return event;
}

[[maybe_unused]] can::State apply_event(can::State state, const can::Event& event) noexcept {
    switch (event.type) {
        case can::EventType::Warning:
            return can::State::Warning;
        case can::EventType::Passive:
            return can::State::Passive;
        case can::EventType::BusOff:
            return can::State::BusOff;
        case can::EventType::Restarted:
            return can::State::Active;
        default:
            return state;
    }
}

[[maybe_unused]] void update_error_stats(can::Stats& stats, const ::can_frame& frame) noexcept {
    ++stats.error_frames;
    if ((frame.can_id & CAN_ERR_CRTL) != 0U && (frame.data[1] & CAN_ERR_CRTL_RX_OVERFLOW) != 0U) {
        ++stats.rx_overruns;
    }
}

}  // namespace
