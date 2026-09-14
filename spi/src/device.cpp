#include <spi/device.hpp>

#include "spidev_adapter.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <linux/spi/spi.h>
#include <limits>

namespace spi {
namespace {

struct NativeConfig final {
  std::uint32_t mode;
  std::uint8_t bits_per_word;
  std::uint32_t max_speed_hz;
};

enum class ConfigStage { None, Mode, BitsPerWord, MaxSpeed };

[[nodiscard]] std::error_code generic(std::errc value) noexcept {
  return std::make_error_code(value);
}

[[nodiscard]] std::error_code system(int value) noexcept {
  return {value, std::system_category()};
}

[[nodiscard]] bool valid(Mode value) noexcept {
  return value == Mode::Mode0 || value == Mode::Mode1 || value == Mode::Mode2 ||
         value == Mode::Mode3;
}

[[nodiscard]] bool valid(BitOrder value) noexcept {
  return value == BitOrder::MsbFirst || value == BitOrder::LsbFirst;
}

[[nodiscard]] std::uint32_t requested_mode(
    Mode mode, BitOrder bit_order, std::uint32_t original) noexcept {
  auto candidate = original & ~(SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST);
  switch (mode) {
    case Mode::Mode0:
      break;
    case Mode::Mode1:
      candidate |= SPI_CPHA;
      break;
    case Mode::Mode2:
      candidate |= SPI_CPOL;
      break;
    case Mode::Mode3:
      candidate |= SPI_CPOL | SPI_CPHA;
      break;
  }
  if (bit_order == BitOrder::LsbFirst) candidate |= SPI_LSB_FIRST;
  return candidate;
}

[[nodiscard]] bool mode_matches(std::uint32_t actual, std::uint32_t requested) noexcept {
  constexpr std::uint32_t kOwnedModeBits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;
  return (actual & kOwnedModeBits) == (requested & kOwnedModeBits);
}

void restore(int fd, const NativeConfig& original, ConfigStage stage) noexcept {
  if (stage == ConfigStage::MaxSpeed) {
    static_cast<void>(spidev_adapter::write_max_speed_hz(fd, original.max_speed_hz));
  }
  if (stage == ConfigStage::MaxSpeed || stage == ConfigStage::BitsPerWord) {
    static_cast<void>(spidev_adapter::write_bits_per_word(fd, original.bits_per_word));
  }
  if (stage != ConfigStage::None) {
    static_cast<void>(spidev_adapter::write_mode32(fd, original.mode));
  }
  static_cast<void>(spidev_adapter::close_fd(fd));
}

}  // namespace

Device::~Device() noexcept { static_cast<void>(close()); }

Device::Device(Device&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

bool Device::is_open() const noexcept { return fd_ >= 0; }

std::error_code Device::open(const std::string& path, const Config& config) noexcept {
  if (is_open()) return generic(std::errc::device_or_resource_busy);
  if (path.empty() || path.find('\0') != std::string::npos) {
    return generic(std::errc::invalid_argument);
  }
  if (!valid(config.mode) || !valid(config.bit_order) || config.max_speed_hz == 0 ||
      config.bits_per_word == 0) {
    return generic(std::errc::invalid_argument);
  }

  const auto opened = spidev_adapter::open_path(path.c_str(), O_RDWR | O_CLOEXEC);
  if (opened.value < 0) return system(opened.error);
  const auto fd = opened.value;

  NativeConfig original{};
  const auto mode_captured = spidev_adapter::read_mode32(fd, original.mode);
  if (mode_captured.value < 0) {
    static_cast<void>(spidev_adapter::close_fd(fd));
    return mode_captured.error == ENOTTY
               ? generic(std::errc::inappropriate_io_control_operation)
               : system(mode_captured.error);
  }
  const auto bits_captured = spidev_adapter::read_bits_per_word(fd, original.bits_per_word);
  if (bits_captured.value < 0) {
    static_cast<void>(spidev_adapter::close_fd(fd));
    return system(bits_captured.error);
  }
  const auto speed_captured = spidev_adapter::read_max_speed_hz(fd, original.max_speed_hz);
  if (speed_captured.value < 0) {
    static_cast<void>(spidev_adapter::close_fd(fd));
    return system(speed_captured.error);
  }

  const auto candidate_mode = requested_mode(config.mode, config.bit_order, original.mode);
  auto stage = ConfigStage::Mode;
  const auto mode_written = spidev_adapter::write_mode32(fd, candidate_mode);
  if (mode_written.value < 0) {
    const auto error = system(mode_written.error);
    restore(fd, original, stage);
    return error;
  }
  std::uint32_t actual_mode{};
  const auto mode_readback = spidev_adapter::read_mode32(fd, actual_mode);
  if (mode_readback.value < 0 || !mode_matches(actual_mode, candidate_mode)) {
    const auto error = mode_readback.value < 0 ? system(mode_readback.error)
                                               : generic(std::errc::io_error);
    restore(fd, original, stage);
    return error;
  }

  stage = ConfigStage::BitsPerWord;
  const auto bits_written = spidev_adapter::write_bits_per_word(fd, config.bits_per_word);
  if (bits_written.value < 0) {
    const auto error = system(bits_written.error);
    restore(fd, original, stage);
    return error;
  }
  std::uint8_t actual_bits_per_word{};
  const auto bits_readback = spidev_adapter::read_bits_per_word(fd, actual_bits_per_word);
  if (bits_readback.value < 0 || actual_bits_per_word != config.bits_per_word) {
    const auto error = bits_readback.value < 0 ? system(bits_readback.error)
                                               : generic(std::errc::io_error);
    restore(fd, original, stage);
    return error;
  }

  stage = ConfigStage::MaxSpeed;
  const auto speed_written = spidev_adapter::write_max_speed_hz(fd, config.max_speed_hz);
  if (speed_written.value < 0) {
    const auto error = system(speed_written.error);
    restore(fd, original, stage);
    return error;
  }
  std::uint32_t actual_max_speed_hz{};
  const auto speed_readback = spidev_adapter::read_max_speed_hz(fd, actual_max_speed_hz);
  if (speed_readback.value < 0 || actual_max_speed_hz != config.max_speed_hz) {
    const auto error = speed_readback.value < 0 ? system(speed_readback.error)
                                                : generic(std::errc::io_error);
    restore(fd, original, stage);
    return error;
  }

  fd_ = fd;
  return {};
}

std::error_code Device::close() noexcept {
  if (!is_open()) return {};
  const auto fd = fd_;
  fd_ = -1;
  const auto closed = spidev_adapter::close_fd(fd);
  return closed.value < 0 ? system(closed.error) : std::error_code{};
}

std::error_code Device::transfer(
    const std::byte* tx, std::byte* rx, std::size_t size) noexcept {
  if (!is_open()) return generic(std::errc::bad_file_descriptor);
  if (size == 0) return {};
  if (tx == nullptr && rx == nullptr) return generic(std::errc::invalid_argument);
  constexpr auto kMaxLength =
      static_cast<std::size_t>(std::numeric_limits<int>::max()) <
              static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())
          ? static_cast<std::size_t>(std::numeric_limits<int>::max())
          : static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
  if (size > kMaxLength) return generic(std::errc::value_too_large);

  spi_ioc_transfer value{};
  value.tx_buf = static_cast<__u64>(reinterpret_cast<std::uintptr_t>(tx));
  value.rx_buf = static_cast<__u64>(reinterpret_cast<std::uintptr_t>(rx));
  value.len = static_cast<__u32>(size);
  const auto transferred = spidev_adapter::transfer(fd_, value);
  if (transferred.value < 0) return system(transferred.error);
  if (static_cast<std::size_t>(transferred.value) != size) {
    return generic(std::errc::io_error);
  }
  return {};
}

}  // namespace spi
