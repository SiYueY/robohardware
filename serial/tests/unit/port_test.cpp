#include <serial/port.hpp>

#include "controlled_tty_adapter.hpp"

#include <cassert>
#include <cerrno>
#include <poll.h>

namespace {

serial::PortConfig configuration() {
  return {115200, serial::DataBits::Eight, serial::Parity::None,
          serial::StopBits::One, serial::FlowControl::None,
          serial::Rs485Config::disabled()};
}

void assert_error(const std::error_code& actual, const std::error_code& expected) {
  assert(actual == expected);
}

}  // namespace

int main() {
  using namespace serial;
  test::reset_adapter();
  std::byte bytes[4]{};

  Port port;
  assert(!port.is_open());
  assert_error(port.read(nullptr, 0, Timeout::immediate()).error,
               make_error_code(Error::PortNotOpen));
  assert_error(port.open("/dev/controlled", configuration()), {});
  assert(port.is_open());
  assert_error(port.open("/dev/controlled", configuration()),
               make_error_code(Error::PortAlreadyOpen));
  assert(port.read(nullptr, 0, Timeout::immediate()).error == std::error_code{});
  assert_error(port.read(nullptr, 1, Timeout::immediate()).error,
               std::make_error_code(std::errc::invalid_argument));
  assert_error(port.read(bytes, sizeof(bytes), Timeout::after(std::chrono::nanoseconds(-1))).error,
               std::make_error_code(std::errc::invalid_argument));
  assert_error(port.drain(Timeout::after(std::chrono::milliseconds(1))),
               std::make_error_code(std::errc::operation_not_supported));

  test::set_output_queue(1);
  assert_error(port.drain(Timeout::immediate()),
               std::make_error_code(std::errc::resource_unavailable_try_again));
  test::set_output_queue(0, ENOTTY);
  assert_error(port.drain(Timeout::immediate()),
               std::make_error_code(std::errc::operation_not_supported));

  test::set_read_result(-1, EAGAIN);
  assert_error(port.read(bytes, sizeof(bytes), Timeout::immediate()).error,
               std::make_error_code(std::errc::resource_unavailable_try_again));
  test::set_write_result(2);
  const auto partial = port.write(bytes, sizeof(bytes), Timeout::immediate());
  assert(partial.bytes_transferred == 2);
  assert_error(partial.error,
               std::make_error_code(std::errc::resource_unavailable_try_again));
  test::set_wait_result(1, POLLHUP);
  assert_error(port.read(bytes, sizeof(bytes), Timeout::after(std::chrono::milliseconds(1))).error,
               make_error_code(Error::DeviceDisconnected));

  Port moved(std::move(port));
  assert(!port.is_open());
  assert(moved.is_open());
  test::set_close_result(-1, EIO);
  assert_error(moved.close(), std::error_code(EIO, std::system_category()));
  assert(!moved.is_open());
  assert_error(moved.close(), {});
  assert(test::close_count() == 1);

  test::reset_adapter();
  test::set_is_tty_result(0);
  Port not_tty;
  assert_error(not_tty.open("/tmp/file", configuration()),
               std::make_error_code(std::errc::inappropriate_io_control_operation));
  assert(!not_tty.is_open());
  assert(test::close_count() == 1);

  test::reset_adapter();
  test::set_rs485_get_result(-1, ENOTTY);
  Port rs485;
  const PortConfig requested_rs485{
      115200, DataBits::Eight, Parity::None, StopBits::One, FlowControl::None,
      Rs485Config::enabled(RtsLevel::Asserted, RtsLevel::Deasserted, {}, {})};
  assert_error(rs485.open("/dev/controlled", requested_rs485),
               make_error_code(Error::UnsupportedConfiguration));
  assert(!rs485.is_open());

  test::reset_adapter();
  test::force_termios_mismatch();
  Port mismatch;
  assert_error(mismatch.open("/dev/controlled", configuration()),
               make_error_code(Error::ConfigurationMismatch));
  assert(!mismatch.is_open());
  // Candidate application and best-effort restoration were both attempted.
  assert(test::set_termios_count() == 2);
  assert(test::close_count() == 1);

  test::reset_adapter();
  test::set_set_termios_result(-1, EIO);
  Port set_failure;
  assert_error(set_failure.open("/dev/controlled", configuration()),
               std::error_code(EIO, std::system_category()));
  assert(!set_failure.is_open());
  assert(test::set_termios_count() == 2);
  assert(test::close_count() == 1);

  test::reset_adapter();
  Port timeout;
  assert_error(timeout.open("/dev/controlled", configuration()), {});
  test::set_wait_result(0, 0);
  assert_error(timeout.read(bytes, sizeof(bytes), Timeout::after(std::chrono::milliseconds(1))).error,
               std::make_error_code(std::errc::timed_out));
  test::set_flush_result(-1, EINTR);
  assert_error(timeout.flush(FlushDirection::Input),
               std::error_code(EINTR, std::system_category()));
  assert_error(timeout.flush(static_cast<FlushDirection>(99)),
               std::make_error_code(std::errc::invalid_argument));
  assert_error(timeout.close(), {});

  test::reset_adapter();
  test::ignore_rs485_set();
  Port rs485_mismatch;
  assert_error(rs485_mismatch.open("/dev/controlled", requested_rs485),
               make_error_code(Error::ConfigurationMismatch));
  assert(!rs485_mismatch.is_open());
}
