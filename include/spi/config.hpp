#pragma once

#include <cstdint>

namespace spi {

enum class Mode : std::uint8_t { Mode0, Mode1, Mode2, Mode3 };
enum class BitOrder : std::uint8_t { MsbFirst, LsbFirst };

struct Config {
    Mode mode;
    std::uint32_t max_speed_hz;
    std::uint8_t bits_per_word;
    BitOrder bit_order;
};

}  // namespace spi
