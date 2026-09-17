#include <serial/port.hpp>

#include "tty.hpp"

#include <cerrno>
#include <cstdint>
#include <limits>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>

namespace serial {
namespace {

constexpr int kClosedFd = -1;
constexpr long kNanosecondsPerSecond = 1'000'000'000L;

template <typename T>
using Result = hardware::Result<T, Error>;

[[nodiscard]] bool is_unsupported_error(int native_error) noexcept {
    return native_error == ENOTTY || native_error == EOPNOTSUPP || native_error == ENOSYS;
}

[[nodiscard]] bool is_disconnected_error(int native_error) noexcept {
    return native_error == EIO || native_error == ENXIO || native_error == ENODEV ||
           native_error == ECONNRESET;
}

[[nodiscard]] Error map_open_error(int native_error) noexcept {
    if (native_error == EACCES || native_error == EPERM) return Error::PermissionDenied;
    if (native_error == ENOENT || native_error == ENODEV || native_error == ENXIO) {
        return Error::DeviceNotFound;
    }
    if (native_error == EBUSY) return Error::Busy;
    if (native_error == ENOMEM) return Error::OutOfMemory;
    return Error::Io;
}

[[nodiscard]] Error map_runtime_error(int native_error) noexcept {
    if (native_error == EACCES || native_error == EPERM) return Error::PermissionDenied;
    if (native_error == EBUSY) return Error::Busy;
    if (is_unsupported_error(native_error)) return Error::Unsupported;
    if (is_disconnected_error(native_error)) return Error::Disconnected;
    return Error::Io;
}

[[nodiscard]] bool is_valid(DataBits value) noexcept {
    return value == DataBits::Five || value == DataBits::Six || value == DataBits::Seven ||
           value == DataBits::Eight;
}

[[nodiscard]] bool is_valid(Parity value) noexcept {
    return value == Parity::None || value == Parity::Odd || value == Parity::Even ||
           value == Parity::Mark || value == Parity::Space;
}

[[nodiscard]] bool is_valid(StopBits value) noexcept {
    return value == StopBits::One || value == StopBits::Two;
}

[[nodiscard]] bool is_valid(FlowControl value) noexcept {
    return value == FlowControl::None || value == FlowControl::XonXoff ||
           value == FlowControl::RtsCts;
}

[[nodiscard]] bool to_termios_speed(std::uint32_t baud_rate, speed_t& speed) noexcept {
#define SERIAL_BAUD(value) \
    case value:             \
        speed = B##value;   \
        return true
    switch (baud_rate) {
        SERIAL_BAUD(50);
        SERIAL_BAUD(75);
        SERIAL_BAUD(110);
        SERIAL_BAUD(300);
        SERIAL_BAUD(600);
        SERIAL_BAUD(1200);
        SERIAL_BAUD(2400);
        SERIAL_BAUD(4800);
        SERIAL_BAUD(9600);
        SERIAL_BAUD(19200);
        SERIAL_BAUD(38400);
        SERIAL_BAUD(57600);
        SERIAL_BAUD(115200);
        SERIAL_BAUD(230400);
#ifdef B460800
        SERIAL_BAUD(460800);
#endif
#ifdef B500000
        SERIAL_BAUD(500000);
#endif
#ifdef B576000
        SERIAL_BAUD(576000);
#endif
#ifdef B921600
        SERIAL_BAUD(921600);
#endif
#ifdef B1000000
        SERIAL_BAUD(1000000);
#endif
#ifdef B1152000
        SERIAL_BAUD(1152000);
#endif
#ifdef B1500000
        SERIAL_BAUD(1500000);
#endif
#ifdef B2000000
        SERIAL_BAUD(2000000);
#endif
#ifdef B2500000
        SERIAL_BAUD(2500000);
#endif
#ifdef B3000000
        SERIAL_BAUD(3000000);
#endif
#ifdef B3500000
        SERIAL_BAUD(3500000);
#endif
#ifdef B4000000
        SERIAL_BAUD(4000000);
#endif
        default:
            return false;
    }
#undef SERIAL_BAUD
}

[[nodiscard]] Result<speed_t> validate_config(const Config& config) noexcept {
    if (!is_valid(config.data_bits) || !is_valid(config.parity) || !is_valid(config.stop_bits) ||
        !is_valid(config.flow_control) || config.baud_rate == 0 ||
        config.rs485.delay_before_send.count() < 0 ||
        config.rs485.delay_after_send.count() < 0 ||
        config.rs485.delay_before_send.count() > std::numeric_limits<std::uint32_t>::max() ||
        config.rs485.delay_after_send.count() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<speed_t>::failure(Error::InvalidArgument);
    }

#ifndef CMSPAR
    if (config.parity == Parity::Mark || config.parity == Parity::Space) {
        return Result<speed_t>::failure(Error::Unsupported);
    }
#endif
#ifndef CRTSCTS
    if (config.flow_control == FlowControl::RtsCts) {
        return Result<speed_t>::failure(Error::Unsupported);
    }
#endif
#ifndef SER_RS485_RX_DURING_TX
    if (config.rs485.receive_during_transmit) {
        return Result<speed_t>::failure(Error::Unsupported);
    }
#endif

    speed_t speed{};
    if (!to_termios_speed(config.baud_rate, speed)) {
        return Result<speed_t>::failure(Error::Unsupported);
    }
    return Result<speed_t>::success(speed);
}

void apply_raw_mode(termios& attributes) noexcept {
    attributes.c_iflag &=
        ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | INPCK | IGNPAR | IXON |
          IXOFF | IXANY);
    attributes.c_oflag &= ~OPOST;
    attributes.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    attributes.c_cflag |= CREAD | CLOCAL;
    attributes.c_cc[VMIN] = 0;
    attributes.c_cc[VTIME] = 0;
}

[[nodiscard]] Result<void> configure_attributes(
    termios& attributes, const Config& config, speed_t speed) noexcept {
    apply_raw_mode(attributes);

    attributes.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CMSPAR
    attributes.c_cflag &= ~CMSPAR;
#endif
#ifdef CRTSCTS
    attributes.c_cflag &= ~CRTSCTS;
#endif

    switch (config.data_bits) {
        case DataBits::Five:
            attributes.c_cflag |= CS5;
            break;
        case DataBits::Six:
            attributes.c_cflag |= CS6;
            break;
        case DataBits::Seven:
            attributes.c_cflag |= CS7;
            break;
        case DataBits::Eight:
            attributes.c_cflag |= CS8;
            break;
    }

    if (config.parity != Parity::None) attributes.c_cflag |= PARENB;
    if (config.parity == Parity::Odd || config.parity == Parity::Mark) {
        attributes.c_cflag |= PARODD;
    }
#ifdef CMSPAR
    if (config.parity == Parity::Mark || config.parity == Parity::Space) {
        attributes.c_cflag |= CMSPAR;
    }
#endif

    if (config.stop_bits == StopBits::Two) attributes.c_cflag |= CSTOPB;
    if (config.flow_control == FlowControl::XonXoff) attributes.c_iflag |= IXON | IXOFF;
#ifdef CRTSCTS
    if (config.flow_control == FlowControl::RtsCts) attributes.c_cflag |= CRTSCTS;
#endif

    if (::cfsetispeed(&attributes, speed) < 0 || ::cfsetospeed(&attributes, speed) < 0) {
        return Result<void>::failure(map_runtime_error(errno));
    }
    return Result<void>::success();
}

[[nodiscard]] bool attributes_match(
    const termios& attributes, const Config& config, speed_t speed) noexcept {
    const tcflag_t expected_input = config.flow_control == FlowControl::XonXoff ? IXON | IXOFF : 0;
    const tcflag_t raw_input =
        IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | INPCK | IGNPAR | IXANY;

    if ((attributes.c_iflag & (raw_input | IXON | IXOFF)) != expected_input ||
        (attributes.c_oflag & OPOST) != 0 ||
        (attributes.c_lflag & (ECHO | ECHONL | ICANON | ISIG | IEXTEN)) != 0 ||
        (attributes.c_cflag & (CREAD | CLOCAL)) != (CREAD | CLOCAL) ||
        attributes.c_cc[VMIN] != 0 || attributes.c_cc[VTIME] != 0 ||
        ::cfgetispeed(&attributes) != speed || ::cfgetospeed(&attributes) != speed) {
        return false;
    }

    tcflag_t expected_control = 0;
    switch (config.data_bits) {
        case DataBits::Five:
            expected_control |= CS5;
            break;
        case DataBits::Six:
            expected_control |= CS6;
            break;
        case DataBits::Seven:
            expected_control |= CS7;
            break;
        case DataBits::Eight:
            expected_control |= CS8;
            break;
    }
    if (config.parity != Parity::None) expected_control |= PARENB;
    if (config.parity == Parity::Odd || config.parity == Parity::Mark) {
        expected_control |= PARODD;
    }
#ifdef CMSPAR
    if (config.parity == Parity::Mark || config.parity == Parity::Space) {
        expected_control |= CMSPAR;
    }
#endif
    if (config.stop_bits == StopBits::Two) expected_control |= CSTOPB;

    tcflag_t control_mask = CSIZE | PARENB | PARODD | CSTOPB;
#ifdef CMSPAR
    control_mask |= CMSPAR;
#endif
    if ((attributes.c_cflag & control_mask) != expected_control) return false;

#ifdef CRTSCTS
    const bool rts_cts_enabled = (attributes.c_cflag & CRTSCTS) != 0;
    if (rts_cts_enabled != (config.flow_control == FlowControl::RtsCts)) return false;
#endif
    return true;
}

[[nodiscard]] serial_rs485 make_rs485_request(const Config::RS485& config) noexcept {
    serial_rs485 request{};
    if (!config.enabled) return request;

    request.flags = SER_RS485_ENABLED;
    if (config.rts_on_send) request.flags |= SER_RS485_RTS_ON_SEND;
    if (config.rts_after_send) request.flags |= SER_RS485_RTS_AFTER_SEND;
#ifdef SER_RS485_RX_DURING_TX
    if (config.receive_during_transmit) request.flags |= SER_RS485_RX_DURING_TX;
#endif
    request.delay_rts_before_send = static_cast<std::uint32_t>(config.delay_before_send.count());
    request.delay_rts_after_send = static_cast<std::uint32_t>(config.delay_after_send.count());
    return request;
}

[[nodiscard]] bool rs485_matches(
    const serial_rs485& actual, const Config::RS485& requested_config) noexcept {
    const auto requested = make_rs485_request(requested_config);
    unsigned int owned_flags =
        SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND;
#ifdef SER_RS485_RX_DURING_TX
    owned_flags |= SER_RS485_RX_DURING_TX;
#endif

    return (actual.flags & owned_flags) == (requested.flags & owned_flags) &&
           actual.delay_rts_before_send == requested.delay_rts_before_send &&
           actual.delay_rts_after_send == requested.delay_rts_after_send;
}

void rollback_open(
    int fd, const termios& original_attributes, const serial_rs485& original_rs485,
    bool attributes_attempted, bool rs485_attempted) noexcept {
    if (rs485_attempted) static_cast<void>(tty::write_rs485(fd, original_rs485));
    if (attributes_attempted) {
        static_cast<void>(tty::write_attributes(fd, TCSANOW, original_attributes));
    }
    static_cast<void>(tty::close(fd));
}

struct Deadline final {
    timespec absolute{};
};

[[nodiscard]] int compare_time(const timespec& left, const timespec& right) noexcept {
    if (left.tv_sec != right.tv_sec) return left.tv_sec < right.tv_sec ? -1 : 1;
    if (left.tv_nsec != right.tv_nsec) return left.tv_nsec < right.tv_nsec ? -1 : 1;
    return 0;
}

[[nodiscard]] Result<Deadline> make_deadline(std::chrono::nanoseconds timeout) noexcept {
    if (timeout.count() < 0) return Result<Deadline>::failure(Error::InvalidArgument);

    timespec now{};
    if (tty::monotonic_now(now) < 0) {
        const int native_error = errno;
        return Result<Deadline>::failure(map_runtime_error(native_error));
    }

    const auto seconds = timeout.count() / kNanosecondsPerSecond;
    const auto nanoseconds = timeout.count() % kNanosecondsPerSecond;
    if (seconds > std::numeric_limits<time_t>::max() - now.tv_sec) {
        return Result<Deadline>::failure(Error::InvalidArgument);
    }

    Deadline deadline{{
        now.tv_sec + static_cast<time_t>(seconds), now.tv_nsec + static_cast<long>(nanoseconds)}};
    if (deadline.absolute.tv_nsec >= kNanosecondsPerSecond) {
        ++deadline.absolute.tv_sec;
        deadline.absolute.tv_nsec -= kNanosecondsPerSecond;
    }
    return Result<Deadline>::success(std::move(deadline));
}

[[nodiscard]] Result<void> wait_fd(int fd, short events, const Deadline* deadline) noexcept {
    for (;;) {
        timespec remaining{};
        const timespec* timeout = nullptr;
        if (deadline != nullptr) {
            timespec now{};
            if (tty::monotonic_now(now) < 0) {
                const int native_error = errno;
                return Result<void>::failure(map_runtime_error(native_error));
            }
            if (compare_time(now, deadline->absolute) >= 0) {
                return Result<void>::failure(Error::TimedOut);
            }

            remaining = {
                deadline->absolute.tv_sec - now.tv_sec,
                deadline->absolute.tv_nsec - now.tv_nsec,
            };
            if (remaining.tv_nsec < 0) {
                --remaining.tv_sec;
                remaining.tv_nsec += kNanosecondsPerSecond;
            }
            timeout = &remaining;
        }

        short revents = 0;
        const int waited = tty::wait(fd, events, timeout, revents);
        if (waited < 0) {
            const int native_error = errno;
            if (native_error == EINTR) continue;
            return Result<void>::failure(map_runtime_error(native_error));
        }
        if (waited == 0) return Result<void>::failure(Error::TimedOut);
        if ((revents & POLLNVAL) != 0) return Result<void>::failure(Error::NotOpen);
        if ((revents & POLLERR) != 0) return Result<void>::failure(Error::Disconnected);
        if ((revents & POLLHUP) != 0) {
            // A hung-up TTY may still report POLLIN while buffered input remains.
            if ((events & POLLIN) != 0 && (revents & POLLIN) != 0) {
                return Result<void>::success();
            }
            return Result<void>::failure(Error::Disconnected);
        }
        if ((revents & events) != 0) return Result<void>::success();
        return Result<void>::failure(Error::Io);
    }
}

[[nodiscard]] Result<std::size_t> read_transfer(
    int fd, std::byte* data, std::size_t size, const Deadline* deadline, bool immediate) noexcept {
    if (fd < 0) return Result<std::size_t>::failure(Error::NotOpen);
    if (data == nullptr && size != 0) return Result<std::size_t>::failure(Error::InvalidArgument);
    if (size == 0) return Result<std::size_t>::success(0);

    for (;;) {
        if (!immediate) {
            auto ready = wait_fd(fd, POLLIN, deadline);
            if (!ready) return Result<std::size_t>::failure(ready.error());
        }

        const ssize_t transferred = tty::read(fd, data, size);
        if (transferred > 0) {
            return Result<std::size_t>::success(static_cast<std::size_t>(transferred));
        }
        if (transferred == 0) {
            // With VMIN=0 a non-blocking TTY may return zero after a readiness race.
            // Re-enter the same wait/deadline instead of treating this as EOF.
            if (immediate) return Result<std::size_t>::failure(Error::WouldBlock);
            continue;
        }

        const int native_error = errno;
        if (native_error == EINTR) continue;
        if (native_error == EAGAIN || native_error == EWOULDBLOCK) {
            if (immediate) return Result<std::size_t>::failure(Error::WouldBlock);
            continue;
        }
        return Result<std::size_t>::failure(map_runtime_error(native_error));
    }
}

[[nodiscard]] Result<std::size_t> write_transfer(
    int fd, const std::byte* data, std::size_t size, const Deadline* deadline,
    bool immediate) noexcept {
    if (fd < 0) return Result<std::size_t>::failure(Error::NotOpen);
    if (data == nullptr && size != 0) return Result<std::size_t>::failure(Error::InvalidArgument);
    if (size == 0) return Result<std::size_t>::success(0);

    for (;;) {
        if (!immediate) {
            auto ready = wait_fd(fd, POLLOUT, deadline);
            if (!ready) return Result<std::size_t>::failure(ready.error());
        }

        const ssize_t transferred = tty::write(fd, data, size);
        if (transferred > 0) {
            return Result<std::size_t>::success(static_cast<std::size_t>(transferred));
        }
        if (transferred == 0) {
            return Result<std::size_t>::failure(immediate ? Error::WouldBlock : Error::Io);
        }

        const int native_error = errno;
        if (native_error == EINTR) continue;
        if (native_error == EAGAIN || native_error == EWOULDBLOCK) {
            if (immediate) return Result<std::size_t>::failure(Error::WouldBlock);
            continue;
        }
        return Result<std::size_t>::failure(map_runtime_error(native_error));
    }
}

[[nodiscard]] Result<bool> read_modem_line(int fd, int line) noexcept {
    if (fd < 0) return Result<bool>::failure(Error::NotOpen);

    int lines = 0;
    if (tty::read_modem_lines(fd, lines) < 0) {
        const int native_error = errno;
        return Result<bool>::failure(map_runtime_error(native_error));
    }
    return Result<bool>::success((lines & line) != 0);
}

}  // namespace

