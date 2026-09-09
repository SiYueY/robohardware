#include "can/socket.hpp"

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <utility>

namespace {

constexpr std::uint32_t kStandardIdMask = 0x7FFU;
constexpr std::uint32_t kExtendedIdMask = 0x1FFFFFFFU;

bool is_valid(std::uint32_t value, can::FrameFormat format) noexcept {
    return value <= (format == can::FrameFormat::Standard ? kStandardIdMask : kExtendedIdMask);
}

can::Result<void> validate(const can::Frame& frame) noexcept {
    if (frame.size > frame.data.size() || !is_valid(frame.id, frame.format)) {
        return can::Error{can::ErrorCode::InvalidFrame};
    }
    return {};
}

can::Result<void> validate(const can::Filter& filter) noexcept {
    if (!is_valid(filter.id, filter.format) || !is_valid(filter.mask, filter.format)) {
        return can::Error{can::ErrorCode::InvalidArgument};
    }
    return {};
}

::can_frame to_native(const can::Frame& frame) noexcept {
    ::can_frame native{};
    native.can_id = frame.id;
    if (frame.format == can::FrameFormat::Extended) native.can_id |= CAN_EFF_FLAG;
    if (frame.type == can::FrameType::Remote) native.can_id |= CAN_RTR_FLAG;
    native.can_dlc = frame.size;
    std::memcpy(native.data, frame.data.data(), frame.size);
    return native;
}

can::Result<can::Frame> from_native(const ::can_frame& native) noexcept {
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

::can_filter to_native(const can::Filter& filter) noexcept {
    ::can_filter native{};
    native.can_id = filter.id;
    native.can_mask = filter.mask | CAN_EFF_FLAG;
    if (filter.format == can::FrameFormat::Extended) native.can_id |= CAN_EFF_FLAG;
    return native;
}

can::Timestamp now() noexcept {
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now());
}

can::Event decode_error_frame(const ::can_frame& frame, can::Timestamp timestamp) noexcept {
    const std::uint32_t flags = frame.can_id & CAN_ERR_MASK;
    can::Event event{can::EventType::Unknown, timestamp, flags};

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
    } else if ((flags & CAN_ERR_CRTL) != 0U && (frame.data[1] & CAN_ERR_CRTL_RX_OVERFLOW) != 0U) {
        event.type = can::EventType::RxOverflow;
    } else if ((flags & CAN_ERR_LOSTARB) != 0U) {
        event.type = can::EventType::ArbitrationLost;
    } else if ((flags & CAN_ERR_PROT) != 0U) {
        event.type = can::EventType::ProtocolError;
    } else if ((flags & (CAN_ERR_CRTL | CAN_ERR_TRX | CAN_ERR_TX_TIMEOUT)) != 0U) {
        event.type = can::EventType::ControllerError;
    }
    return event;
}

can::State apply_event(can::State state, const can::Event& event) noexcept {
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

}  // namespace

namespace can {

Socket::Socket(int fd) noexcept : fd_(fd) {}

Result<Socket> Socket::open(Options options) {
    if (options.interface.empty()) return Error{ErrorCode::InvalidArgument};
    for (const Filter& filter : options.filters) {
        const Result<void> result = validate(filter);
        if (!result) return result.error();
    }

    const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) return Error{ErrorCode::OpenFailed, errno};

    const auto fail = [fd]() noexcept {
        const int code = errno;
        ::close(fd);
        return Error{ErrorCode::OpenFailed, code};
    };

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return fail();

    const unsigned int ifindex = ::if_nametoindex(options.interface.c_str());
    if (ifindex == 0U) return fail();

    std::vector<::can_filter> native_filters;
    native_filters.reserve(options.filters.size());
    for (const Filter& filter : options.filters) native_filters.push_back(to_native(filter));
    if (!native_filters.empty() &&
        ::setsockopt(
            fd, SOL_CAN_RAW, CAN_RAW_FILTER, native_filters.data(),
            static_cast<socklen_t>(native_filters.size() * sizeof(::can_filter))) < 0) {
        return fail();
    }

    const can_err_mask_t error_mask = options.error_frames ? CAN_ERR_MASK : 0U;
    if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &error_mask, sizeof(error_mask)) < 0) {
        return fail();
    }

    const int receive_own = options.receive_own ? 1 : 0;
    if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &receive_own, sizeof(receive_own)) <
        0) {
        return fail();
    }

    ::sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(ifindex);
    if (::bind(fd, reinterpret_cast<const ::sockaddr*>(&address), sizeof(address)) < 0)
        return fail();

    return Socket(fd);
}

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
    other.fd_ = -1;
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
    other.fd_ = -1;
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

Result<void> Socket::send(const Frame& frame) noexcept {
    const Result<void> result = validate(frame);
    if (!result) return result.error();

    const ::can_frame native = to_native(frame);
    const ssize_t sent = ::send(fd_, &native, sizeof(native), 0);
    if (sent != static_cast<ssize_t>(sizeof(native))) {
        ++tx_errors_;
        return Error{ErrorCode::IoFailed, sent < 0 ? errno : EIO};
    }
    ++tx_frames_;
    return {};
}

Result<bool> Socket::receive(Frame& frame, RxInfo& info) noexcept {
    ::can_frame native{};
    ::iovec iov{&native, sizeof(native)};
    ::msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    const ssize_t received = ::recvmsg(fd_, &message, 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
        ++rx_errors_;
        return Error{ErrorCode::IoFailed, errno};
    }
    if (received != static_cast<ssize_t>(sizeof(native))) {
        ++rx_errors_;
        return Error{ErrorCode::IoFailed, EIO};
    }

    const Timestamp timestamp = now();
    if ((native.can_id & CAN_ERR_FLAG) != 0U) {
        const Event event = decode_error_frame(native, timestamp);
        ++error_frames_;
        if ((native.can_id & CAN_ERR_CRTL) != 0U &&
            (native.data[1] & CAN_ERR_CRTL_RX_OVERFLOW) != 0U) {
            ++rx_overruns_;
        }
        state_.store(
            apply_event(state_.load(std::memory_order_relaxed), event), std::memory_order_relaxed);

        const std::size_t write = event_write_.load(std::memory_order_relaxed);
        const std::size_t next = (write + 1) % events_.size();
        if (next == event_read_.load(std::memory_order_acquire)) {
            ++dropped_events_;
        } else {
            events_[write] = event;
            event_write_.store(next, std::memory_order_release);
        }
        return false;
    }

    const Result<Frame> decoded = from_native(native);
    if (!decoded) {
        ++rx_errors_;
        return decoded.error();
    }
    frame = decoded.value();
    info.received_at = timestamp;
    ++rx_frames_;
    return true;
}

bool Socket::try_pop_event(Event& event) noexcept {
    const std::size_t read = event_read_.load(std::memory_order_relaxed);
    if (read == event_write_.load(std::memory_order_acquire)) return false;
    event = events_[read];
    event_read_.store((read + 1) % events_.size(), std::memory_order_release);
    return true;
}

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
