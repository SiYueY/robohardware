#include "serial/port.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

namespace {

// Kept private because glibc's termios headers conflict with asm/termbits.h.
struct LinuxTermios2 {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[19];
    unsigned int c_ispeed;
    unsigned int c_ospeed;
};

static_assert(sizeof(LinuxTermios2) == 44, "Linux termios2 UAPI layout changed");

constexpr unsigned long kTcgets2 = _IOR('T', 0x2A, LinuxTermios2);
constexpr unsigned long kTcsets2 = _IOW('T', 0x2B, LinuxTermios2);
constexpr unsigned int kCbaud = 0010017U;
constexpr unsigned int kBother = 0010000U;

serial::Error io_error() noexcept { return {serial::ErrorCode::IoFailed, errno}; }

serial::Error capability_error() noexcept {
    const int code = errno;
    if (code == ENOTTY || code == EOPNOTSUPP || code == ENOSYS) {
        return {serial::ErrorCode::Unsupported, code};
    }
    return {serial::ErrorCode::IoFailed, code};
}

serial::Error configure_error(const serial::Config& config, int code) noexcept {
    if (config.flow_control == serial::FlowControl::Hardware &&
        (code == EINVAL || code == ENOTTY || code == EOPNOTSUPP)) {
        return {serial::ErrorCode::Unsupported, code};
    }
    return {serial::ErrorCode::ConfigureFailed, code};
}

bool valid(const serial::Config& config) noexcept {
    return !config.device.empty() && config.baud_rate != 0 &&
           (config.data_bits == serial::DataBits::Five ||
            config.data_bits == serial::DataBits::Six ||
            config.data_bits == serial::DataBits::Seven ||
            config.data_bits == serial::DataBits::Eight) &&
           (config.parity == serial::Parity::None || config.parity == serial::Parity::Odd ||
            config.parity == serial::Parity::Even) &&
           (config.stop_bits == serial::StopBits::One ||
            config.stop_bits == serial::StopBits::Two) &&
           (config.flow_control == serial::FlowControl::None ||
            config.flow_control == serial::FlowControl::Software ||
            config.flow_control == serial::FlowControl::Hardware);
}

bool standard_baud(std::uint32_t baud, speed_t& native) noexcept {
    switch (baud) {
        case 9600:
            native = B9600;
            return true;
        case 19200:
            native = B19200;
            return true;
        case 38400:
            native = B38400;
            return true;
        case 57600:
            native = B57600;
            return true;
        case 115200:
            native = B115200;
            return true;
        case 230400:
            native = B230400;
            return true;
        case 460800:
            native = B460800;
            return true;
        case 500000:
            native = B500000;
            return true;
        case 576000:
            native = B576000;
            return true;
        case 921600:
            native = B921600;
            return true;
        case 1000000:
            native = B1000000;
            return true;
        default:
            return false;
    }
}

serial::Result<void> configure(int fd, const serial::Config& config) noexcept {
    ::termios tty{};
    if (::tcgetattr(fd, &tty) < 0) {
        return serial::Error{serial::ErrorCode::ConfigureFailed, errno};
    }
    const ::termios original = tty;

    ::cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSIZE;
    switch (config.data_bits) {
        case serial::DataBits::Five:
            tty.c_cflag |= CS5;
            break;
        case serial::DataBits::Six:
            tty.c_cflag |= CS6;
            break;
        case serial::DataBits::Seven:
            tty.c_cflag |= CS7;
            break;
        case serial::DataBits::Eight:
            tty.c_cflag |= CS8;
            break;
    }

    tty.c_cflag &= ~(PARENB | PARODD);
    if (config.parity == serial::Parity::Odd) tty.c_cflag |= PARENB | PARODD;
    if (config.parity == serial::Parity::Even) tty.c_cflag |= PARENB;
    if (config.stop_bits == serial::StopBits::Two)
        tty.c_cflag |= CSTOPB;
    else
        tty.c_cflag &= ~CSTOPB;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    if (config.flow_control == serial::FlowControl::Software) tty.c_iflag |= IXON | IXOFF;
#ifdef CRTSCTS
    if (config.flow_control == serial::FlowControl::Hardware)
        tty.c_cflag |= CRTSCTS;
    else
        tty.c_cflag &= ~CRTSCTS;
#else
    if (config.flow_control == serial::FlowControl::Hardware) {
        return serial::Error{serial::ErrorCode::Unsupported};
    }
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    speed_t speed{};
    const bool has_standard_baud = standard_baud(config.baud_rate, speed);
    if (::cfsetispeed(&tty, has_standard_baud ? speed : B38400) < 0 ||
        ::cfsetospeed(&tty, has_standard_baud ? speed : B38400) < 0 ||
        ::tcsetattr(fd, TCSANOW, &tty) < 0) {
        return configure_error(config, errno);
    }
    if (has_standard_baud) return {};

    LinuxTermios2 tty2{};
    if (::ioctl(fd, kTcgets2, &tty2) < 0) {
        const int code = errno;
        ::tcsetattr(fd, TCSANOW, &original);
        return serial::Error{
            code == ENOTTY || code == EINVAL ? serial::ErrorCode::Unsupported
                                             : serial::ErrorCode::ConfigureFailed,
            code};
    }
    tty2.c_cflag &= ~kCbaud;
    tty2.c_cflag |= kBother;
    tty2.c_ispeed = config.baud_rate;
    tty2.c_ospeed = config.baud_rate;
    if (::ioctl(fd, kTcsets2, &tty2) < 0) {
        const int code = errno;
        ::tcsetattr(fd, TCSANOW, &original);
        return serial::Error{
            code == ENOTTY || code == EINVAL || code == EOPNOTSUPP
                ? serial::ErrorCode::Unsupported
                : serial::ErrorCode::ConfigureFailed,
            code};
    }
    return {};
}

serial::Result<bool> wait_for(int fd, short events, std::chrono::milliseconds timeout) noexcept {
    if (fd < 0) return serial::Error{serial::ErrorCode::InvalidState};
    if (timeout.count() < 0) return serial::Error{serial::ErrorCode::InvalidArgument};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        const auto rounded = std::chrono::duration_cast<std::chrono::milliseconds>(
            remaining + std::chrono::milliseconds(1) - std::chrono::steady_clock::duration(1));
        const int milliseconds = remaining <= std::chrono::steady_clock::duration::zero()
                                     ? 0
                                     : (rounded.count() > std::numeric_limits<int>::max()
                                            ? std::numeric_limits<int>::max()
                                            : static_cast<int>(rounded.count()));
        ::pollfd pollfd{fd, events, 0};
        const int result = ::poll(&pollfd, 1, milliseconds);
        if (result == 0) return false;
        if (result < 0) {
            if (errno == EINTR) continue;
            return io_error();
        }
        if ((pollfd.revents & POLLNVAL) != 0)
            return serial::Error{serial::ErrorCode::IoFailed, EBADF};
        if ((pollfd.revents & (POLLERR | POLLHUP)) != 0)
            return serial::Error{serial::ErrorCode::IoFailed, EIO};
        return (pollfd.revents & events) != 0;
    }
}

}  // namespace

