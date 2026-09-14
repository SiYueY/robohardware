#pragma once

#include <cstdint>

namespace spi {

enum class Mode { Mode0, Mode1, Mode2, Mode3 };
enum class BitOrder { MsbFirst, LsbFirst };

class Options final {
 public:
  Options(
      Mode mode,
      std::uint32_t frequency_hz,
      std::uint8_t bits_per_word,
      BitOrder bit_order) noexcept;

 private:
  Mode mode_;
  std::uint32_t frequency_hz_;
  std::uint8_t bits_per_word_;
  BitOrder bit_order_;

  friend class Device;
};

}  // namespace spi
