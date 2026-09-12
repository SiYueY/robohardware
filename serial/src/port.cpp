#include <serial/port.hpp>

#include "tty_adapter.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <termios.h>

namespace serial {

// This private accessor keeps configuration types closed to consumers while
// allowing the Linux implementation helpers to inspect a requested value.
struct PortAccess final {
  [[nodiscard]] static bool is_immediate(const Timeout& value) noexcept {
    return value.kind_ == Timeout::Kind::Immediate;
  }
  [[nodiscard]] static bool is_infinite(const Timeout& value) noexcept {
    return value.kind_ == Timeout::Kind::Infinite;
  }
  [[nodiscard]] static bool is_finite(const Timeout& value) noexcept {
    return value.kind_ == Timeout::Kind::Finite;
  }
  [[nodiscard]] static std::chrono::nanoseconds duration(const Timeout& value) noexcept {
    return value.duration_;
  }
  [[nodiscard]] static std::uint32_t baud_rate(const PortConfig& value) noexcept {
    return value.baud_rate_;
  }
  [[nodiscard]] static DataBits data_bits(const PortConfig& value) noexcept {
    return value.data_bits_;
  }
  [[nodiscard]] static Parity parity(const PortConfig& value) noexcept {
    return value.parity_;
  }
  [[nodiscard]] static StopBits stop_bits(const PortConfig& value) noexcept {
    return value.stop_bits_;
  }
  [[nodiscard]] static FlowControl flow_control(const PortConfig& value) noexcept {
    return value.flow_control_;
  }
  [[nodiscard]] static const Rs485Config& rs485(const PortConfig& value) noexcept {
    return value.rs485_;
  }
  [[nodiscard]] static bool enabled(const Rs485Config& value) noexcept {
    return value.enabled_;
  }
  [[nodiscard]] static RtsLevel rts_during_send(const Rs485Config& value) noexcept {
    return value.rts_during_send_;
  }
  [[nodiscard]] static RtsLevel rts_after_send(const Rs485Config& value) noexcept {
    return value.rts_after_send_;
  }
  [[nodiscard]] static std::chrono::milliseconds delay_before_send(
      const Rs485Config& value) noexcept {
    return value.delay_before_send_;
  }
  [[nodiscard]] static std::chrono::milliseconds delay_after_send(
      const Rs485Config& value) noexcept {
    return value.delay_after_send_;
  }
};

namespace {

constexpr int kClosedFd = -1;

[[nodiscard]] std::error_code generic(std::errc value) noexcept {
  return std::make_error_code(value);
}
[[nodiscard]] std::error_code system(int value) noexcept {
  return {value, std::system_category()};
}
[[nodiscard]] bool unsupported(int value) noexcept {
  return value == ENOTTY || value == EOPNOTSUPP || value == ENOSYS;
}
[[nodiscard]] bool valid(DataBits v) noexcept {
  return v == DataBits::Five || v == DataBits::Six || v == DataBits::Seven ||
         v == DataBits::Eight;
}
[[nodiscard]] bool valid(Parity v) noexcept {
  return v == Parity::None || v == Parity::Even || v == Parity::Odd;
}
[[nodiscard]] bool valid(StopBits v) noexcept {
  return v == StopBits::One || v == StopBits::Two;
}
[[nodiscard]] bool valid(FlowControl v) noexcept {
  return v == FlowControl::None || v == FlowControl::Software ||
         v == FlowControl::Hardware;
}
[[nodiscard]] bool valid(RtsLevel v) noexcept {
  return v == RtsLevel::Asserted || v == RtsLevel::Deasserted;
}
[[nodiscard]] bool valid(FlushDirection v) noexcept {
  return v == FlushDirection::Input || v == FlushDirection::Output ||
         v == FlushDirection::Both;
}

[[nodiscard]] bool baud_speed(std::uint32_t baud, speed_t& speed) noexcept {
#define SERIAL_SPEED(n) \
  case n:               \
    speed = B##n;       \
    return true
  switch (baud) {
    SERIAL_SPEED(50); SERIAL_SPEED(75); SERIAL_SPEED(110);
#ifdef B134
    SERIAL_SPEED(134);
#endif
#ifdef B150
    SERIAL_SPEED(150);
#endif
#ifdef B200
    SERIAL_SPEED(200);
#endif
    SERIAL_SPEED(300);
    SERIAL_SPEED(600); SERIAL_SPEED(1200); SERIAL_SPEED(2400); SERIAL_SPEED(4800);
#ifdef B1800
    SERIAL_SPEED(1800);
#endif
    SERIAL_SPEED(9600); SERIAL_SPEED(19200); SERIAL_SPEED(38400); SERIAL_SPEED(57600);
    SERIAL_SPEED(115200); SERIAL_SPEED(230400);
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
    default: return false;
  }
#undef SERIAL_SPEED
}

[[nodiscard]] std::error_code check_config(const PortConfig& c, speed_t& speed) noexcept {
  const auto& rs485 = PortAccess::rs485(c);
  if (!valid(PortAccess::data_bits(c)) || !valid(PortAccess::parity(c)) ||
      !valid(PortAccess::stop_bits(c)) || !valid(PortAccess::flow_control(c)) ||
      !valid(PortAccess::rts_during_send(rs485)) ||
      !valid(PortAccess::rts_after_send(rs485)) ||
      PortAccess::delay_before_send(rs485).count() < 0 ||
      PortAccess::delay_after_send(rs485).count() < 0 ||
      PortAccess::delay_before_send(rs485).count() >
          std::numeric_limits<std::uint32_t>::max() ||
      PortAccess::delay_after_send(rs485).count() >
          std::numeric_limits<std::uint32_t>::max() ||
      !baud_speed(PortAccess::baud_rate(c), speed)) {
    return generic(std::errc::invalid_argument);
  }
#ifndef CRTSCTS
  if (PortAccess::flow_control(c) == FlowControl::Hardware) {
    return make_error_code(Error::UnsupportedConfiguration);
  }
#endif
  return {};
}

void raw(termios& t) noexcept {
  t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL |
                 INPCK | IGNPAR | IXON | IXOFF | IXANY);
  t.c_oflag &= ~OPOST;
  t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  t.c_cflag |= CREAD | CLOCAL;
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
}

[[nodiscard]] bool set_config(termios& t, const PortConfig& c, speed_t speed) noexcept {
  raw(t);
  t.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CRTSCTS
  t.c_cflag &= ~CRTSCTS;
#endif
  switch (PortAccess::data_bits(c)) {
    case DataBits::Five: t.c_cflag |= CS5; break;
    case DataBits::Six: t.c_cflag |= CS6; break;
    case DataBits::Seven: t.c_cflag |= CS7; break;
    case DataBits::Eight: t.c_cflag |= CS8; break;
    default: return false;
  }
  if (PortAccess::parity(c) != Parity::None) t.c_cflag |= PARENB;
  if (PortAccess::parity(c) == Parity::Odd) t.c_cflag |= PARODD;
  if (PortAccess::stop_bits(c) == StopBits::Two) t.c_cflag |= CSTOPB;
  if (PortAccess::flow_control(c) == FlowControl::Software) t.c_iflag |= IXON | IXOFF;
#ifdef CRTSCTS
  if (PortAccess::flow_control(c) == FlowControl::Hardware) t.c_cflag |= CRTSCTS;
#endif
  return cfsetispeed(&t, speed) == 0 && cfsetospeed(&t, speed) == 0;
}

[[nodiscard]] bool matches(const termios& t, const PortConfig& c, speed_t speed) noexcept {
  const tcflag_t raw_input =
      IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | INPCK | IGNPAR | IXANY;
  const tcflag_t software =
      PortAccess::flow_control(c) == FlowControl::Software ? IXON | IXOFF : 0;
  if ((t.c_iflag & (raw_input | IXON | IXOFF)) != software ||
      (t.c_oflag & OPOST) || (t.c_lflag & (ECHO | ECHONL | ICANON | ISIG | IEXTEN)) ||
      (t.c_cflag & (CREAD | CLOCAL)) != (CREAD | CLOCAL) || t.c_cc[VMIN] != 0 ||
      t.c_cc[VTIME] != 0 || cfgetispeed(&t) != speed || cfgetospeed(&t) != speed) return false;
  tcflag_t expected = PortAccess::data_bits(c) == DataBits::Five ? CS5 :
                      PortAccess::data_bits(c) == DataBits::Six ? CS6 :
                      PortAccess::data_bits(c) == DataBits::Seven ? CS7 : CS8;
  if (PortAccess::parity(c) != Parity::None) expected |= PARENB;
  if (PortAccess::parity(c) == Parity::Odd) expected |= PARODD;
  if (PortAccess::stop_bits(c) == StopBits::Two) expected |= CSTOPB;
  if ((t.c_cflag & (CSIZE | PARENB | PARODD | CSTOPB)) != expected) return false;
#ifdef CRTSCTS
  if (((t.c_cflag & CRTSCTS) != 0) !=
      (PortAccess::flow_control(c) == FlowControl::Hardware)) return false;
#endif
  return true;
}

[[nodiscard]] serial_rs485 requested(const Rs485Config& c) noexcept {
  serial_rs485 r{};
  r.flags = SER_RS485_ENABLED;
  if (PortAccess::rts_during_send(c) == RtsLevel::Asserted) r.flags |= SER_RS485_RTS_ON_SEND;
  if (PortAccess::rts_after_send(c) == RtsLevel::Asserted) r.flags |= SER_RS485_RTS_AFTER_SEND;
  r.delay_rts_before_send =
      static_cast<std::uint32_t>(PortAccess::delay_before_send(c).count());
  r.delay_rts_after_send =
      static_cast<std::uint32_t>(PortAccess::delay_after_send(c).count());
  return r;
}
[[nodiscard]] bool matches(const serial_rs485& actual, const Rs485Config& c) noexcept {
  const auto wanted = requested(c);
  const auto flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND;
  return (actual.flags & flags) == (wanted.flags & flags) &&
         actual.delay_rts_before_send == wanted.delay_rts_before_send &&
         actual.delay_rts_after_send == wanted.delay_rts_after_send;
}
void rollback(int fd, const termios& t, bool termios_changed, const serial_rs485& r,
              bool rs485_changed) noexcept {
  if (rs485_changed) static_cast<void>(tty_adapter::set_rs485(fd, r));
  if (termios_changed) static_cast<void>(tty_adapter::set_termios(fd, TCSANOW, t));
  static_cast<void>(tty_adapter::close_fd(fd));
}

struct Deadline { bool infinite; timespec absolute; };
[[nodiscard]] int compare(const timespec& a, const timespec& b) noexcept {
  return a.tv_sec == b.tv_sec ? (a.tv_nsec == b.tv_nsec ? 0 : a.tv_nsec < b.tv_nsec ? -1 : 1)
                              : a.tv_sec < b.tv_sec ? -1 : 1;
}
[[nodiscard]] std::error_code deadline_for(const Timeout& timeout, Deadline& out) noexcept {
  out.infinite = PortAccess::is_infinite(timeout);
  if (out.infinite) return {};
  timespec now{};
  const auto result = tty_adapter::monotonic_now(now);
  if (result.value < 0) return system(result.error);
  const auto seconds = PortAccess::duration(timeout).count() / 1000000000LL;
  const auto nanos = PortAccess::duration(timeout).count() % 1000000000LL;
  if (seconds > std::numeric_limits<time_t>::max() - now.tv_sec) {
    return generic(std::errc::value_too_large);
  }
  out.absolute = {now.tv_sec + static_cast<time_t>(seconds), now.tv_nsec + static_cast<long>(nanos)};
  if (out.absolute.tv_nsec >= 1000000000L) {
    if (out.absolute.tv_sec == std::numeric_limits<time_t>::max()) return generic(std::errc::value_too_large);
    ++out.absolute.tv_sec; out.absolute.tv_nsec -= 1000000000L;
  }
  return {};
}
[[nodiscard]] std::error_code wait_for(int fd, short events, const Deadline& d, short& revents) noexcept {
  for (;;) {
    timespec remaining{};
    const timespec* timeout = nullptr;
    if (!d.infinite) {
      timespec now{}; const auto current = tty_adapter::monotonic_now(now);
      if (current.value < 0) return system(current.error);
      if (compare(now, d.absolute) >= 0) return generic(std::errc::timed_out);
      remaining = {d.absolute.tv_sec - now.tv_sec, d.absolute.tv_nsec - now.tv_nsec};
      if (remaining.tv_nsec < 0) { --remaining.tv_sec; remaining.tv_nsec += 1000000000L; }
      timeout = &remaining;
    }
    const auto result = tty_adapter::wait(fd, events, timeout, revents);
    if (result.value >= 0) return result.value == 0 ? generic(std::errc::timed_out) : std::error_code{};
    if (result.error != EINTR) return system(result.error);
  }
}
[[nodiscard]] std::error_code unavailable(const Timeout& timeout) noexcept {
  return PortAccess::is_immediate(timeout)
             ? generic(std::errc::resource_unavailable_try_again)
             : generic(std::errc::timed_out);
}

}  // namespace

