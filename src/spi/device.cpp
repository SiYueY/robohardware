#include <spi/device.hpp>

#include "spidev.hpp"

#include <cerrno>
#include <cstdint>

#include <linux/spi/spi.h>

namespace spi {
namespace {

enum class ConfigStage { None, Mode, BitsPerWord, MaxSpeed };

[[nodiscard]] Error to_error(int native_error) noexcept { return static_cast<Error>(native_error); }

[[nodiscard]] hardware::Result<void, Error> to_failure(int native_error) noexcept {
    return hardware::Result<void, Error>::failure(to_error(native_error));
}

[[nodiscard]] bool is_valid(Mode mode) noexcept {
    return mode == Mode::Mode0 || mode == Mode::Mode1 || mode == Mode::Mode2 || mode == Mode::Mode3;
}

[[nodiscard]] bool is_valid(BitOrder bit_order) noexcept {
    return bit_order == BitOrder::MsbFirst || bit_order == BitOrder::LsbFirst;
}

[[nodiscard]] bool is_valid(const Config& config) noexcept {
    return is_valid(config.mode) && is_valid(config.bit_order) && config.max_speed > 0 &&
           config.bits_per_word > 0;
}

[[nodiscard]] std::uint32_t to_spidev_mode(
    Mode mode, BitOrder bit_order, std::uint32_t original_mode) noexcept {
    auto spidev_mode = original_mode & ~(SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST);
    switch (mode) {
        case Mode::Mode0:
            break;
        case Mode::Mode1:
            spidev_mode |= SPI_CPHA;
            break;
        case Mode::Mode2:
            spidev_mode |= SPI_CPOL;
            break;
        case Mode::Mode3:
            spidev_mode |= SPI_CPOL | SPI_CPHA;
            break;
    }
    if (bit_order == BitOrder::LsbFirst) spidev_mode |= SPI_LSB_FIRST;
    return spidev_mode;
}

[[nodiscard]] bool mode_matches(std::uint32_t actual, std::uint32_t expected) noexcept {
    constexpr std::uint32_t kOwnedModeBits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;
    return (actual & kOwnedModeBits) == (expected & kOwnedModeBits);
}

void restore_config(int fd, const spidev::Config& original, ConfigStage stage) noexcept {
    if (stage == ConfigStage::MaxSpeed) {
        static_cast<void>(spidev::write_max_speed(fd, original.max_speed));
    }
    if (stage == ConfigStage::MaxSpeed || stage == ConfigStage::BitsPerWord) {
        static_cast<void>(spidev::write_bits_per_word(fd, original.bits_per_word));
    }
    if (stage != ConfigStage::None) {
        static_cast<void>(spidev::write_mode(fd, original.mode));
    }
}

[[nodiscard]] hardware::Result<void, Error> fail_open(int fd, int native_error) noexcept {
    static_cast<void>(spidev::close(fd));
    return to_failure(native_error);
}

[[nodiscard]] hardware::Result<void, Error> fail_configuration(
    int fd, const spidev::Config& original, ConfigStage stage, int native_error) noexcept {
    restore_config(fd, original, stage);
    return fail_open(fd, native_error);
}

[[nodiscard]] hardware::Result<void, Error> configuration_mismatch(
    int fd, const spidev::Config& original, ConfigStage stage) noexcept {
    restore_config(fd, original, stage);
    static_cast<void>(spidev::close(fd));
    return hardware::Result<void, Error>::failure(Error::ConfigurationMismatch);
}

}  // namespace

Device::~Device() noexcept { static_cast<void>(close()); }

Device::Device(Device&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

bool Device::is_open() const noexcept { return fd_ >= 0; }

hardware::Result<void, Error> Device::open(const std::string& path, const Config& config) noexcept {
    if (is_open()) return hardware::Result<void, Error>::failure(Error::AlreadyOpen);
    if (path.empty() || path.find('\0') != std::string::npos || !is_valid(config)) {
        return hardware::Result<void, Error>::failure(Error::InvalidArgument);
    }

    const int fd = spidev::open(path.c_str());
    if (fd < 0) return to_failure(errno);

    spidev::Config original{};
    if (spidev::read_config(fd, original) < 0) return fail_open(fd, errno);

    const std::uint32_t requested_spidev_mode =
        to_spidev_mode(config.mode, config.bit_order, original.mode);
    if (spidev::write_mode(fd, requested_spidev_mode) < 0) {
        return fail_configuration(fd, original, ConfigStage::Mode, errno);
    }

    spidev::Config actual{};
    if (spidev::read_config(fd, actual) < 0) {
        return fail_configuration(fd, original, ConfigStage::Mode, errno);
    }
    if (!mode_matches(actual.mode, requested_spidev_mode)) {
        return configuration_mismatch(fd, original, ConfigStage::Mode);
    }

    if (spidev::write_bits_per_word(fd, config.bits_per_word) < 0) {
        return fail_configuration(fd, original, ConfigStage::BitsPerWord, errno);
    }
    if (spidev::read_config(fd, actual) < 0) {
        return fail_configuration(fd, original, ConfigStage::BitsPerWord, errno);
    }
    if (actual.bits_per_word != config.bits_per_word) {
        return configuration_mismatch(fd, original, ConfigStage::BitsPerWord);
    }

    if (spidev::write_max_speed(fd, config.max_speed) < 0) {
        return fail_configuration(fd, original, ConfigStage::MaxSpeed, errno);
    }
    if (spidev::read_config(fd, actual) < 0) {
        return fail_configuration(fd, original, ConfigStage::MaxSpeed, errno);
    }
    if (actual.max_speed != config.max_speed) {
        return configuration_mismatch(fd, original, ConfigStage::MaxSpeed);
    }

    fd_ = fd;
    return hardware::Result<void, Error>::success();
}

hardware::Result<void, Error> Device::close() noexcept {
    if (!is_open()) return hardware::Result<void, Error>::success();

    const int fd = fd_;
    fd_ = -1;
    if (spidev::close(fd) < 0) return to_failure(errno);
    return hardware::Result<void, Error>::success();
}

hardware::Result<void, Error> Device::transfer(
    const std::byte* tx, std::byte* rx, std::size_t size) noexcept {
    if (!is_open()) return hardware::Result<void, Error>::failure(Error::NotOpen);
    if (size == 0) return hardware::Result<void, Error>::success();
    if (tx == nullptr && rx == nullptr) {
        return hardware::Result<void, Error>::failure(Error::InvalidArgument);
    }

    const int transferred = spidev::message(fd_, tx, rx, size);
    if (transferred < 0) return to_failure(errno);
    if (static_cast<std::size_t>(transferred) != size) {
        return hardware::Result<void, Error>::failure(Error::TransferMismatch);
    }
    return hardware::Result<void, Error>::success();
}

}  // namespace spi
