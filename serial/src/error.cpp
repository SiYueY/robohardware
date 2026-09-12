#include <serial/error.hpp>

#include <string>

namespace serial {
namespace {

class ErrorCategory final : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "serial"; }
  [[nodiscard]] std::string message(int value) const override {
    switch (static_cast<Error>(value)) {
      case Error::PortAlreadyOpen:
        return "port already open";
      case Error::PortNotOpen:
        return "port not open";
      case Error::DeviceDisconnected:
        return "device disconnected";
      case Error::UnsupportedConfiguration:
        return "unsupported configuration";
      case Error::ConfigurationMismatch:
        return "configuration mismatch";
      default:
        return "unknown serial error";
    }
  }
};

[[nodiscard]] const std::error_category& error_category() noexcept {
  static const ErrorCategory category;
  return category;
}

}  // namespace

std::error_code make_error_code(Error error) noexcept {
  return {static_cast<int>(error), error_category()};
}

}  // namespace serial
