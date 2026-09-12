#pragma once

#include <cerrno>
#include <cstddef>

namespace serial::test {

void reset_adapter() noexcept;
void set_open_result(long value, int error = 0) noexcept;
void set_is_tty_result(long value, int error = 0) noexcept;
void set_close_result(long value, int error = 0) noexcept;
void set_get_termios_result(long value, int error = 0) noexcept;
void set_set_termios_result(long value, int error = 0) noexcept;
void force_termios_mismatch() noexcept;
void set_read_result(long value, int error = 0) noexcept;
void set_write_result(long value, int error = 0) noexcept;
void set_wait_result(long value, short revents, int error = 0) noexcept;
void set_output_queue(int value, int error = 0) noexcept;
void set_rs485_get_result(long value, int error = 0) noexcept;
void set_rs485_set_result(long value, int error = 0) noexcept;
void ignore_rs485_set() noexcept;
void set_flush_result(long value, int error = 0) noexcept;
int close_count() noexcept;
int set_termios_count() noexcept;

}  // namespace serial::test
