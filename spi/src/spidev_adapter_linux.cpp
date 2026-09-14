#include "spidev_adapter.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace spi::spidev_adapter {
namespace {

[[nodiscard]] Result ioctl_result(int fd, unsigned long request, void* value) noexcept {
  const auto result = ::ioctl(fd, request, value);
  return {result, result < 0 ? errno : 0};
}

}  // namespace

Result open_path(const char* path, int flags) noexcept {
  const auto result = ::open(path, flags);
  return {result, result < 0 ? errno : 0};
}

Result close_fd(int fd) noexcept {
  const auto result = ::close(fd);
  return {result, result < 0 ? errno : 0};
}

Result read_mode32(int fd, std::uint32_t& value) noexcept {
  return ioctl_result(fd, SPI_IOC_RD_MODE32, &value);
}

Result write_mode32(int fd, std::uint32_t value) noexcept {
  return ioctl_result(fd, SPI_IOC_WR_MODE32, &value);
}

Result read_bits_per_word(int fd, std::uint8_t& value) noexcept {
  return ioctl_result(fd, SPI_IOC_RD_BITS_PER_WORD, &value);
}

Result write_bits_per_word(int fd, std::uint8_t value) noexcept {
  return ioctl_result(fd, SPI_IOC_WR_BITS_PER_WORD, &value);
}

Result read_max_speed_hz(int fd, std::uint32_t& value) noexcept {
  return ioctl_result(fd, SPI_IOC_RD_MAX_SPEED_HZ, &value);
}

Result write_max_speed_hz(int fd, std::uint32_t value) noexcept {
  return ioctl_result(fd, SPI_IOC_WR_MAX_SPEED_HZ, &value);
}

Result transfer(int fd, spi_ioc_transfer& value) noexcept {
  return ioctl_result(fd, SPI_IOC_MESSAGE(1), &value);
}

}  // namespace spi::spidev_adapter
