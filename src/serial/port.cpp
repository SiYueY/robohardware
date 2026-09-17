#include <serial/port.hpp>

#include "tty_adapter.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <type_traits>

namespace serial {
namespace {
constexpr int kClosedFd = -1;
constexpr long kNanosecondsPerSecond = 1000000000L;

[[nodiscard]] bool unsupported_errno(int error) noexcept {
    return error == ENOTTY || error == EOPNOTSUPP || error == ENOSYS;
}
[[nodiscard]] bool disconnected_errno(int error) noexcept {
    return error == EIO || error == ENXIO || error == ENODEV || error == ECONNRESET;
}
[[nodiscard]] Error errno_error(int error) noexcept {
    if (error == EACCES || error == EPERM) return Error::PermissionDenied;
    if (error == ENOENT) return Error::DeviceNotFound;
    if (error == EBUSY) return Error::Busy;
    if (unsupported_errno(error)) return Error::Unsupported;
    if (disconnected_errno(error)) return Error::Disconnected;
    return Error::Io;
}
[[nodiscard]] bool valid(DataBits value) noexcept {
    return value == DataBits::Five || value == DataBits::Six || value == DataBits::Seven ||
           value == DataBits::Eight;
}
[[nodiscard]] bool valid(Parity value) noexcept {
    return value == Parity::None || value == Parity::Odd || value == Parity::Even ||
           value == Parity::Mark || value == Parity::Space;
}
[[nodiscard]] bool valid(StopBits value) noexcept {
    return value == StopBits::One || value == StopBits::Two;
}
[[nodiscard]] bool valid(FlowControl value) noexcept {
    return value == FlowControl::None || value == FlowControl::XonXoff ||
           value == FlowControl::RtsCts;
}
[[nodiscard]] bool baud_speed(std::uint32_t baud_rate, speed_t& speed) noexcept {
#define SERIAL_SPEED(value) \
    case value:             \
        speed = B##value;   \
        return true
    switch (baud_rate) {
        SERIAL_SPEED(50);
        SERIAL_SPEED(75);
        SERIAL_SPEED(110);
        SERIAL_SPEED(300);
        SERIAL_SPEED(600);
        SERIAL_SPEED(1200);
        SERIAL_SPEED(2400);
        SERIAL_SPEED(4800);
        SERIAL_SPEED(9600);
        SERIAL_SPEED(19200);
        SERIAL_SPEED(38400);
        SERIAL_SPEED(57600);
        SERIAL_SPEED(115200);
        SERIAL_SPEED(230400);
#ifdef B460800
        SERIAL_SPEED(460800);
#endif
#ifdef B500000
        SERIAL_SPEED(500000);
#endif
#ifdef B576000
        SERIAL_SPEED(576000);
#endif
#ifdef B921600
        SERIAL_SPEED(921600);
#endif
#ifdef B1000000
        SERIAL_SPEED(1000000);
#endif
#ifdef B1152000
        SERIAL_SPEED(1152000);
#endif
#ifdef B1500000
        SERIAL_SPEED(1500000);
#endif
#ifdef B2000000
        SERIAL_SPEED(2000000);
#endif
#ifdef B2500000
        SERIAL_SPEED(2500000);
#endif
#ifdef B3000000
        SERIAL_SPEED(3000000);
#endif
#ifdef B3500000
        SERIAL_SPEED(3500000);
#endif
#ifdef B4000000
        SERIAL_SPEED(4000000);
#endif
        default:
            return false;
    }
#undef SERIAL_SPEED
}
[[nodiscard]] Error validate_config(const Config& config, speed_t& speed) noexcept {
    if (!valid(config.data_bits) || !valid(config.parity) || !valid(config.stop_bits) ||
        !valid(config.flow_control) || config.rs485.delay_before_send.count() < 0 ||
        config.rs485.delay_after_send.count() < 0 ||
        config.rs485.delay_before_send.count() > std::numeric_limits<std::uint32_t>::max() ||
        config.rs485.delay_after_send.count() > std::numeric_limits<std::uint32_t>::max() ||
        !baud_speed(config.baud_rate, speed))
        return Error::InvalidArgument;
#ifndef CMSPAR
    if (config.parity == Parity::Mark || config.parity == Parity::Space) return Error::Unsupported;
#endif
#ifndef CRTSCTS
    if (config.flow_control == FlowControl::RtsCts) return Error::Unsupported;
#endif
    return Error::Io;
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
[[nodiscard]] bool apply_configuration(
    termios& attributes, const Config& config, speed_t speed) noexcept {
    apply_raw_mode(attributes);
    attributes.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CMSPAR
    attributes.c_cflag &= ~CMSPAR;
#endif
#ifdef CRTSCTS
    attributes.c_cflag &= ~CRTSCTS;
#endif
    attributes.c_cflag |= config.data_bits == DataBits::Five    ? CS5
                          : config.data_bits == DataBits::Six   ? CS6
                          : config.data_bits == DataBits::Seven ? CS7
                                                                : CS8;
    if (config.parity != Parity::None) attributes.c_cflag |= PARENB;
    if (config.parity == Parity::Odd || config.parity == Parity::Mark) attributes.c_cflag |= PARODD;
#ifdef CMSPAR
    if (config.parity == Parity::Mark || config.parity == Parity::Space)
        attributes.c_cflag |= CMSPAR;
#endif
    if (config.stop_bits == StopBits::Two) attributes.c_cflag |= CSTOPB;
    if (config.flow_control == FlowControl::XonXoff) attributes.c_iflag |= IXON | IXOFF;
#ifdef CRTSCTS
    if (config.flow_control == FlowControl::RtsCts) attributes.c_cflag |= CRTSCTS;
#endif
    return ::cfsetispeed(&attributes, speed) == 0 && ::cfsetospeed(&attributes, speed) == 0;
}
[[nodiscard]] bool termios_matches(
    const termios& attributes, const Config& config, speed_t speed) noexcept {
    const tcflag_t expected_input = config.flow_control == FlowControl::XonXoff ? IXON | IXOFF : 0;
    const tcflag_t raw_input =
        IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | INPCK | IGNPAR | IXANY;
    if ((attributes.c_iflag & (raw_input | IXON | IXOFF)) != expected_input ||
        (attributes.c_oflag & OPOST) != 0 ||
        (attributes.c_lflag & (ECHO | ECHONL | ICANON | ISIG | IEXTEN)) != 0 ||
        (attributes.c_cflag & (CREAD | CLOCAL)) != (CREAD | CLOCAL) || attributes.c_cc[VMIN] != 0 ||
        attributes.c_cc[VTIME] != 0 || ::cfgetispeed(&attributes) != speed ||
        ::cfgetospeed(&attributes) != speed)
        return false;
    tcflag_t expected = config.data_bits == DataBits::Five    ? CS5
                        : config.data_bits == DataBits::Six   ? CS6
                        : config.data_bits == DataBits::Seven ? CS7
                                                              : CS8;
    if (config.parity != Parity::None) expected |= PARENB;
    if (config.parity == Parity::Odd || config.parity == Parity::Mark) expected |= PARODD;
#ifdef CMSPAR
    if (config.parity == Parity::Mark || config.parity == Parity::Space) expected |= CMSPAR;
#endif
    if (config.stop_bits == StopBits::Two) expected |= CSTOPB;
    if ((attributes.c_cflag & (CSIZE | PARENB | PARODD | CSTOPB
#ifdef CMSPAR
                               | CMSPAR
#endif
                               )) != expected)
        return false;
#ifdef CRTSCTS
    if (((attributes.c_cflag & CRTSCTS) != 0) != (config.flow_control == FlowControl::RtsCts))
        return false;
#endif
    return true;
}
[[nodiscard]] serial_rs485 requested_rs485(const Config::RS485& config) noexcept {
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
[[nodiscard]] bool rs485_matches(const serial_rs485& actual, const Config::RS485& config) noexcept {
    const auto request = requested_rs485(config);
    unsigned int flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND;
#ifdef SER_RS485_RX_DURING_TX
    flags |= SER_RS485_RX_DURING_TX;
#endif
    return (actual.flags & flags) == (request.flags & flags) &&
           actual.delay_rts_before_send == request.delay_rts_before_send &&
           actual.delay_rts_after_send == request.delay_rts_after_send;
}
void rollback(
    int fd, const termios& original, const serial_rs485& original_rs485, bool termios_changed,
    bool rs485_changed) noexcept {
    if (rs485_changed) static_cast<void>(tty_adapter::set_rs485(fd, original_rs485));
    if (termios_changed) static_cast<void>(tty_adapter::set_termios(fd, TCSANOW, original));
    static_cast<void>(tty_adapter::close_fd(fd));
}
struct Deadline final {
    timespec absolute{};
};
[[nodiscard]] int compare_time(const timespec& left, const timespec& right) noexcept {
    if (left.tv_sec != right.tv_sec) return left.tv_sec < right.tv_sec ? -1 : 1;
    return left.tv_nsec == right.tv_nsec ? 0 : (left.tv_nsec < right.tv_nsec ? -1 : 1);
}
[[nodiscard]] Error make_deadline(std::chrono::nanoseconds timeout, Deadline& deadline) noexcept {
    if (timeout.count() < 0) return Error::InvalidArgument;
    timespec now{};
    const auto clock = tty_adapter::monotonic_now(now);
    if (clock.value < 0) return errno_error(clock.error);
    const auto seconds = timeout.count() / kNanosecondsPerSecond;
    const auto nanoseconds = timeout.count() % kNanosecondsPerSecond;
    if (seconds > std::numeric_limits<time_t>::max() - now.tv_sec) return Error::InvalidArgument;
    deadline.absolute = {
        now.tv_sec + static_cast<time_t>(seconds), now.tv_nsec + static_cast<long>(nanoseconds)};
    if (deadline.absolute.tv_nsec >= kNanosecondsPerSecond) {
        ++deadline.absolute.tv_sec;
        deadline.absolute.tv_nsec -= kNanosecondsPerSecond;
    }
    return Error::Io;
}
[[nodiscard]] Error wait_fd(int fd, short events, const Deadline* deadline) noexcept {
    for (;;) {
        timespec remaining{};
        const timespec* timeout = nullptr;
        if (deadline != nullptr) {
            timespec now{};
            const auto clock = tty_adapter::monotonic_now(now);
            if (clock.value < 0) return errno_error(clock.error);
            if (compare_time(now, deadline->absolute) >= 0) return Error::TimedOut;
            remaining = {
                deadline->absolute.tv_sec - now.tv_sec, deadline->absolute.tv_nsec - now.tv_nsec};
            if (remaining.tv_nsec < 0) {
                --remaining.tv_sec;
                remaining.tv_nsec += kNanosecondsPerSecond;
            }
            timeout = &remaining;
        }
        short revents = 0;
        const auto waited = tty_adapter::wait(fd, events, timeout, revents);
        if (waited.value < 0) {
            if (waited.error == EINTR) continue;
            return errno_error(waited.error);
        }
        if (waited.value == 0) return Error::TimedOut;
        if ((revents & POLLNVAL) != 0) return Error::NotOpen;
        if ((revents & (POLLHUP | POLLERR)) != 0 && (revents & events) == 0)
            return Error::Disconnected;
        if ((revents & events) != 0) return Error::Io;
    }
}
template <typename Pointer>
[[nodiscard]] hardware::Result<std::size_t, Error> transfer(
    int fd, Pointer data, std::size_t size, bool output, const Deadline* deadline,
    bool immediate) noexcept {
    if (fd < 0) return hardware::Result<std::size_t, Error>::failure(Error::NotOpen);
    if (data == nullptr && size != 0)
        return hardware::Result<std::size_t, Error>::failure(Error::InvalidArgument);
    if (size == 0) return hardware::Result<std::size_t, Error>::success(0);
    for (;;) {
        if (!immediate) {
            const auto ready = wait_fd(fd, output ? POLLOUT : POLLIN, deadline);
            if (ready != Error::Io) return hardware::Result<std::size_t, Error>::failure(ready);
        }
        const auto result = [&]() noexcept {
            if constexpr (std::is_const_v<std::remove_pointer_t<Pointer>>) {
                return tty_adapter::write_bytes(fd, data, size);
            } else {
                return tty_adapter::read_bytes(fd, data, size);
            }
        }();
        if (result.value > 0)
            return hardware::Result<std::size_t, Error>::success(
                static_cast<std::size_t>(result.value));
        if (result.value == 0) {
            // A raw TTY with VMIN=0 may report a zero-byte read after a
            // readiness race. It is not EOF; bounded and blocking reads keep
            // their existing deadline, while immediate reads report no progress.
            if (!output) {
                if (immediate)
                    return hardware::Result<std::size_t, Error>::failure(Error::WouldBlock);
                continue;
            }
            return hardware::Result<std::size_t, Error>::failure(Error::Disconnected);
        }
        if (result.error == EINTR) continue;
        if (result.error == EAGAIN || result.error == EWOULDBLOCK) {
            if (immediate) return hardware::Result<std::size_t, Error>::failure(Error::WouldBlock);
            continue;
        }
        return hardware::Result<std::size_t, Error>::failure(errno_error(result.error));
    }
}
[[nodiscard]] hardware::Result<bool, Error> modem_line(int fd, int line) noexcept {
    if (fd < 0) return hardware::Result<bool, Error>::failure(Error::NotOpen);
    int lines = 0;
    const auto result = tty_adapter::get_modem_lines(fd, lines);
    return result.value < 0 ? hardware::Result<bool, Error>::failure(errno_error(result.error))
                            : hardware::Result<bool, Error>::success((lines & line) != 0);
}
}  // namespace

Port::~Port() noexcept { static_cast<void>(close()); }
Port::Port(Port&& other) noexcept : fd_(other.fd_), rts_automatic_(other.rts_automatic_) {
    other.fd_ = kClosedFd;
    other.rts_automatic_ = false;
}
bool Port::is_open() const noexcept { return fd_ >= 0; }
hardware::Result<void, Error> Port::open(const std::string& path, const Config& config) noexcept {
    if (fd_ >= 0) return hardware::Result<void, Error>::failure(Error::AlreadyOpen);
    if (path.empty() || path.find('\0') != std::string::npos)
        return hardware::Result<void, Error>::failure(Error::InvalidArgument);
    speed_t speed{};
    const auto validation = validate_config(config, speed);
    if (validation != Error::Io) return hardware::Result<void, Error>::failure(validation);
    const auto opened =
        tty_adapter::open_path(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (opened.value < 0) return hardware::Result<void, Error>::failure(errno_error(opened.error));
    const int candidate = static_cast<int>(opened.value);
    const auto tty = tty_adapter::is_tty(candidate);
    if (tty.value <= 0) {
        static_cast<void>(tty_adapter::close_fd(candidate));
        return hardware::Result<void, Error>::failure(
            tty.value == 0 ? Error::NotTerminal : errno_error(tty.error));
    }
    termios original{};
    const auto got = tty_adapter::get_termios(candidate, original);
    if (got.value < 0) {
        static_cast<void>(tty_adapter::close_fd(candidate));
        return hardware::Result<void, Error>::failure(errno_error(got.error));
    }
    serial_rs485 original_rs485{};
    const auto got_rs485 = tty_adapter::get_rs485(candidate, original_rs485);
    const bool rs485_supported = got_rs485.value >= 0;
    if (!rs485_supported && config.rs485.enabled) {
        static_cast<void>(tty_adapter::close_fd(candidate));
        return hardware::Result<void, Error>::failure(
            unsupported_errno(got_rs485.error) ? Error::Unsupported : errno_error(got_rs485.error));
    }
    termios requested = original;
    if (!apply_configuration(requested, config, speed)) {
        static_cast<void>(tty_adapter::close_fd(candidate));
        return hardware::Result<void, Error>::failure(Error::InvalidArgument);
    }
    const auto set = tty_adapter::set_termios(candidate, TCSANOW, requested);
    if (set.value < 0) {
        rollback(candidate, original, original_rs485, false, false);
        return hardware::Result<void, Error>::failure(errno_error(set.error));
    }
    bool rs485_changed = false;
    if (rs485_supported) {
        const auto set_rs485 = tty_adapter::set_rs485(candidate, requested_rs485(config.rs485));
        if (set_rs485.value < 0) {
            rollback(candidate, original, original_rs485, true, false);
            return hardware::Result<void, Error>::failure(
                unsupported_errno(set_rs485.error) ? Error::Unsupported
                                                   : errno_error(set_rs485.error));
        }
        rs485_changed = true;
    }
    termios effective{};
    const auto readback = tty_adapter::get_termios(candidate, effective);
    if (readback.value < 0 || !termios_matches(effective, config, speed)) {
        rollback(candidate, original, original_rs485, true, rs485_changed);
        return hardware::Result<void, Error>::failure(
            readback.value < 0 ? errno_error(readback.error) : Error::Unsupported);
    }
    if (rs485_supported) {
        serial_rs485 effective_rs485{};
        const auto rs_readback = tty_adapter::get_rs485(candidate, effective_rs485);
        if (rs_readback.value < 0 || !rs485_matches(effective_rs485, config.rs485)) {
            rollback(candidate, original, original_rs485, true, rs485_changed);
            return hardware::Result<void, Error>::failure(
                rs_readback.value < 0 ? errno_error(rs_readback.error) : Error::Unsupported);
        }
    }
    fd_ = candidate;
    rts_automatic_ = config.rs485.enabled || config.flow_control == FlowControl::RtsCts;
    return hardware::Result<void, Error>::success();
}
hardware::Result<void, Error> Port::close() noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::success();
    const int closing = fd_;
    fd_ = kClosedFd;
    rts_automatic_ = false;
    const auto result = tty_adapter::close_fd(closing);
    return result.value < 0 ? hardware::Result<void, Error>::failure(errno_error(result.error))
                            : hardware::Result<void, Error>::success();
}
hardware::Result<std::size_t, Error> Port::read(std::byte* data, std::size_t size) noexcept {
    return transfer(fd_, data, size, false, nullptr, false);
}
hardware::Result<std::size_t, Error> Port::read(
    std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept {
    Deadline deadline{};
    const auto error = make_deadline(timeout, deadline);
    return error != Error::Io ? hardware::Result<std::size_t, Error>::failure(error)
                              : transfer(fd_, data, size, false, &deadline, false);
}
hardware::Result<std::size_t, Error> Port::try_read(std::byte* data, std::size_t size) noexcept {
    return transfer(fd_, data, size, false, nullptr, true);
}
hardware::Result<std::size_t, Error> Port::write(const std::byte* data, std::size_t size) noexcept {
    return transfer(fd_, data, size, true, nullptr, false);
}
hardware::Result<std::size_t, Error> Port::write(
    const std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept {
    Deadline deadline{};
    const auto error = make_deadline(timeout, deadline);
    return error != Error::Io ? hardware::Result<std::size_t, Error>::failure(error)
                              : transfer(fd_, data, size, true, &deadline, false);
}
hardware::Result<std::size_t, Error> Port::try_write(
    const std::byte* data, std::size_t size) noexcept {
    return transfer(fd_, data, size, true, nullptr, true);
}
hardware::Result<void, Error> Port::wait_readable(std::chrono::nanoseconds timeout) noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    Deadline deadline{};
    const auto error = make_deadline(timeout, deadline);
    if (error != Error::Io) return hardware::Result<void, Error>::failure(error);
    const auto waited = wait_fd(fd_, POLLIN, &deadline);
    return waited == Error::Io ? hardware::Result<void, Error>::success()
                               : hardware::Result<void, Error>::failure(waited);
}
hardware::Result<void, Error> Port::wait_writable(std::chrono::nanoseconds timeout) noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    Deadline deadline{};
    const auto error = make_deadline(timeout, deadline);
    if (error != Error::Io) return hardware::Result<void, Error>::failure(error);
    const auto waited = wait_fd(fd_, POLLOUT, &deadline);
    return waited == Error::Io ? hardware::Result<void, Error>::success()
                               : hardware::Result<void, Error>::failure(waited);
}
hardware::Result<std::size_t, Error> Port::bytes_available() const noexcept {
    if (fd_ < 0) return hardware::Result<std::size_t, Error>::failure(Error::NotOpen);
    int value = 0;
    const auto result = tty_adapter::input_queue_size(fd_, value);
    return result.value < 0 || value < 0
               ? hardware::Result<std::size_t, Error>::failure(errno_error(result.error))
               : hardware::Result<std::size_t, Error>::success(static_cast<std::size_t>(value));
}
hardware::Result<std::size_t, Error> Port::bytes_pending() const noexcept {
    if (fd_ < 0) return hardware::Result<std::size_t, Error>::failure(Error::NotOpen);
    int value = 0;
    const auto result = tty_adapter::output_queue_size(fd_, value);
    return result.value < 0 || value < 0
               ? hardware::Result<std::size_t, Error>::failure(errno_error(result.error))
               : hardware::Result<std::size_t, Error>::success(static_cast<std::size_t>(value));
}
hardware::Result<void, Error> Port::discard_input() noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    const auto result = tty_adapter::flush(fd_, TCIFLUSH);
    return result.value < 0 ? hardware::Result<void, Error>::failure(errno_error(result.error))
                            : hardware::Result<void, Error>::success();
}
hardware::Result<void, Error> Port::discard_output() noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    const auto result = tty_adapter::flush(fd_, TCOFLUSH);
    return result.value < 0 ? hardware::Result<void, Error>::failure(errno_error(result.error))
                            : hardware::Result<void, Error>::success();
}
hardware::Result<void, Error> Port::discard_buffers() noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    const auto result = tty_adapter::flush(fd_, TCIOFLUSH);
    return result.value < 0 ? hardware::Result<void, Error>::failure(errno_error(result.error))
                            : hardware::Result<void, Error>::success();
}
hardware::Result<void, Error> Port::drain() noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    for (;;) {
        const auto result = tty_adapter::drain(fd_);
        if (result.value >= 0) return hardware::Result<void, Error>::success();
        if (result.error != EINTR)
            return hardware::Result<void, Error>::failure(errno_error(result.error));
    }
}
hardware::Result<void, Error> Port::set_rts(bool asserted) noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    if (rts_automatic_) return hardware::Result<void, Error>::failure(Error::InvalidState);
    const auto result = tty_adapter::set_modem_lines(fd_, TIOCM_RTS, asserted);
    return result.value < 0 ? hardware::Result<void, Error>::failure(errno_error(result.error))
                            : hardware::Result<void, Error>::success();
}
hardware::Result<bool, Error> Port::rts() const noexcept { return modem_line(fd_, TIOCM_RTS); }
hardware::Result<void, Error> Port::set_dtr(bool asserted) noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    const auto result = tty_adapter::set_modem_lines(fd_, TIOCM_DTR, asserted);
    return result.value < 0 ? hardware::Result<void, Error>::failure(errno_error(result.error))
                            : hardware::Result<void, Error>::success();
}
hardware::Result<bool, Error> Port::dtr() const noexcept { return modem_line(fd_, TIOCM_DTR); }
hardware::Result<bool, Error> Port::cts() const noexcept { return modem_line(fd_, TIOCM_CTS); }
hardware::Result<bool, Error> Port::dsr() const noexcept { return modem_line(fd_, TIOCM_DSR); }
hardware::Result<bool, Error> Port::ri() const noexcept { return modem_line(fd_, TIOCM_RI); }
hardware::Result<bool, Error> Port::dcd() const noexcept { return modem_line(fd_, TIOCM_CAR); }
hardware::Result<void, Error> Port::set_break(bool asserted) noexcept {
    if (fd_ < 0) return hardware::Result<void, Error>::failure(Error::NotOpen);
    const auto result = tty_adapter::set_break(fd_, asserted);
    return result.value < 0 ? hardware::Result<void, Error>::failure(errno_error(result.error))
                            : hardware::Result<void, Error>::success();
}
}  // namespace serial
