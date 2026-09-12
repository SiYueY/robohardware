#include "controlled_tty_adapter.hpp"

#include "tty_adapter.hpp"

#include <cstring>
#include <poll.h>
#include <termios.h>

namespace {

struct State final {
  serial::tty_adapter::Result opened{11, 0};
  serial::tty_adapter::Result tty{1, 0};
  serial::tty_adapter::Result close{0, 0};
  serial::tty_adapter::Result get_termios{0, 0};
  serial::tty_adapter::Result set_termios{0, 0};
  serial::tty_adapter::Result read{-1, EAGAIN};
  serial::tty_adapter::Result write{-1, EAGAIN};
  serial::tty_adapter::Result wait{1, 0};
  short revents{POLLIN | POLLOUT};
  serial::tty_adapter::Result output_queue{0, 0};
  serial::tty_adapter::Result get_rs485{0, 0};
  serial::tty_adapter::Result set_rs485{0, 0};
  serial::tty_adapter::Result flush{0, 0};
  bool termios_mismatch{false};
  bool ignore_rs485{false};
  int output_size{0};
  int closes{0};
  int set_termios_calls{0};
  termios attributes{};
  serial_rs485 rs485{};
} state;

}  // namespace

namespace serial::test {

void reset_adapter() noexcept {
  state = State{};
  state.attributes.c_cflag = CS8;
  static_cast<void>(cfsetispeed(&state.attributes, B9600));
  static_cast<void>(cfsetospeed(&state.attributes, B9600));
}
void set_open_result(long value, int error) noexcept { state.opened = {value, error}; }
void set_is_tty_result(long value, int error) noexcept { state.tty = {value, error}; }
void set_close_result(long value, int error) noexcept { state.close = {value, error}; }
void set_get_termios_result(long value, int error) noexcept { state.get_termios = {value, error}; }
void set_set_termios_result(long value, int error) noexcept { state.set_termios = {value, error}; }
void force_termios_mismatch() noexcept { state.termios_mismatch = true; }
void set_read_result(long value, int error) noexcept { state.read = {value, error}; }
void set_write_result(long value, int error) noexcept { state.write = {value, error}; }
void set_wait_result(long value, short revents, int error) noexcept {
  state.wait = {value, error};
  state.revents = revents;
}
void set_output_queue(int value, int error) noexcept {
  state.output_size = value;
  state.output_queue = {error == 0 ? 0 : -1, error};
}
void set_rs485_get_result(long value, int error) noexcept { state.get_rs485 = {value, error}; }
void set_rs485_set_result(long value, int error) noexcept { state.set_rs485 = {value, error}; }
void ignore_rs485_set() noexcept { state.ignore_rs485 = true; }
void set_flush_result(long value, int error) noexcept { state.flush = {value, error}; }
int close_count() noexcept { return state.closes; }
int set_termios_count() noexcept { return state.set_termios_calls; }

}  // namespace serial::test

namespace serial::tty_adapter {

Result open_path(const char*, int) noexcept { return state.opened; }
Result close_fd(int) noexcept { ++state.closes; return state.close; }
Result is_tty(int) noexcept { return state.tty; }
Result get_termios(int, termios& attributes) noexcept {
  attributes = state.attributes;
  if (state.termios_mismatch) attributes.c_cc[VMIN] = 1;
  return state.get_termios;
}
Result set_termios(int, int, const termios& attributes) noexcept {
  ++state.set_termios_calls;
  state.attributes = attributes;
  return state.set_termios;
}
Result get_rs485(int, serial_rs485& configuration) noexcept {
  configuration = state.rs485;
  return state.get_rs485;
}
Result set_rs485(int, const serial_rs485& configuration) noexcept {
  if (!state.ignore_rs485) state.rs485 = configuration;
  return state.set_rs485;
}
Result monotonic_now(timespec& value) noexcept { value = {1, 0}; return {0, 0}; }
Result wait(int, short, const timespec*, short& revents) noexcept {
  revents = state.revents;
  return state.wait;
}
Result read_bytes(int, void*, std::size_t) noexcept { return state.read; }
Result write_bytes(int, const void*, std::size_t) noexcept { return state.write; }
Result flush(int, int) noexcept { return state.flush; }
Result output_queue_size(int, int& value) noexcept {
  value = state.output_size;
  return state.output_queue;
}
Result drain(int) noexcept { return {0, 0}; }

}  // namespace serial::tty_adapter
