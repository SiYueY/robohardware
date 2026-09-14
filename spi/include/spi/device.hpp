#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

#include <spi/error.hpp>
#include <spi/options.hpp>

namespace spi {

class Device final {
 public:
  Device() noexcept;
  ~Device() noexcept;
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&& other) noexcept;
  Device& operator=(Device&& other) = delete;

  [[nodiscard]] std::error_code open(
      const std::string& path,
      const Options& options) noexcept;
  [[nodiscard]] std::error_code close() noexcept;
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] std::error_code transfer(
      const std::byte* tx,
      std::byte* rx,
      std::size_t size) noexcept;

 private:
  int fd_;
};

}  // namespace spi