Port::Port() noexcept : fd_(kClosedFd) {}
Port::~Port() noexcept { static_cast<void>(close()); }
Port::Port(Port&& other) noexcept : fd_(other.fd_) { other.fd_ = kClosedFd; }
bool Port::is_open() const noexcept { return fd_ >= 0; }

std::error_code Port::open(const std::string& path, const PortConfig& config) noexcept {
  if (is_open()) return make_error_code(Error::PortAlreadyOpen);
  if (path.empty() || path.find('\0') != std::string::npos) return generic(std::errc::invalid_argument);
  speed_t speed{}; if (const auto error = check_config(config, speed)) return error;
  const auto opened = tty_adapter::open_path(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (opened.value < 0) return system(opened.error);
  const int fd = static_cast<int>(opened.value);
  const auto tty = tty_adapter::is_tty(fd);
  if (tty.value <= 0) {
    static_cast<void>(tty_adapter::close_fd(fd));
    return tty.value == 0
               ? generic(std::errc::inappropriate_io_control_operation)
               : system(tty.error);
  }
  termios original{}; const auto got = tty_adapter::get_termios(fd, original);
  if (got.value < 0) { static_cast<void>(tty_adapter::close_fd(fd)); return system(got.error); }
  serial_rs485 original_rs{};
  if (PortAccess::enabled(PortAccess::rs485(config))) {
    const auto rs = tty_adapter::get_rs485(fd, original_rs);
    if (rs.value < 0) { static_cast<void>(tty_adapter::close_fd(fd)); return unsupported(rs.error) ? make_error_code(Error::UnsupportedConfiguration) : system(rs.error); }
  }
  termios candidate = original;
  if (!set_config(candidate, config, speed)) { static_cast<void>(tty_adapter::close_fd(fd)); return generic(std::errc::invalid_argument); }
  const auto set = tty_adapter::set_termios(fd, TCSANOW, candidate);
  if (set.value < 0) {
    rollback(fd, original, true, original_rs, false);
    return system(set.error);
  }
  bool rs_changed = false;
  if (PortAccess::enabled(PortAccess::rs485(config))) {
    const auto rs = tty_adapter::set_rs485(fd, requested(PortAccess::rs485(config)));
    if (rs.value < 0) { rollback(fd, original, true, original_rs, false); return unsupported(rs.error) ? make_error_code(Error::UnsupportedConfiguration) : system(rs.error); }
    rs_changed = true;
  }
  termios actual{}; const auto readback = tty_adapter::get_termios(fd, actual);
  if (readback.value < 0) { rollback(fd, original, true, original_rs, rs_changed); return system(readback.error); }
  if (!matches(actual, config, speed)) { rollback(fd, original, true, original_rs, rs_changed); return make_error_code(Error::ConfigurationMismatch); }
  if (PortAccess::enabled(PortAccess::rs485(config))) {
    serial_rs485 actual_rs{}; const auto rs = tty_adapter::get_rs485(fd, actual_rs);
    if (rs.value < 0) { rollback(fd, original, true, original_rs, rs_changed); return unsupported(rs.error) ? make_error_code(Error::UnsupportedConfiguration) : system(rs.error); }
    if (!matches(actual_rs, PortAccess::rs485(config))) {
      rollback(fd, original, true, original_rs, rs_changed);
      return make_error_code(Error::ConfigurationMismatch);
    }
  }
  fd_ = fd; return {};
}

std::error_code Port::close() noexcept {
  const int fd = fd_; if (fd < 0) return {}; fd_ = kClosedFd;
  const auto result = tty_adapter::close_fd(fd);
  return result.value < 0 ? system(result.error) : std::error_code{};
}

TransferResult Port::read(std::byte* data, std::size_t capacity, Timeout timeout) noexcept {
  if (!is_open()) return {0, make_error_code(Error::PortNotOpen)};
  if (data == nullptr && capacity > 0) return {0, generic(std::errc::invalid_argument)};
  if (!timeout.is_valid()) return {0, generic(std::errc::invalid_argument)};
  if (capacity == 0) return {0, {}};
  Deadline deadline{};
  if (!PortAccess::is_immediate(timeout)) {
    if (const auto error = deadline_for(timeout, deadline)) return {0, error};
  }
  for (;;) {
    short revents = 0;
    if (!PortAccess::is_immediate(timeout)) {
      if (const auto error = wait_for(fd_, POLLIN, deadline, revents)) return {0, error};
      if (revents & POLLNVAL) return {0, system(EBADF)};
      if (!(revents & POLLIN) && (revents & POLLHUP)) return {0, make_error_code(Error::DeviceDisconnected)};
    }
    const auto result = tty_adapter::read_bytes(fd_, data, capacity);
    if (result.value > 0) return {static_cast<std::size_t>(result.value), {}};
    if (result.value == 0) return {0, make_error_code(Error::DeviceDisconnected)};
    if (result.error == EINTR && !PortAccess::is_immediate(timeout)) continue;
    if (result.error == EAGAIN || result.error == EWOULDBLOCK) {
      if (PortAccess::is_immediate(timeout)) return {0, unavailable(timeout)};
      continue;
    }
    return {0, system(result.error)};
  }
}

TransferResult Port::write(const std::byte* data, std::size_t size, Timeout timeout) noexcept {
  if (!is_open()) return {0, make_error_code(Error::PortNotOpen)};
  if (data == nullptr && size > 0) return {0, generic(std::errc::invalid_argument)};
  if (!timeout.is_valid()) return {0, generic(std::errc::invalid_argument)};
  if (size == 0) return {0, {}};
  Deadline deadline{};
  if (!PortAccess::is_immediate(timeout)) {
    if (const auto error = deadline_for(timeout, deadline)) return {0, error};
  }
  std::size_t done = 0;
  for (;;) {
    short revents = 0;
    if (!PortAccess::is_immediate(timeout)) {
      if (const auto error = wait_for(fd_, POLLOUT, deadline, revents)) return {done, error};
      if (revents & POLLNVAL) return {done, system(EBADF)};
      if (!(revents & POLLOUT) && (revents & POLLHUP)) return {done, make_error_code(Error::DeviceDisconnected)};
    }
    const auto result = tty_adapter::write_bytes(fd_, data + done, size - done);
    if (result.value > 0) {
      done += static_cast<std::size_t>(result.value);
      if (done == size) return {done, {}};
      if (PortAccess::is_immediate(timeout)) {
        return {done, unavailable(timeout)};
      }
      continue;
    }
    if (result.value == 0) return {done, make_error_code(Error::DeviceDisconnected)};
    if (result.error == EINTR && !PortAccess::is_immediate(timeout)) continue;
    if (result.error == EAGAIN || result.error == EWOULDBLOCK) {
      if (PortAccess::is_immediate(timeout)) {
        return {done, unavailable(timeout)};
      }
      continue;
    }
    return {done, system(result.error)};
  }
}

std::error_code Port::flush(FlushDirection direction) noexcept {
  if (!is_open()) return make_error_code(Error::PortNotOpen);
  if (!valid(direction)) return generic(std::errc::invalid_argument);
  const int selector = direction == FlushDirection::Input ? TCIFLUSH :
                       direction == FlushDirection::Output ? TCOFLUSH : TCIOFLUSH;
  const auto result = tty_adapter::flush(fd_, selector);
  return result.value < 0 ? system(result.error) : std::error_code{};
}

std::error_code Port::drain(Timeout timeout) noexcept {
  if (!is_open()) return make_error_code(Error::PortNotOpen);
  if (!timeout.is_valid()) return generic(std::errc::invalid_argument);
  if (PortAccess::is_finite(timeout)) {
    return generic(std::errc::operation_not_supported);
  }
  if (PortAccess::is_immediate(timeout)) {
    int queued = 0; const auto result = tty_adapter::output_queue_size(fd_, queued);
    if (result.value < 0) return unsupported(result.error) ? generic(std::errc::operation_not_supported) : system(result.error);
    return queued == 0 ? std::error_code{} : generic(std::errc::resource_unavailable_try_again);
  }
  for (;;) { const auto result = tty_adapter::drain(fd_); if (result.value >= 0) return {}; if (result.error != EINTR) return system(result.error); }
}

}  // namespace serial
