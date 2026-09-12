#include "tty_adapter.hpp"

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace serial::tty_adapter {
namespace {
[[nodiscard]] Result captured(long value) noexcept {
  return {value, value < 0 ? errno : 0};
}
}  // namespace

Result open_path(const char* path, int flags) noexcept { return captured(::open(path, flags)); }
Result close_fd(int fd) noexcept { return captured(::close(fd)); }
Result is_tty(int fd) noexcept { return captured(::isatty(fd)); }
Result get_termios(int fd, termios& attributes) noexcept { return captured(::tcgetattr(fd, &attributes)); }
Result set_termios(int fd, int action, const termios& attributes) noexcept {
  return captured(::tcsetattr(fd, action, &attributes));
}
Result get_rs485(int fd, serial_rs485& configuration) noexcept {
  return captured(::ioctl(fd, TIOCGRS485, &configuration));
}
Result set_rs485(int fd, const serial_rs485& configuration) noexcept {
  auto mutable_configuration = configuration;
  return captured(::ioctl(fd, TIOCSRS485, &mutable_configuration));
}
Result monotonic_now(timespec& value) noexcept { return captured(::clock_gettime(CLOCK_MONOTONIC, &value)); }
Result wait(int fd, short events, const timespec* timeout, short& revents) noexcept {
  pollfd descriptor{};
  descriptor.fd = fd;
  descriptor.events = events;
  const auto result = captured(::ppoll(&descriptor, 1, timeout, nullptr));
  revents = descriptor.revents;
  return result;
}
Result read_bytes(int fd, void* data, std::size_t size) noexcept { return captured(::read(fd, data, size)); }
Result write_bytes(int fd, const void* data, std::size_t size) noexcept { return captured(::write(fd, data, size)); }
Result flush(int fd, int selector) noexcept { return captured(::tcflush(fd, selector)); }
Result output_queue_size(int fd, int& size) noexcept { return captured(::ioctl(fd, TIOCOUTQ, &size)); }
Result drain(int fd) noexcept { return captured(::tcdrain(fd)); }

}  // namespace serial::tty_adapter
