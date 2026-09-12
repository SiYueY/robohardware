#include <serial/timeout.hpp>

namespace serial {

Timeout::Timeout(Kind kind, std::chrono::nanoseconds duration) noexcept
    : kind_(kind), duration_(duration) {}

Timeout Timeout::immediate() noexcept {
  return Timeout{Kind::Immediate, std::chrono::nanoseconds::zero()};
}

Timeout Timeout::infinite() noexcept {
  return Timeout{Kind::Infinite, std::chrono::nanoseconds::zero()};
}

Timeout Timeout::after(std::chrono::nanoseconds duration) noexcept {
  if (duration.count() < 0) {
    return Timeout{Kind::Invalid, duration};
  }
  if (duration == std::chrono::nanoseconds::zero()) {
    return immediate();
  }
  return Timeout{Kind::Finite, duration};
}

bool Timeout::is_valid() const noexcept { return kind_ != Kind::Invalid; }

}  // namespace serial
