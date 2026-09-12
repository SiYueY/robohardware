#include <realtime/clock.hpp>

namespace {

[[maybe_unused]] void compile_clock_header() {
  const realtime::TimePoint now = realtime::Clock::now();
  static_cast<void>(now);
}

}  // namespace
