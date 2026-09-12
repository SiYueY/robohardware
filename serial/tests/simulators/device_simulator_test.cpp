#include <serial/port.hpp>

#include <cassert>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
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

  const pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    static_cast<void>(::write(master, "abc", 3));
    usleep(20000);
    static_cast<void>(::write(master, "de", 2));
    usleep(100000);
    static_cast<void>(::close(master));
    _exit(0);
  }

  assert(::close(master) == 0);
  serial::Port port;
  assert(port.open(slave_path, configuration()) == std::error_code{});
  std::byte data[8]{};
  const auto first = port.read(data, sizeof(data), serial::Timeout::after(std::chrono::seconds(1)));
  assert(first.error == std::error_code{} && first.bytes_transferred == 3);
  assert(std::memcmp(data, "abc", 3) == 0);
  const auto second = port.read(data, sizeof(data), serial::Timeout::after(std::chrono::seconds(1)));
  assert(second.error == std::error_code{} && second.bytes_transferred == 2);
  assert(std::memcmp(data, "de", 2) == 0);
  const auto disconnected = port.read(data, sizeof(data), serial::Timeout::after(std::chrono::seconds(1)));
  assert(disconnected.error == serial::make_error_code(serial::Error::DeviceDisconnected));
  assert(port.close() == std::error_code{});
  int status = 0;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
