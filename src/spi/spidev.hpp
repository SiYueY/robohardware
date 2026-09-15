#pragma once

#include <cstddef>
#include <cstdint>

#include <linux/spi/spidev.h>

namespace spi::spidev {

struct Config {
    std::uint32_t mode;
    std::uint8_t bits_per_word;
    std::uint32_t max_speed;  // Unit: Hz.
};

[[nodiscard]] int open(const char* path) noexcept;
[[nodiscard]] int close(int fd) noexcept;
[[nodiscard]] int read_config(int fd, Config& config) noexcept;
[[nodiscard]] int write_mode(int fd, std::uint32_t mode) noexcept;
[[nodiscard]] int write_bits_per_word(int fd, std::uint8_t bits_per_word) noexcept;
[[nodiscard]] int write_max_speed(int fd, std::uint32_t max_speed) noexcept;
[[nodiscard]] int message(int fd, const std::byte* tx, std::byte* rx, std::size_t size) noexcept;

}  // namespace spi::spidev
