#include "can/native.hpp"

#include <linux/can/error.h>

#include <cstring>

namespace can::native {
namespace {

constexpr std::uint32_t kStandardIdMask = 0x7FFU;
constexpr std::uint32_t kExtendedIdMask = 0x1FFFFFFFU;

bool is_valid(std::uint32_t value, FrameFormat format) noexcept {
    return value <= (format == FrameFormat::Standard ? kStandardIdMask : kExtendedIdMask);
}

}  // namespace

Result<void> validate(const Frame& frame) noexcept {
    if (frame.size > frame.data.size() || !is_valid(frame.id, frame.format)) {
        return Error{ErrorCode::InvalidFrame};
    }
    return {};
}

Result<void> validate(const Filter& filter) noexcept {
    if (!is_valid(filter.id, filter.format) || !is_valid(filter.mask, filter.format)) {
        return Error{ErrorCode::InvalidArgument};
    }
    return {};
}

::can_frame to_native(const Frame& frame) noexcept {
    ::can_frame native{};
    native.can_id = frame.id;
    if (frame.format == FrameFormat::Extended) native.can_id |= CAN_EFF_FLAG;
    if (frame.type == FrameType::Remote) native.can_id |= CAN_RTR_FLAG;
    native.can_dlc = frame.size;
    std::memcpy(native.data, frame.data.data(), frame.size);
    return native;
}

Result<Frame> from_native(const ::can_frame& native) noexcept {
    if ((native.can_id & CAN_ERR_FLAG) != 0U || native.can_dlc > CAN_MAX_DLEN) {
        return Error{ErrorCode::InvalidFrame};
    }

    Frame frame{};
    frame.format = (native.can_id & CAN_EFF_FLAG) != 0U ? FrameFormat::Extended
                                                        : FrameFormat::Standard;
    frame.type = (native.can_id & CAN_RTR_FLAG) != 0U ? FrameType::Remote : FrameType::Data;
    frame.id = native.can_id & (frame.format == FrameFormat::Extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    frame.size = native.can_dlc;
    std::memcpy(frame.data.data(), native.data, frame.size);
    return frame;
}

::can_filter to_native(const Filter& filter) noexcept {
    ::can_filter native{};
    native.can_id = filter.id;
    native.can_mask = filter.mask | CAN_EFF_FLAG;
    if (filter.format == FrameFormat::Extended) native.can_id |= CAN_EFF_FLAG;
    return native;
}

Event decode_error_frame(const ::can_frame& frame, Timestamp timestamp) noexcept {
    const std::uint32_t flags = frame.can_id & CAN_ERR_MASK;
    Event event{EventType::Unknown, timestamp, flags, 0};

    if ((flags & CAN_ERR_BUSOFF) != 0U) {
        event.type = EventType::BusOff;
    } else if ((flags & CAN_ERR_RESTARTED) != 0U) {
        event.type = EventType::Restarted;
    } else if ((flags & CAN_ERR_CRTL) != 0U &&
               (frame.data[1] & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE)) != 0U) {
        event.type = EventType::Passive;
    } else if ((flags & CAN_ERR_CRTL) != 0U &&
               (frame.data[1] & (CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING)) != 0U) {
        event.type = EventType::Warning;
    } else if ((flags & CAN_ERR_CRTL) != 0U && (frame.data[1] & CAN_ERR_CRTL_RX_OVERFLOW) != 0U) {
        event.type = EventType::RxOverflow;
    } else if ((flags & CAN_ERR_LOSTARB) != 0U) {
        event.type = EventType::ArbitrationLost;
    } else if ((flags & CAN_ERR_PROT) != 0U) {
        event.type = EventType::ProtocolError;
    } else if ((flags & (CAN_ERR_CRTL | CAN_ERR_TRX | CAN_ERR_TX_TIMEOUT)) != 0U) {
        event.type = EventType::ControllerError;
    }
    return event;
}

}  // namespace can::native
