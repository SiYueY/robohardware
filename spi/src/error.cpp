#include <spi/error.hpp>

#include <string>

namespace spi {
namespace {

class ErrorCategory final : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "spi"; }

  [[nodiscard]] std::string message(int value) const override {
    switch (static_cast<Error>(value)) {
      case Error::DeviceAlreadyOpen:
        return "device already open";
      case Error::DeviceNotOpen:
        return "device not open";
      case Error::ConfigurationMismatch:
        return "configuration mismatch";
      default:
        return "unknown spi error";
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

}  // namespace spi