Port::~Port() noexcept { static_cast<void>(close()); }

Port::Port(Port&& other) noexcept : fd_(other.fd_), rts_automatic_(other.rts_automatic_) {
    other.fd_ = kClosedFd;
    other.rts_automatic_ = false;
}

bool Port::is_open() const noexcept { return fd_ >= 0; }

hardware::Result<void, Error> Port::open(const std::string& path, const Config& config) noexcept {
    if (is_open()) return Result<void>::failure(Error::AlreadyOpen);
    if (path.empty() || path.find('\0') != std::string::npos) {
        return Result<void>::failure(Error::InvalidArgument);
    }

    auto speed = validate_config(config);
    if (!speed) return Result<void>::failure(speed.error());

    const int candidate = tty::open(path.c_str());
    if (candidate < 0) {
        const int native_error = errno;
        return Result<void>::failure(map_open_error(native_error));
    }

    const int terminal = tty::is_terminal(candidate);
    if (terminal <= 0) {
        const int native_error = errno;
        static_cast<void>(tty::close(candidate));
        return Result<void>::failure(
            terminal == 0 ? Error::NotTerminal : map_runtime_error(native_error));
    }

    termios original_attributes{};
    if (tty::read_attributes(candidate, original_attributes) < 0) {
        const int native_error = errno;
        static_cast<void>(tty::close(candidate));
        return Result<void>::failure(map_runtime_error(native_error));
    }

    serial_rs485 original_rs485{};
    bool rs485_supported = true;
    if (tty::read_rs485(candidate, original_rs485) < 0) {
        const int native_error = errno;
        if (is_unsupported_error(native_error)) {
            rs485_supported = false;
            if (config.rs485.enabled) {
                static_cast<void>(tty::close(candidate));
                return Result<void>::failure(Error::Unsupported);
            }
        } else {
            static_cast<void>(tty::close(candidate));
            return Result<void>::failure(map_runtime_error(native_error));
        }
    }

    termios requested_attributes = original_attributes;
    auto configured = configure_attributes(requested_attributes, config, speed.value());
    if (!configured) {
        static_cast<void>(tty::close(candidate));
        return Result<void>::failure(configured.error());
    }

    if (tty::write_attributes(candidate, TCSANOW, requested_attributes) < 0) {
        const int native_error = errno;
        rollback_open(candidate, original_attributes, original_rs485, true, false);
        return Result<void>::failure(map_runtime_error(native_error));
    }

    bool rs485_attempted = false;
    if (rs485_supported) {
        rs485_attempted = true;
        const auto requested_rs485 = make_rs485_request(config.rs485);
        if (tty::write_rs485(candidate, requested_rs485) < 0) {
            const int native_error = errno;
            rollback_open(candidate, original_attributes, original_rs485, true, true);
            return Result<void>::failure(
                is_unsupported_error(native_error) ? Error::Unsupported
                                                   : map_runtime_error(native_error));
        }
    }

    termios effective_attributes{};
    if (tty::read_attributes(candidate, effective_attributes) < 0) {
        const int native_error = errno;
        rollback_open(candidate, original_attributes, original_rs485, true, rs485_attempted);
        return Result<void>::failure(map_runtime_error(native_error));
    }
    if (!attributes_match(effective_attributes, config, speed.value())) {
        rollback_open(candidate, original_attributes, original_rs485, true, rs485_attempted);
        return Result<void>::failure(Error::Unsupported);
    }

    if (rs485_supported) {
        serial_rs485 effective_rs485{};
        if (tty::read_rs485(candidate, effective_rs485) < 0) {
            const int native_error = errno;
            rollback_open(candidate, original_attributes, original_rs485, true, true);
            return Result<void>::failure(map_runtime_error(native_error));
        }
        if (!rs485_matches(effective_rs485, config.rs485)) {
            rollback_open(candidate, original_attributes, original_rs485, true, true);
            return Result<void>::failure(Error::Unsupported);
        }
    }

    fd_ = candidate;
    rts_automatic_ = config.rs485.enabled || config.flow_control == FlowControl::RtsCts;
    return Result<void>::success();
}

