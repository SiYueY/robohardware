#include "can/socket.hpp"

#include <linux/can.h>
#include <linux/can/error.h>

#include <cstring>
#include <unistd.h>
#include <utility>

namespace {

constexpr std::uint32_t kStandardIdMask = 0x7FFU;
constexpr std::uint32_t kExtendedIdMask = 0x1FFFFFFFU;

bool is_valid(std::uint32_t value, can::FrameFormat format) noexcept {
    return value <= (format == can::FrameFormat::Standard ? kStandardIdMask : kExtendedIdMask);
}

[[maybe_unused]] can::Result<void> validate(const can::Frame& frame) noexcept {
    if (frame.size > frame.data.size() || !is_valid(frame.id, frame.format)) {
        return can::Error{can::ErrorCode::InvalidFrame};
    }
    return {};
}

[[maybe_unused]] can::Result<void> validate(const can::Filter& filter) noexcept {
    if (!is_valid(filter.id, filter.format) || !is_valid(filter.mask, filter.format)) {
        return can::Error{can::ErrorCode::InvalidArgument};
    }
    return {};
}

[[maybe_unused]] ::can_frame to_native(const can::Frame& frame) noexcept {
    ::can_frame native{};
    native.can_id = frame.id;
    if (frame.format == can::FrameFormat::Extended) native.can_id |= CAN_EFF_FLAG;
    if (frame.type == can::FrameType::Remote) native.can_id |= CAN_RTR_FLAG;
    native.can_dlc = frame.size;
    std::memcpy(native.data, frame.data.data(), frame.size);
    return native;
}

[[maybe_unused]] can::Result<can::Frame> from_native(const ::can_frame& native) noexcept {
    if ((native.can_id & CAN_ERR_FLAG) != 0U || native.can_dlc > CAN_MAX_DLEN) {
        return can::Error{can::ErrorCode::InvalidFrame};
    }

    can::Frame frame{};
    frame.format = (native.can_id & CAN_EFF_FLAG) != 0U ? can::FrameFormat::Extended
                                                        : can::FrameFormat::Standard;
    frame.type =
        (native.can_id & CAN_RTR_FLAG) != 0U ? can::FrameType::Remote : can::FrameType::Data;
    frame.id =
        native.can_id & (frame.format == can::FrameFormat::Extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    frame.size = native.can_dlc;
    std::memcpy(frame.data.data(), native.data, frame.size);
    return frame;
}

[[maybe_unused]] ::can_filter to_native(const can::Filter& filter) noexcept {
    ::can_filter native{};
    native.can_id = filter.id;
    native.can_mask = filter.mask | CAN_EFF_FLAG;
    if (filter.format == can::FrameFormat::Extended) native.can_id |= CAN_EFF_FLAG;
    return native;
}

}  // namespace

namespace can {

Socket::Socket(int fd) noexcept : fd_(fd) {}

Socket::~Socket() {
    if (fd_ >= 0) ::close(fd_);
}

Socket::Socket(Socket&& other) noexcept
: fd_(std::exchange(other.fd_, -1)),
  state_(other.state_.load(std::memory_order_relaxed)),
  rx_frames_(other.rx_frames_.load(std::memory_order_relaxed)),
  tx_frames_(other.tx_frames_.load(std::memory_order_relaxed)),
  rx_errors_(other.rx_errors_.load(std::memory_order_relaxed)),
  tx_errors_(other.tx_errors_.load(std::memory_order_relaxed)),
  error_frames_(other.error_frames_.load(std::memory_order_relaxed)),
  rx_overruns_(other.rx_overruns_.load(std::memory_order_relaxed)),
  dropped_events_(other.dropped_events_.load(std::memory_order_relaxed)),
  events_(other.events_),
  event_read_(other.event_read_.load(std::memory_order_relaxed)),
  event_write_(other.event_write_.load(std::memory_order_relaxed)) {
    other.state_.store(State::Unknown, std::memory_order_relaxed);
    other.rx_frames_.store(0, std::memory_order_relaxed);
    other.tx_frames_.store(0, std::memory_order_relaxed);
    other.rx_errors_.store(0, std::memory_order_relaxed);
    other.tx_errors_.store(0, std::memory_order_relaxed);
    other.error_frames_.store(0, std::memory_order_relaxed);
    other.rx_overruns_.store(0, std::memory_order_relaxed);
    other.dropped_events_.store(0, std::memory_order_relaxed);
    other.event_read_.store(0, std::memory_order_relaxed);
    other.event_write_.store(0, std::memory_order_relaxed);
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this == &other) return *this;
    if (fd_ >= 0) ::close(fd_);
    fd_ = std::exchange(other.fd_, -1);
    state_.store(other.state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    rx_frames_.store(other.rx_frames_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    tx_frames_.store(other.tx_frames_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    rx_errors_.store(other.rx_errors_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    tx_errors_.store(other.tx_errors_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    error_frames_.store(
        other.error_frames_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    rx_overruns_.store(
        other.rx_overruns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dropped_events_.store(
        other.dropped_events_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    events_ = other.events_;
    event_read_.store(other.event_read_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    event_write_.store(
        other.event_write_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    other.state_.store(State::Unknown, std::memory_order_relaxed);
    other.rx_frames_.store(0, std::memory_order_relaxed);
    other.tx_frames_.store(0, std::memory_order_relaxed);
    other.rx_errors_.store(0, std::memory_order_relaxed);
    other.tx_errors_.store(0, std::memory_order_relaxed);
    other.error_frames_.store(0, std::memory_order_relaxed);
    other.rx_overruns_.store(0, std::memory_order_relaxed);
    other.dropped_events_.store(0, std::memory_order_relaxed);
    other.event_read_.store(0, std::memory_order_relaxed);
    other.event_write_.store(0, std::memory_order_relaxed);
    return *this;
}

Result<void> Socket::send(const Frame&) noexcept { return Error{ErrorCode::InvalidState}; }

Result<bool> Socket::receive(Frame&, RxInfo&) noexcept { return Error{ErrorCode::InvalidState}; }

bool Socket::try_pop_event(Event&) noexcept { return false; }

State Socket::state() const noexcept { return state_.load(std::memory_order_relaxed); }

Stats Socket::stats() const noexcept {
    return {
        rx_frames_.load(std::memory_order_relaxed),
        tx_frames_.load(std::memory_order_relaxed),
        rx_errors_.load(std::memory_order_relaxed),
        tx_errors_.load(std::memory_order_relaxed),
        error_frames_.load(std::memory_order_relaxed),
        rx_overruns_.load(std::memory_order_relaxed),
        dropped_events_.load(std::memory_order_relaxed),
    };
}

int Socket::fd() const noexcept { return fd_; }

}  // namespace can