namespace serial {

Port::Port(int fd, Config config) noexcept : fd_(fd), config_(std::move(config)) {}

Result<Port> Port::open(Config config) {
    if (!valid(config)) return Error{ErrorCode::InvalidArgument};
    const int fd = ::open(config.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return Error{ErrorCode::OpenFailed, errno};
    const Result<void> configured = configure(fd, config);
    if (!configured) {
        ::close(fd);
        return configured.error();
    }
    return Port(fd, std::move(config));
}

Port::~Port() { close(); }

Port::Port(Port&& other) noexcept
: fd_(std::exchange(other.fd_, -1)),
  config_(std::move(other.config_)),
  rx_bytes_(other.rx_bytes_.load(std::memory_order_relaxed)),
  tx_bytes_(other.tx_bytes_.load(std::memory_order_relaxed)),
  read_calls_(other.read_calls_.load(std::memory_order_relaxed)),
  write_calls_(other.write_calls_.load(std::memory_order_relaxed)),
  read_errors_(other.read_errors_.load(std::memory_order_relaxed)),
  write_errors_(other.write_errors_.load(std::memory_order_relaxed)) {}

Port& Port::operator=(Port&& other) noexcept {
    if (this == &other) return *this;
    close();
    fd_ = std::exchange(other.fd_, -1);
    config_ = std::move(other.config_);
    rx_bytes_.store(other.rx_bytes_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    tx_bytes_.store(other.tx_bytes_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    read_calls_.store(other.read_calls_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    write_calls_.store(
        other.write_calls_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    read_errors_.store(
        other.read_errors_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    write_errors_.store(
        other.write_errors_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
}

void Port::close() noexcept {
    if (fd_ >= 0) ::close(std::exchange(fd_, -1));
}

Result<std::size_t> Port::read(std::uint8_t* data, std::size_t size) noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    if (size == 0) return std::size_t{0};
    if (data == nullptr) return Error{ErrorCode::InvalidArgument};
    ++read_calls_;
    const ssize_t count = ::read(fd_, data, size);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return std::size_t{0};
        ++read_errors_;
        return io_error();
    }
    rx_bytes_.fetch_add(static_cast<std::size_t>(count), std::memory_order_relaxed);
    return static_cast<std::size_t>(count);
}

Result<std::size_t> Port::write(const std::uint8_t* data, std::size_t size) noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    if (size == 0) return std::size_t{0};
    if (data == nullptr) return Error{ErrorCode::InvalidArgument};
    ++write_calls_;
    const ssize_t count = ::write(fd_, data, size);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return std::size_t{0};
        ++write_errors_;
        return io_error();
    }
    tx_bytes_.fetch_add(static_cast<std::size_t>(count), std::memory_order_relaxed);
    return static_cast<std::size_t>(count);
}

Result<bool> Port::wait_readable(std::chrono::milliseconds timeout) noexcept {
    return wait_for(fd_, POLLIN, timeout);
}

Result<bool> Port::wait_writable(std::chrono::milliseconds timeout) noexcept {
    return wait_for(fd_, POLLOUT, timeout);
}

Result<std::size_t> Port::available() const noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    int bytes = 0;
    if (::ioctl(fd_, TIOCINQ, &bytes) < 0) return io_error();
    return static_cast<std::size_t>(bytes);
}

Result<std::size_t> Port::pending_write() const noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    int bytes = 0;
    if (::ioctl(fd_, TIOCOUTQ, &bytes) < 0) return io_error();
    return static_cast<std::size_t>(bytes);
}

Result<void> Port::flush_input() noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    if (::tcflush(fd_, TCIFLUSH) < 0) return io_error();
    return {};
}

Result<void> Port::flush_output() noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    if (::tcflush(fd_, TCOFLUSH) < 0) return io_error();
    return {};
}