hardware::Result<void, Error> Port::close() noexcept {
    if (!is_open()) return Result<void>::success();

    const int closing = fd_;
    fd_ = kClosedFd;
    rts_automatic_ = false;
    if (tty::close(closing) < 0) {
        const int native_error = errno;
        return Result<void>::failure(map_runtime_error(native_error));
    }
    return Result<void>::success();
}

hardware::Result<std::size_t, Error> Port::read(std::byte* data, std::size_t size) noexcept {
    return read_transfer(fd_, data, size, nullptr, false);
}

hardware::Result<std::size_t, Error> Port::read(
    std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept {
    if (!is_open()) return Result<std::size_t>::failure(Error::NotOpen);
    auto deadline = make_deadline(timeout);
    if (!deadline) return Result<std::size_t>::failure(deadline.error());
    return read_transfer(fd_, data, size, &deadline.value(), false);
}

hardware::Result<std::size_t, Error> Port::try_read(
    std::byte* data, std::size_t size) noexcept {
    return read_transfer(fd_, data, size, nullptr, true);
}

hardware::Result<std::size_t, Error> Port::write(
    const std::byte* data, std::size_t size) noexcept {
    return write_transfer(fd_, data, size, nullptr, false);
}

hardware::Result<std::size_t, Error> Port::write(
    const std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept {
    if (!is_open()) return Result<std::size_t>::failure(Error::NotOpen);
    auto deadline = make_deadline(timeout);
    if (!deadline) return Result<std::size_t>::failure(deadline.error());
    return write_transfer(fd_, data, size, &deadline.value(), false);
}

hardware::Result<std::size_t, Error> Port::try_write(
    const std::byte* data, std::size_t size) noexcept {
    return write_transfer(fd_, data, size, nullptr, true);
}

hardware::Result<void, Error> Port::wait_readable(std::chrono::nanoseconds timeout) noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    auto deadline = make_deadline(timeout);
    if (!deadline) return Result<void>::failure(deadline.error());
    return wait_fd(fd_, POLLIN, &deadline.value());
}

hardware::Result<void, Error> Port::wait_writable(std::chrono::nanoseconds timeout) noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    auto deadline = make_deadline(timeout);
    if (!deadline) return Result<void>::failure(deadline.error());
    return wait_fd(fd_, POLLOUT, &deadline.value());
}

