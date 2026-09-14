#pragma once

#include <cstddef>
#include <cstdint>

#include <linux/spi/spidev.h>

namespace spi::spidev_adapter {

struct Result final {
  long value;
  int error;
};

[[nodiscard]] Result open_path(const char* path, int flags) noexcept;
[[nodiscard]] Result close_fd(int fd) noexcept;
[[nodiscard]] Result read_mode32(int fd, std::uint32_t& value) noexcept;
[[nodiscard]] Result write_mode32(int fd, std::uint32_t value) noexcept;
[[nodiscard]] Result read_bits_per_word(int fd, std::uint8_t& value) noexcept;
[[nodiscard]] Result write_bits_per_word(int fd, std::uint8_t value) noexcept;
[[nodiscard]] Result read_max_speed_hz(int fd, std::uint32_t& value) noexcept;
[[nodiscard]] Result write_max_speed_hz(int fd, std::uint32_t value) noexcept;
[[nodiscard]] Result transfer(int fd, spi_ioc_transfer& value) noexcept;

}  // namespace spi::spidev_adapter
