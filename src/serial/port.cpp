#include <serial/port.hpp>

#include "tty.hpp"

#include <cerrno>
#include <cstdint>
#include <limits>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <utility>

namespace serial {
namespace {

constexpr int kClosedFd = -1;
constexpr long kNanosecondsPerSecond = 1'000'000'000L;

template <typename T>
using Result = hardware::Result<T, Error>;

enum class ErrorContext { Open, Runtime };
enum class TransferMode { Wait, Immediate };

[[nodiscard]] Error to_error(int err, ErrorContext context) noexcept {
    switch (err) {
        case EACCES:
        case EPERM:
            return Error::PermissionDenied;
        case EBUSY:
            return Error::Busy;
        case ENOMEM:
            return Error::OutOfMemory;
        case ENOTTY:
        case EOPNOTSUPP:
        case ENOSYS:
            return Error::Unsupported;
        case ENOENT:
            return Error::DeviceNotFound;
        case ENODEV:
        case ENXIO:
            return context == ErrorContext::Open ? Error::DeviceNotFound : Error::Disconnected;
        case EIO:
        case ECONNRESET:
            return context == ErrorContext::Runtime ? Error::Disconnected : Error::Io;
        default:
            return Error::Io;
    }
}

[[nodiscard]] bool is_valid(DataBits value) noexcept {
    switch (value) {
        case DataBits::Five:
        case DataBits::Six:
        case DataBits::Seven:
        case DataBits::Eight:
            return true;
    }
    return false;
}

[[nodiscard]] bool is_valid(Parity value) noexcept {
    switch (value) {
        case Parity::None:
        case Parity::Odd:
        case Parity::Even:
        case Parity::Mark:
        case Parity::Space:
            return true;
    }
    return false;
}

[[nodiscard]] bool is_valid(StopBits value) noexcept {
    switch (value) {
        case StopBits::One:
        case StopBits::Two:
            return true;
    }
    return false;
}

[[nodiscard]] bool is_valid(FlowControl value) noexcept {
    switch (value) {
        case FlowControl::None:
        case FlowControl::XonXoff:
        case FlowControl::RtsCts:
            return true;
    }
    return false;
}

[[nodiscard]] bool is_valid_delay(std::chrono::milliseconds delay) noexcept {
    return delay.count() >= 0 && delay.count() <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] Result<speed_t> to_termios_speed(std::uint32_t baud_rate) noexcept {
#define SERIAL_BAUD(value) \
    case value:            \
        return Result<speed_t>::success(B##value)
    switch (baud_rate) {
        SERIAL_BAUD(50);
        SERIAL_BAUD(75);
        SERIAL_BAUD(110);
#ifdef B134
        SERIAL_BAUD(134);
#endif
#ifdef B150
        SERIAL_BAUD(150);
#endif
#ifdef B200
        SERIAL_BAUD(200);
#endif
        SERIAL_BAUD(300);
        SERIAL_BAUD(600);
        SERIAL_BAUD(1200);
#ifdef B1800
        SERIAL_BAUD(1800);
#endif
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
            return Result<speed_t>::failure(Error::Unsupported);
    }
#undef SERIAL_BAUD
}

[[nodiscard]] Result<speed_t> validate_config(const Config& config) noexcept {
    if (!is_valid(config.data_bits) || !is_valid(config.parity) || !is_valid(config.stop_bits) ||
        !is_valid(config.flow_control) || config.baud_rate == 0 ||
        (config.rs485.enabled && config.flow_control == FlowControl::RtsCts) ||
        !is_valid_delay(config.rs485.delay_before_send) ||
        !is_valid_delay(config.rs485.delay_after_send)) {
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

    return to_termios_speed(config.baud_rate);
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

    if (::cfsetispeed(&attributes, speed) < 0) {
        return Result<void>::failure(to_error(errno, ErrorContext::Runtime));
    }
    if (::cfsetospeed(&attributes, speed) < 0) {
        return Result<void>::failure(to_error(errno, ErrorContext::Runtime));
    }
    return Result<void>::success();
}

[[nodiscard]] bool attributes_match(const termios& actual, const termios& requested) noexcept {
    constexpr tcflag_t input_mask = IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL |
                                    INPCK | IGNPAR | IXON | IXOFF | IXANY;
    constexpr tcflag_t output_mask = OPOST;
    constexpr tcflag_t local_mask = ECHO | ECHONL | ICANON | ISIG | IEXTEN;
    tcflag_t control_mask = CSIZE | PARENB | PARODD | CSTOPB | CREAD | CLOCAL;
#ifdef CMSPAR
    control_mask |= CMSPAR;
#endif
#ifdef CRTSCTS
    control_mask |= CRTSCTS;
#endif

    if ((actual.c_iflag & input_mask) != (requested.c_iflag & input_mask)) return false;
    if ((actual.c_oflag & output_mask) != (requested.c_oflag & output_mask)) return false;
    if ((actual.c_lflag & local_mask) != (requested.c_lflag & local_mask)) return false;
    if ((actual.c_cflag & control_mask) != (requested.c_cflag & control_mask)) return false;
    if (actual.c_cc[VMIN] != requested.c_cc[VMIN]) return false;
    if (actual.c_cc[VTIME] != requested.c_cc[VTIME]) return false;
    if (::cfgetispeed(&actual) != ::cfgetispeed(&requested)) return false;
    if (::cfgetospeed(&actual) != ::cfgetospeed(&requested)) return false;
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
    const serial_rs485& actual, const serial_rs485& requested) noexcept {
    unsigned int owned_flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND;
#ifdef SER_RS485_RX_DURING_TX
    owned_flags |= SER_RS485_RX_DURING_TX;
#endif

    return (actual.flags & owned_flags) == (requested.flags & owned_flags) &&
           actual.delay_rts_before_send == requested.delay_rts_before_send &&
           actual.delay_rts_after_send == requested.delay_rts_after_send;
}

void release_open_candidate(int fd) noexcept {
    static_cast<void>(tty::clear_exclusive(fd));
    static_cast<void>(tty::close(fd));
}

void rollback_open(
    int fd, const termios& original_attributes, const serial_rs485& original_rs485,
    bool rs485_attempted) noexcept {
    if (rs485_attempted) static_cast<void>(tty::write_rs485(fd, original_rs485));
    static_cast<void>(tty::write_attributes(fd, TCSANOW, original_attributes));
    release_open_candidate(fd);
}

struct Deadline final {
    timespec absolute{};
    bool immediate_check_pending{false};
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
        return Result<Deadline>::failure(to_error(errno, ErrorContext::Runtime));
    }

    const auto seconds = timeout.count() / kNanosecondsPerSecond;
    const auto nanoseconds = timeout.count() % kNanosecondsPerSecond;
    if (seconds > std::numeric_limits<time_t>::max() - now.tv_sec) {
        return Result<Deadline>::failure(Error::InvalidArgument);
    }

    Deadline deadline{
        {
            now.tv_sec + static_cast<time_t>(seconds),
            now.tv_nsec + static_cast<long>(nanoseconds),
        },
        timeout.count() == 0};
    if (deadline.absolute.tv_nsec >= kNanosecondsPerSecond) {
        ++deadline.absolute.tv_sec;
        deadline.absolute.tv_nsec -= kNanosecondsPerSecond;
    }
    return Result<Deadline>::success(std::move(deadline));
}

[[nodiscard]] Result<void> wait_fd(int fd, short events, Deadline* deadline) noexcept {
    for (;;) {
        timespec remaining{};
        const timespec* timeout = nullptr;
        if (deadline != nullptr) {
            timespec now{};
            if (tty::monotonic_now(now) < 0) {
                return Result<void>::failure(to_error(errno, ErrorContext::Runtime));
            }
            const bool expired = compare_time(now, deadline->absolute) >= 0;
            if (expired && !deadline->immediate_check_pending) {
                return Result<void>::failure(Error::TimedOut);
            }
            if (!expired) {
                remaining = {
                    deadline->absolute.tv_sec - now.tv_sec,
                    deadline->absolute.tv_nsec - now.tv_nsec,
                };
                if (remaining.tv_nsec < 0) {
                    --remaining.tv_sec;
                    remaining.tv_nsec += kNanosecondsPerSecond;
                }
            }
            timeout = &remaining;
        }

        short revents = 0;
        if (deadline != nullptr) deadline->immediate_check_pending = false;
        const int waited = tty::wait(fd, events, timeout, revents);
        if (waited < 0) {
            const int err = errno;
            if (err == EINTR) continue;
            return Result<void>::failure(to_error(err, ErrorContext::Runtime));
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
    int fd, std::byte* data, std::size_t size, Deadline* deadline, TransferMode mode) noexcept {
    if (fd < 0) return Result<std::size_t>::failure(Error::NotOpen);
    if (data == nullptr && size != 0) return Result<std::size_t>::failure(Error::InvalidArgument);
    if (size == 0) return Result<std::size_t>::success(0);

    bool readiness_race_seen = false;
    for (;;) {
        if (mode == TransferMode::Wait) {
            auto ready = wait_fd(fd, POLLIN, deadline);
            if (!ready) return Result<std::size_t>::failure(ready.error());
        }

        const ssize_t transferred = tty::read(fd, data, size);
        if (transferred > 0) {
            return Result<std::size_t>::success(static_cast<std::size_t>(transferred));
        }
        if (transferred == 0) {
            // With VMIN=0 a non-blocking TTY may return zero after a readiness race.
            if (mode == TransferMode::Immediate)
                return Result<std::size_t>::failure(Error::WouldBlock);
            if (readiness_race_seen) return Result<std::size_t>::failure(Error::Io);
            readiness_race_seen = true;
            continue;
        }

        const int err = errno;
        if (err == EINTR) continue;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            if (mode == TransferMode::Immediate)
                return Result<std::size_t>::failure(Error::WouldBlock);
            if (readiness_race_seen) return Result<std::size_t>::failure(Error::Io);
            readiness_race_seen = true;
            continue;
        }
        return Result<std::size_t>::failure(to_error(err, ErrorContext::Runtime));
    }
}

[[nodiscard]] Result<std::size_t> write_transfer(
    int fd, const std::byte* data, std::size_t size, Deadline* deadline,
    TransferMode mode) noexcept {
    if (fd < 0) return Result<std::size_t>::failure(Error::NotOpen);
    if (data == nullptr && size != 0) return Result<std::size_t>::failure(Error::InvalidArgument);
    if (size == 0) return Result<std::size_t>::success(0);

    bool readiness_race_seen = false;
    for (;;) {
        if (mode == TransferMode::Wait) {
            auto ready = wait_fd(fd, POLLOUT, deadline);
            if (!ready) return Result<std::size_t>::failure(ready.error());
        }

        const ssize_t transferred = tty::write(fd, data, size);
        if (transferred > 0) {
            return Result<std::size_t>::success(static_cast<std::size_t>(transferred));
        }
        if (transferred == 0) {
            return Result<std::size_t>::failure(
                mode == TransferMode::Immediate ? Error::WouldBlock : Error::Io);
        }

        const int err = errno;
        if (err == EINTR) continue;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            if (mode == TransferMode::Immediate)
                return Result<std::size_t>::failure(Error::WouldBlock);
            if (readiness_race_seen) return Result<std::size_t>::failure(Error::Io);
            readiness_race_seen = true;
            continue;
        }
        return Result<std::size_t>::failure(to_error(err, ErrorContext::Runtime));
    }
}

[[nodiscard]] Result<bool> read_modem_line(int fd, int line) noexcept {
    if (fd < 0) return Result<bool>::failure(Error::NotOpen);

    int lines = 0;
    if (tty::read_modem_lines(fd, lines) < 0) {
        return Result<bool>::failure(to_error(errno, ErrorContext::Runtime));
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
        return Result<void>::failure(to_error(errno, ErrorContext::Open));
    }

    const int terminal = tty::is_terminal(candidate);
    if (terminal <= 0) {
        const int err = errno;
        static_cast<void>(tty::close(candidate));
        return Result<void>::failure(
            terminal == 0 ? Error::NotTerminal : to_error(err, ErrorContext::Runtime));
    }

    if (tty::set_exclusive(candidate) < 0) {
        const int err = errno;
        static_cast<void>(tty::close(candidate));
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }

    termios original_attributes{};
    if (tty::read_attributes(candidate, original_attributes) < 0) {
        const int err = errno;
        release_open_candidate(candidate);
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }

    serial_rs485 original_rs485{};
    bool rs485_supported = true;
    if (tty::read_rs485(candidate, original_rs485) < 0) {
        const int err = errno;
        if (to_error(err, ErrorContext::Runtime) == Error::Unsupported) {
            rs485_supported = false;
            if (config.rs485.enabled) {
                release_open_candidate(candidate);
                return Result<void>::failure(Error::Unsupported);
            }
        } else {
            release_open_candidate(candidate);
            return Result<void>::failure(to_error(err, ErrorContext::Runtime));
        }
    }

    termios requested_attributes = original_attributes;
    auto configured = configure_attributes(requested_attributes, config, speed.value());
    if (!configured) {
        release_open_candidate(candidate);
        return Result<void>::failure(configured.error());
    }

    if (tty::write_attributes(candidate, TCSANOW, requested_attributes) < 0) {
        const int err = errno;
        rollback_open(candidate, original_attributes, original_rs485, false);
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }

    const auto requested_rs485 = make_rs485_request(config.rs485);
    bool rs485_attempted = false;
    if (rs485_supported) {
        rs485_attempted = true;
        if (tty::write_rs485(candidate, requested_rs485) < 0) {
            const int err = errno;
            rollback_open(candidate, original_attributes, original_rs485, true);
            return Result<void>::failure(to_error(err, ErrorContext::Runtime));
        }
    }

    termios effective_attributes{};
    if (tty::read_attributes(candidate, effective_attributes) < 0) {
        const int err = errno;
        rollback_open(candidate, original_attributes, original_rs485, rs485_attempted);
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
    if (!attributes_match(effective_attributes, requested_attributes)) {
        rollback_open(candidate, original_attributes, original_rs485, rs485_attempted);
        return Result<void>::failure(Error::Unsupported);
    }

    if (rs485_supported) {
        serial_rs485 effective_rs485{};
        if (tty::read_rs485(candidate, effective_rs485) < 0) {
            const int err = errno;
            rollback_open(candidate, original_attributes, original_rs485, true);
            return Result<void>::failure(to_error(err, ErrorContext::Runtime));
        }
        if (!rs485_matches(effective_rs485, requested_rs485)) {
            rollback_open(candidate, original_attributes, original_rs485, true);
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

    int exclusive_error = 0;
    if (tty::clear_exclusive(closing) < 0) exclusive_error = errno;
    if (tty::close(closing) < 0) {
        const int err = errno;
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
    if (exclusive_error != 0) {
        return Result<void>::failure(to_error(exclusive_error, ErrorContext::Runtime));
    }
    return Result<void>::success();
}

hardware::Result<std::size_t, Error> Port::read(std::byte* data, std::size_t size) noexcept {
    return read_transfer(fd_, data, size, nullptr, TransferMode::Wait);
}

hardware::Result<std::size_t, Error> Port::read(
    std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept {
    if (!is_open()) return Result<std::size_t>::failure(Error::NotOpen);
    auto deadline = make_deadline(timeout);
    if (!deadline) return Result<std::size_t>::failure(deadline.error());
    return read_transfer(fd_, data, size, &deadline.value(), TransferMode::Wait);
}

hardware::Result<std::size_t, Error> Port::try_read(std::byte* data, std::size_t size) noexcept {
    return read_transfer(fd_, data, size, nullptr, TransferMode::Immediate);
}

hardware::Result<std::size_t, Error> Port::write(const std::byte* data, std::size_t size) noexcept {
    return write_transfer(fd_, data, size, nullptr, TransferMode::Wait);
}

hardware::Result<std::size_t, Error> Port::write(
    const std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept {
    if (!is_open()) return Result<std::size_t>::failure(Error::NotOpen);
    auto deadline = make_deadline(timeout);
    if (!deadline) return Result<std::size_t>::failure(deadline.error());
    return write_transfer(fd_, data, size, &deadline.value(), TransferMode::Wait);
}

hardware::Result<std::size_t, Error> Port::try_write(
    const std::byte* data, std::size_t size) noexcept {
    return write_transfer(fd_, data, size, nullptr, TransferMode::Immediate);
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
    if (tty::input_queue_size(fd_, value) < 0) {
        const int err = errno;
        return Result<std::size_t>::failure(to_error(err, ErrorContext::Runtime));
    }
    if (value < 0) return Result<std::size_t>::failure(Error::Io);
    return Result<std::size_t>::success(static_cast<std::size_t>(value));
}

hardware::Result<std::size_t, Error> Port::bytes_pending() const noexcept {
    if (!is_open()) return Result<std::size_t>::failure(Error::NotOpen);

    int value = 0;
    if (tty::output_queue_size(fd_, value) < 0) {
        const int err = errno;
        return Result<std::size_t>::failure(to_error(err, ErrorContext::Runtime));
    }
    if (value < 0) return Result<std::size_t>::failure(Error::Io);
    return Result<std::size_t>::success(static_cast<std::size_t>(value));
}

hardware::Result<void, Error> Port::discard_input() noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    if (tty::discard(fd_, TCIFLUSH) < 0) {
        const int err = errno;
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
    return Result<void>::success();
}

hardware::Result<void, Error> Port::discard_output() noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    if (tty::discard(fd_, TCOFLUSH) < 0) {
        const int err = errno;
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
    return Result<void>::success();
}

hardware::Result<void, Error> Port::discard_buffers() noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    if (tty::discard(fd_, TCIOFLUSH) < 0) {
        const int err = errno;
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
    return Result<void>::success();
}

hardware::Result<void, Error> Port::drain() noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);

    for (;;) {
        if (tty::drain(fd_) == 0) return Result<void>::success();
        const int err = errno;
        if (err == EINTR) continue;
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
}

hardware::Result<void, Error> Port::set_rts(bool asserted) noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);
    if (rts_automatic_) return Result<void>::failure(Error::InvalidState);

    if (tty::write_modem_line(fd_, TIOCM_RTS, asserted) < 0) {
        const int err = errno;
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
    return Result<void>::success();
}

hardware::Result<bool, Error> Port::rts() const noexcept { return read_modem_line(fd_, TIOCM_RTS); }

hardware::Result<void, Error> Port::set_dtr(bool asserted) noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);

    if (tty::write_modem_line(fd_, TIOCM_DTR, asserted) < 0) {
        const int err = errno;
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
    return Result<void>::success();
}

hardware::Result<bool, Error> Port::dtr() const noexcept { return read_modem_line(fd_, TIOCM_DTR); }

hardware::Result<bool, Error> Port::cts() const noexcept { return read_modem_line(fd_, TIOCM_CTS); }

hardware::Result<bool, Error> Port::dsr() const noexcept { return read_modem_line(fd_, TIOCM_DSR); }

hardware::Result<bool, Error> Port::ri() const noexcept { return read_modem_line(fd_, TIOCM_RI); }

hardware::Result<bool, Error> Port::dcd() const noexcept { return read_modem_line(fd_, TIOCM_CAR); }

hardware::Result<void, Error> Port::set_break(bool asserted) noexcept {
    if (!is_open()) return Result<void>::failure(Error::NotOpen);

    if (tty::write_break(fd_, asserted) < 0) {
        const int err = errno;
        return Result<void>::failure(to_error(err, ErrorContext::Runtime));
    }
    return Result<void>::success();
}

}  // namespace serial
