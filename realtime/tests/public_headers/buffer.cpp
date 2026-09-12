#include <realtime/buffer.hpp>

namespace {

struct Payload {
  int value;

  Payload() = delete;
  explicit constexpr Payload(int initial_value) noexcept : value(initial_value) {}
};

[[maybe_unused]] void compile_buffer_header() {
  realtime::Buffer<Payload> buffer;
  Payload value{1};
  buffer.write(value);
  static_cast<void>(buffer.try_read(value));
}

}  // namespace
