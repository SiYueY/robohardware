#pragma once

#include <system_error>

namespace serial {

enum class Error {
  PortAlreadyOpen,
  PortNotOpen,
  DeviceDisconnected,
  UnsupportedConfiguration,
  ConfigurationMismatch,
};

[[nodiscard]] std::error_code make_error_code(Error error) noexcept;

}  // namespace serial

namespace std {

template <>
struct is_error_code_enum<serial::Error> : true_type {};

}  // namespace std
