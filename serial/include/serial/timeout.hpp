#pragma once

#include <chrono>
#include <cstdint>

namespace serial {

class Timeout final {
 public:
  Timeout() = delete;

  [[nodiscard]] static Timeout immediate() noexcept;
  [[nodiscard]] static Timeout infinite() noexcept;
  [[nodiscard]] static Timeout after(std::chrono::nanoseconds duration) noexcept;
  [[nodiscard]] bool is_valid() const noexcept;

 private:
  enum class Kind : std::uint8_t { Immediate, Infinite, Finite, Invalid };

  explicit Timeout(Kind kind, std::chrono::nanoseconds duration) noexcept;

  Kind kind_;
  std::chrono::nanoseconds duration_;

  friend class Port;
  friend struct PortAccess;
};

}  // namespace serial
