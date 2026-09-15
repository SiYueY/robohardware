#pragma once

#include <cstddef>
#include <cstdint>

#include <linux/spi/spidev.h>

namespace spi::spidev {

struct Config {
    std::uint32_t mode;
    std::uint8_t bits_per_word;

    // Unit: Hz.
    std::uint32_t max_speed;
};

[[nodiscard]] int open(const char* path) noexcept;
[[nodiscard]] int close(int fd) noexcept;
[[nodiscard]] int read_mode(int fd, std::uint32_t& mode) noexcept;
[[nodiscard]] int write_mode(int fd, std::uint32_t mode) noexcept;
[[nodiscard]] int read_bits_per_word(int fd, std::uint8_t& bits_per_word) noexcept;
[[nodiscard]] int write_bits_per_word(int fd, std::uint8_t bits_per_word) noexcept;

// Writes the maximum SPI clock speed.
// max_speed unit: Hz.
[[nodiscard]] int read_max_speed(int fd, std::uint32_t& max_speed) noexcept;
[[nodiscard]] int write_max_speed(int fd, std::uint32_t max_speed) noexcept;

[[nodiscard]] int message(int fd, spi_ioc_transfer* transfers, std::size_t count) noexcept;

}  // namespace spi::spidev
