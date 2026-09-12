#include <realtime/buffer.hpp>

#include <iostream>
#include <type_traits>

namespace {

struct Payload {
  int value;

  Payload() = delete;
  explicit constexpr Payload(int initial_value) noexcept : value(initial_value) {}
  Payload(const Payload&) noexcept = default;
  Payload& operator=(const Payload&) noexcept = default;
};

struct alignas(64) OverAlignedPayload {
  int value;

  OverAlignedPayload() = delete;
  explicit constexpr OverAlignedPayload(int initial_value) noexcept : value(initial_value) {}
  OverAlignedPayload(const OverAlignedPayload&) noexcept = default;
  OverAlignedPayload& operator=(const OverAlignedPayload&) noexcept = default;
};

bool expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "buffer test failed: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  static_assert(alignof(realtime::Buffer<OverAlignedPayload>) >=
                alignof(OverAlignedPayload));
  static_assert(std::is_trivially_copyable_v<OverAlignedPayload>);

  realtime::Buffer<Payload> buffer;
  Payload output{99};
  bool passed = expect(!buffer.try_read(output), "empty buffer returned a value");
  passed &= expect(output.value == 99, "empty read modified output");

  buffer.write(Payload{1});
  passed &= expect(buffer.try_read(output) && output.value == 1,
                   "first publication is unavailable or incorrect");
  output = Payload{0};
  passed &= expect(buffer.try_read(output) && output.value == 1,
                   "published value is not repeatable");

  buffer.write(Payload{2});
  passed &= expect(buffer.try_read(output) && output.value == 2,
                   "latest sequential publication is incorrect");

  for (int value = 3; value <= 32; ++value) {
    buffer.write(Payload{value});
  }
  passed &= expect(buffer.try_read(output) && output.value == 32,
                   "latest publication was not retained through repeated slot reuse");

  realtime::Buffer<OverAlignedPayload> over_aligned_buffer;
  OverAlignedPayload over_aligned_output{0};
  over_aligned_buffer.write(OverAlignedPayload{7});
  passed &= expect(over_aligned_buffer.try_read(over_aligned_output) &&
                       over_aligned_output.value == 7,
                   "over-aligned publication was not transferred correctly");

  return passed ? 0 : 1;
}
