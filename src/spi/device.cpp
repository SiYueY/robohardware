#include <spi/device.hpp>

#include "spidev.hpp"

#include <cerrno>
#include <cstdint>
#include <limits>

#include <linux/spi/spi.h>

namespace spi {
namespace {

enum class UpdateStage { None, Mode, BitsPerWord, MaxSpeed };

[[nodiscard]] Error to_error(int native_error) noexcept { return static_cast<Error>(native_error); }

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
    Mode mode, BitOrder bit_order, std::uint32_t current_mode) noexcept {
    auto spidev_mode = current_mode & ~(SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST);
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

[[nodiscard]] spidev::Config create_spidev_config(
    const Config& config, const spidev::Config& current) noexcept {
    return {
        to_spidev_mode(config.mode, config.bit_order, current.mode),
        config.bits_per_word,
        config.max_speed,
    };
}

[[nodiscard]] hardware::Result<spidev::Config, Error> read_spidev_config(int fd) noexcept {
    spidev::Config config{};
    if (spidev::read_mode(fd, config.mode) < 0) {
        return hardware::Result<spidev::Config, Error>::failure(to_error(errno));
    }
    if (spidev::read_bits_per_word(fd, config.bits_per_word) < 0) {
        return hardware::Result<spidev::Config, Error>::failure(to_error(errno));
    }
    if (spidev::read_max_speed(fd, config.max_speed) < 0) {
        return hardware::Result<spidev::Config, Error>::failure(to_error(errno));
    }
    return hardware::Result<spidev::Config, Error>::success(config);
}

void restore_spidev_config(int fd, const spidev::Config& original, UpdateStage stage) noexcept {
    switch (stage) {
        case UpdateStage::MaxSpeed:
            static_cast<void>(spidev::write_max_speed(fd, original.max_speed));
            [[fallthrough]];
        case UpdateStage::BitsPerWord:
            static_cast<void>(spidev::write_bits_per_word(fd, original.bits_per_word));
            [[fallthrough]];
        case UpdateStage::Mode:
            static_cast<void>(spidev::write_mode(fd, original.mode));
            break;
        case UpdateStage::None:
            break;
    }
}

[[nodiscard]] hardware::Result<void, Error> update_spidev_config(
    int fd, const spidev::Config& original, const spidev::Config& target) noexcept {
    if (spidev::write_mode(fd, target.mode) < 0) {
        const Error error = to_error(errno);
        restore_spidev_config(fd, original, UpdateStage::Mode);
        return hardware::Result<void, Error>::failure(error);
    }

    const auto mode_actual = read_spidev_config(fd);
    if (!mode_actual) {
        restore_spidev_config(fd, original, UpdateStage::Mode);
        return hardware::Result<void, Error>::failure(mode_actual.error());
    }
    constexpr std::uint32_t kOwnedModeBits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;
    if ((mode_actual.value().mode & kOwnedModeBits) != (target.mode & kOwnedModeBits)) {
        restore_spidev_config(fd, original, UpdateStage::Mode);
        return hardware::Result<void, Error>::failure(Error::ConfigurationMismatch);
    }

    if (spidev::write_bits_per_word(fd, target.bits_per_word) < 0) {
        const Error error = to_error(errno);
        restore_spidev_config(fd, original, UpdateStage::BitsPerWord);
        return hardware::Result<void, Error>::failure(error);
    }

    const auto bits_per_word_actual = read_spidev_config(fd);
    if (!bits_per_word_actual) {
        restore_spidev_config(fd, original, UpdateStage::BitsPerWord);
        return hardware::Result<void, Error>::failure(bits_per_word_actual.error());
    }
    if (bits_per_word_actual.value().bits_per_word != target.bits_per_word) {
        restore_spidev_config(fd, original, UpdateStage::BitsPerWord);
        return hardware::Result<void, Error>::failure(Error::ConfigurationMismatch);
    }

    if (spidev::write_max_speed(fd, target.max_speed) < 0) {
        const Error error = to_error(errno);
        restore_spidev_config(fd, original, UpdateStage::MaxSpeed);
        return hardware::Result<void, Error>::failure(error);
    }

    const auto max_speed_actual = read_spidev_config(fd);
    if (!max_speed_actual) {
        restore_spidev_config(fd, original, UpdateStage::MaxSpeed);
        return hardware::Result<void, Error>::failure(max_speed_actual.error());
    }
    if (max_speed_actual.value().max_speed != target.max_speed) {
        restore_spidev_config(fd, original, UpdateStage::MaxSpeed);
        return hardware::Result<void, Error>::failure(Error::ConfigurationMismatch);
    }

    return hardware::Result<void, Error>::success();
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
    if (fd < 0) return hardware::Result<void, Error>::failure(to_error(errno));

    auto current = read_spidev_config(fd);
    if (!current) {
        const Error error = current.error();
        static_cast<void>(spidev::close(fd));
        return hardware::Result<void, Error>::failure(error);
    }

    const auto target = create_spidev_config(config, current.value());
    auto updated = update_spidev_config(fd, current.value(), target);
    if (!updated) {
        const Error error = updated.error();
        static_cast<void>(spidev::close(fd));
        return hardware::Result<void, Error>::failure(error);
    }

    fd_ = fd;
    return hardware::Result<void, Error>::success();
}

hardware::Result<void, Error> Device::close() noexcept {
    if (!is_open()) return hardware::Result<void, Error>::success();

    const int fd = fd_;
    fd_ = -1;
    if (spidev::close(fd) < 0) {
        return hardware::Result<void, Error>::failure(to_error(errno));
    }
    return hardware::Result<void, Error>::success();
}

hardware::Result<void, Error> Device::transfer(
    const std::byte* tx, std::byte* rx, std::size_t size) noexcept {
    if (!is_open()) return hardware::Result<void, Error>::failure(Error::NotOpen);
    if (size == 0) return hardware::Result<void, Error>::success();
    if (tx == nullptr && rx == nullptr) {
        return hardware::Result<void, Error>::failure(Error::InvalidArgument);
    }

    constexpr std::size_t kMaximumTransferSize =
        std::numeric_limits<__u32>::max() < std::numeric_limits<int>::max()
            ? std::numeric_limits<__u32>::max()
            : std::numeric_limits<int>::max();
    if (size > kMaximumTransferSize) {
        return hardware::Result<void, Error>::failure(Error::MessageTooLong);
    }

    spi_ioc_transfer transfer{};
    transfer.tx_buf = static_cast<__u64>(reinterpret_cast<std::uintptr_t>(tx));
    transfer.rx_buf = static_cast<__u64>(reinterpret_cast<std::uintptr_t>(rx));
    transfer.len = static_cast<__u32>(size);
    const int transferred = spidev::message(fd_, &transfer, 1);
    if (transferred < 0) return hardware::Result<void, Error>::failure(to_error(errno));
    if (static_cast<std::size_t>(transferred) != size) {
        return hardware::Result<void, Error>::failure(Error::TransferMismatch);
    }
    return hardware::Result<void, Error>::success();
}

}  // namespace spi
