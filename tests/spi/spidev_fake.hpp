#pragma once

#include "spidev.hpp"

#include <cstddef>
#include <vector>

namespace spi::spidev::fake {

enum class Operation {
    Open,
    Close,
    ReadMode,
    WriteMode,
    ReadBitsPerWord,
    WriteBitsPerWord,
    ReadMaxSpeed,
    WriteMaxSpeed,
    Message,
};

void reset() noexcept;
void fail(Operation operation, std::size_t occurrence, int native_error) noexcept;
void ignore_write(Operation operation) noexcept;
void set_message_result(int result) noexcept;
[[nodiscard]] const std::vector<Operation>& calls() noexcept;

}  // namespace spi::spidev::fake