hardware::Result<std::size_t, Error> Port::bytes_available() const noexcept {
    if (!is_open()) return Result<std::size_t>::failure(Error::NotOpen);

    int value = 0;
    if (tty::read_input_queue_size(fd_, value) < 0) {
        const int native_error = errno;
        return Result<std::size_t>::failure(map_runtime_error(native_error));
    }
    if (value < 0) return Result<std::size_t>::failure(Error::Io);
    return Result<std::size_t>::success(static_cast<std::size_t>(value));
}

hardware::Result<std::size_t, Error> Port::bytes_pending() const noexcept {
    if (!is_open()) return Result<std::size_t>::failure(Error::NotOpen);

    int value = 0;
    if (tty::read_output_queue_size(fd_, value) < 0) {
        const int native_error = errno;
        return Result<std::size_t>::failure(map_runtime_error(native_error));
    }
    if (value < 0) return Result<std::size_t>::failure(Error::Io);
    return Result<std::size_t>::success(static_cast<std::size_t>(value));
}

hardware::Result<void, Error> Port::discard_input() noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    if (tty::discard(fd_, TCIFLUSH) < 0) {
        const int native_error = errno;
        return Result<void>::failure(map_runtime_error(native_error));
    }
    return Result<void>::success();
}

hardware::Result<void, Error> Port::discard_output() noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    if (tty::discard(fd_, TCOFLUSH) < 0) {
        const int native_error = errno;
        return Result<void>::failure(map_runtime_error(native_error));
    }
    return Result<void>::success();
}

