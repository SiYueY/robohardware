#pragma once

#include <cstddef>
#include <string>
#include <system_error>

#include <serial/configuration.hpp>
#include <serial/error.hpp>
#include <serial/timeout.hpp>
#include <serial/transfer.hpp>

namespace serial {

enum class FlushDirection { Input, Output, Both };

class Port final {
 public:
  Port() noexcept;
  ~Port() noexcept;
  Port(const Port&) = delete;
  Port& operator=(const Port&) = delete;
  Port(Port&& other) noexcept;
  Port& operator=(Port&& other) = delete;

  [[nodiscard]] std::error_code open(
      const std::string& path,
      const PortConfig& config) noexcept;
  [[nodiscard]] std::error_code close() noexcept;
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] TransferResult read(
      std::byte* data,
      std::size_t capacity,
      Timeout timeout) noexcept;
  [[nodiscard]] TransferResult write(
      const std::byte* data,
      std::size_t size,
      Timeout timeout) noexcept;
  [[nodiscard]] std::error_code flush(FlushDirection direction) noexcept;
  [[nodiscard]] std::error_code drain(Timeout timeout) noexcept;

 private:
  int fd_;
};

}  // namespace serial
