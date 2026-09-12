#include <serial/port.hpp>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace {

serial::PortConfig configuration() {
  return {115200, serial::DataBits::Eight, serial::Parity::None,
          serial::StopBits::One, serial::FlowControl::None,
          serial::Rs485Config::disabled()};
}

}  // namespace

int main() {
  const int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  assert(master >= 0);
  assert(grantpt(master) == 0);
  assert(unlockpt(master) == 0);
  char* const slave_path = ptsname(master);
  assert(slave_path != nullptr);

  serial::Port port;
  assert(port.open(slave_path, configuration()) == std::error_code{});

  std::byte received[8]{};
  const char input[] = "abc";
  assert(::write(master, input, sizeof(input) - 1) == 3);
  const auto read = port.read(received, sizeof(received), serial::Timeout::after(std::chrono::seconds(1)));
  assert(read.error == std::error_code{});
  assert(read.bytes_transferred == 3);
  assert(std::memcmp(received, input, 3) == 0);

  const std::byte output[] = {std::byte{'x'}, std::byte{'y'}};
  const auto written = port.write(output, sizeof(output), serial::Timeout::after(std::chrono::seconds(1)));
  assert(written.error == std::error_code{});
  assert(written.bytes_transferred == sizeof(output));
  char observed[2]{};
  assert(::read(master, observed, sizeof(observed)) == 2);
  assert(observed[0] == 'x' && observed[1] == 'y');

  termios attributes{};
  const int inspection_fd = ::open(slave_path, O_RDWR | O_NOCTTY | O_CLOEXEC);
  assert(inspection_fd >= 0);
  assert(tcgetattr(inspection_fd, &attributes) == 0);
  assert((attributes.c_lflag & (ICANON | ECHO | ISIG)) == 0);
  assert(attributes.c_cc[VMIN] == 0 && attributes.c_cc[VTIME] == 0);
  assert(::close(inspection_fd) == 0);

  const auto no_input =
      port.read(received, sizeof(received), serial::Timeout::after(std::chrono::milliseconds(10)));
  assert(no_input.error == std::make_error_code(std::errc::timed_out));

  assert(::write(master, input, sizeof(input) - 1) == 3);
  assert(port.flush(serial::FlushDirection::Input) == std::error_code{});
  assert(port.close() == std::error_code{});
  assert(::close(master) == 0);
}
