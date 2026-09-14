#pragma once

#include <system_error>

namespace spi {

enum class Error {
  DeviceAlreadyOpen = 1,
  DeviceNotOpen,
  ConfigurationMismatch,
};

[[nodiscard]] std::error_code make_error_code(Error error) noexcept;

}  // namespace spi

namespace std {

template <>
struct is_error_code_enum<spi::Error> : true_type {};

}  // namespace std