Result<void> Port::drain() noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    while (::tcdrain(fd_) < 0) {
        if (errno != EINTR) return io_error();
    }
    return {};
}

Result<void> Port::set_break(bool enabled) noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    if (::ioctl(fd_, enabled ? TIOCSBRK : TIOCCBRK) < 0) return capability_error();
    return {};
}

Result<void> Port::set_rts(bool enabled) noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    if (config_.flow_control == FlowControl::Hardware) return Error{ErrorCode::InvalidState};
    const int line = TIOCM_RTS;
    if (::ioctl(fd_, enabled ? TIOCMBIS : TIOCMBIC, &line) < 0) return capability_error();
    return {};
}

Result<void> Port::set_dtr(bool enabled) noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    const int line = TIOCM_DTR;
    if (::ioctl(fd_, enabled ? TIOCMBIS : TIOCMBIC, &line) < 0) return capability_error();
    return {};
}

Result<Signals> Port::signals() const noexcept {
    if (fd_ < 0) return Error{ErrorCode::InvalidState};
    int lines = 0;
    if (::ioctl(fd_, TIOCMGET, &lines) < 0) return capability_error();
    return Signals{
        (lines & TIOCM_CTS) != 0, (lines & TIOCM_DSR) != 0, (lines & TIOCM_CAR) != 0,
        (lines & TIOCM_RI) != 0};
}

const Config& Port::config() const noexcept { return config_; }

Stats Port::stats() const noexcept {
    return {rx_bytes_.load(std::memory_order_relaxed),
            tx_bytes_.load(std::memory_order_relaxed),
            read_calls_.load(std::memory_order_relaxed),
            write_calls_.load(std::memory_order_relaxed),
            read_errors_.load(std::memory_order_relaxed),
            write_errors_.load(std::memory_order_relaxed)};
}

int Port::fd() const noexcept { return fd_; }

}  // namespace serial
