#pragma once

#include <cstddef>
#include <cstdint>

#include <linux/spi/spidev.h>

namespace spi::test {

enum class Operation {
  Open,
  Close,
  ReadMode,
  WriteMode,
  ReadBits,
  WriteBits,
  ReadSpeed,
  WriteSpeed,
  Transfer,
};

void reset_adapter() noexcept;
void set_result(Operation operation, int value, int error = 0) noexcept;
void set_result_on_call(
    Operation operation, std::size_t call_number, int value, int error = 0) noexcept;
void set_mode(std::uint32_t value) noexcept;
void set_bits_per_word(std::uint8_t value) noexcept;
void set_max_speed_hz(std::uint32_t value) noexcept;
void ignore_mode_writes() noexcept;
void ignore_bits_per_word_writes() noexcept;
void ignore_max_speed_hz_writes() noexcept;
[[nodiscard]] std::size_t operation_count() noexcept;
[[nodiscard]] Operation operation_at(std::size_t index) noexcept;
[[nodiscard]] int close_count() noexcept;
[[nodiscard]] int opened_fd() noexcept;
[[nodiscard]] int open_flags() noexcept;
[[nodiscard]] std::uint32_t current_mode() noexcept;
[[nodiscard]] std::uint8_t current_bits_per_word() noexcept;
[[nodiscard]] std::uint32_t current_max_speed_hz() noexcept;
[[nodiscard]] const spi_ioc_transfer& last_transfer() noexcept;

}  // namespace spi::test
