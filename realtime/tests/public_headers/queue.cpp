#include <realtime/queue.hpp>

namespace {

struct Payload {
  int value;

  Payload() = delete;
  explicit constexpr Payload(int initial_value) noexcept : value(initial_value) {}
};

[[maybe_unused]] void compile_queue_header() {
  realtime::Queue<Payload, 1> queue;
  Payload value{1};
  static_cast<void>(queue.try_push(value));
  static_cast<void>(queue.try_pop(value));
}

}  // namespace
