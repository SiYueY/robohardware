#pragma once

#include <cstddef>
#include <string>
#include <system_error>

#include <spi/config.hpp>

namespace spi {

class Device final {
 public:
  Device() noexcept = default;
  ~Device() noexcept;
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&& other) noexcept;
  Device& operator=(Device&& other) = delete;

  [[nodiscard]] std::error_code open(
      const std::string& path,
      const Config& config) noexcept;
  [[nodiscard]] std::error_code close() noexcept;
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] std::error_code transfer(
      const std::byte* tx,
      std::byte* rx,
      std::size_t size) noexcept;

 private:
  int fd_{-1};
};

}  // namespace spi
