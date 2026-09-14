#include <spi/options.hpp>

namespace spi {

Options::Options(
    Mode mode,
    std::uint32_t frequency_hz,
    std::uint8_t bits_per_word,
    BitOrder bit_order) noexcept
    : mode_(mode),
      frequency_hz_(frequency_hz),
      bits_per_word_(bits_per_word),
      bit_order_(bit_order) {}

}  // namespace spi
