#pragma once

#include <cstddef>
#include <ctime>
#include <termios.h>

#include <linux/serial.h>

namespace serial::tty_adapter {

struct Result final {
  long value;
  int error;
};

[[nodiscard]] Result open_path(const char* path, int flags) noexcept;
[[nodiscard]] Result close_fd(int fd) noexcept;
[[nodiscard]] Result is_tty(int fd) noexcept;
[[nodiscard]] Result get_termios(int fd, termios& attributes) noexcept;
[[nodiscard]] Result set_termios(int fd, int action, const termios& attributes) noexcept;
[[nodiscard]] Result get_rs485(int fd, serial_rs485& configuration) noexcept;
[[nodiscard]] Result set_rs485(int fd, const serial_rs485& configuration) noexcept;
[[nodiscard]] Result monotonic_now(timespec& value) noexcept;
[[nodiscard]] Result wait(int fd, short events, const timespec* timeout, short& revents) noexcept;
[[nodiscard]] Result read_bytes(int fd, void* data, std::size_t size) noexcept;
[[nodiscard]] Result write_bytes(int fd, const void* data, std::size_t size) noexcept;
[[nodiscard]] Result flush(int fd, int selector) noexcept;
[[nodiscard]] Result output_queue_size(int fd, int& size) noexcept;
[[nodiscard]] Result drain(int fd) noexcept;

}  // namespace serial::tty_adapter