hardware::Result<void, Error> Port::discard_buffers() noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    if (tty::discard(fd_, TCIOFLUSH) < 0) {
        const int native_error = errno;
        return Result<void>::failure(map_runtime_error(native_error));
    }
    return Result<void>::success();
}

hardware::Result<void, Error> Port::drain() noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);

    for (;;) {
        if (tty::drain(fd_) == 0) return Result<void>::success();
        const int native_error = errno;
        if (native_error == EINTR) continue;
        return Result<void>::failure(map_runtime_error(native_error));
    }
}

hardware::Result<void, Error> Port::set_rts(bool asserted) noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    if (rts_automatic_) return Result<void>::failure(Error::InvalidState);

    if (tty::write_modem_line(fd_, TIOCM_RTS, asserted) < 0) {
        const int native_error = errno;
        return Result<void>::failure(map_runtime_error(native_error));
    }
    return Result<void>::success();
}

hardware::Result<bool, Error> Port::rts() const noexcept {
    return read_modem_line(fd_, TIOCM_RTS);
}

hardware::Result<void, Error> Port::set_dtr(bool asserted) noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);

    if (tty::write_modem_line(fd_, TIOCM_DTR, asserted) < 0) {
        const int native_error = errno;
        return Result<void>::failure(map_runtime_error(native_error));
    }
    return Result<void>::success();
}

hardware::Result<bool, Error> Port::dtr() const noexcept {
    return read_modem_line(fd_, TIOCM_DTR);
}

hardware::Result<bool, Error> Port::cts() const noexcept {
    return read_modem_line(fd_, TIOCM_CTS);
}

hardware::Result<bool, Error> Port::dsr() const noexcept {
    return read_modem_line(fd_, TIOCM_DSR);
}

hardware::Result<bool, Error> Port::ri() const noexcept {
    return read_modem_line(fd_, TIOCM_RI);
}

hardware::Result<bool, Error> Port::dcd() const noexcept {
    return read_modem_line(fd_, TIOCM_CAR);
}

hardware::Result<void, Error> Port::set_break(bool asserted) noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);

    if (tty::write_break(fd_, asserted) < 0) {
        const int native_error = errno;
        return Result<void>::failure(map_runtime_error(native_error));
    }
    return Result<void>::success();
}

}  // namespace serial
