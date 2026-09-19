#pragma once

#include <cstddef>
#include <ctime>

#include <sys/types.h>
#include <termios.h>

#include <linux/serial.h>

namespace serial::tty {

[[nodiscard]] int open(const char* path) noexcept;
[[nodiscard]] int close(int fd) noexcept;
[[nodiscard]] int is_terminal(int fd) noexcept;
[[nodiscard]] int set_exclusive(int fd) noexcept;
[[nodiscard]] int clear_exclusive(int fd) noexcept;

[[nodiscard]] int read_attributes(int fd, termios& attributes) noexcept;
[[nodiscard]] int write_attributes(int fd, int action, const termios& attributes) noexcept;

[[nodiscard]] int read_rs485(int fd, serial_rs485& configuration) noexcept;
[[nodiscard]] int write_rs485(int fd, const serial_rs485& configuration) noexcept;

[[nodiscard]] int monotonic_now(timespec& value) noexcept;
[[nodiscard]] int wait(int fd, short events, const timespec* timeout, short& revents) noexcept;

[[nodiscard]] ssize_t read(int fd, void* data, std::size_t size) noexcept;
[[nodiscard]] ssize_t write(int fd, const void* data, std::size_t size) noexcept;

[[nodiscard]] int input_queue_size(int fd, int& size) noexcept;
[[nodiscard]] int output_queue_size(int fd, int& size) noexcept;
[[nodiscard]] int discard(int fd, int selector) noexcept;
[[nodiscard]] int drain(int fd) noexcept;

[[nodiscard]] int read_modem_lines(int fd, int& lines) noexcept;
[[nodiscard]] int write_modem_line(int fd, int bits, bool asserted) noexcept;
[[nodiscard]] int write_break(int fd, bool asserted) noexcept;

}  // namespace serial::tty
