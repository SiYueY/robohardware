#include <realtime/queue.hpp>

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
    std::cerr << "queue test failed: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using Queue = realtime::Queue<Payload, 2>;
  static_assert(alignof(realtime::Queue<OverAlignedPayload, 3>) >=
                alignof(OverAlignedPayload));
  static_assert(std::is_trivially_copyable_v<OverAlignedPayload>);

  Queue queue;
  Payload output{99};
  bool passed = expect(queue.capacity() == 2, "capacity is not fully usable");

  passed &= expect(!queue.try_pop(output), "empty queue accepted pop");
  passed &= expect(output.value == 99, "empty pop modified output");

  passed &= expect(queue.try_push(Payload{1}), "first push failed");
  passed &= expect(queue.try_push(Payload{2}), "second push failed");
  passed &= expect(!queue.try_push(Payload{3}), "full queue accepted push");

  passed &= expect(queue.try_pop(output) && output.value == 1, "first FIFO value is wrong");
  passed &= expect(queue.try_pop(output) && output.value == 2, "second FIFO value is wrong");
  passed &= expect(!queue.try_pop(output), "drained queue accepted pop");
  passed &= expect(output.value == 2, "drained pop modified output");

  realtime::Queue<Payload, 3> wrapping_queue;
  passed &= expect(wrapping_queue.try_push(Payload{1}) &&
                       wrapping_queue.try_push(Payload{2}) &&
                       wrapping_queue.try_push(Payload{3}),
                   "non-power-of-two queue did not fill");
  passed &= expect(wrapping_queue.try_pop(output) && output.value == 1 &&
                       wrapping_queue.try_push(Payload{4}) &&
                       wrapping_queue.try_pop(output) && output.value == 2 &&
                       wrapping_queue.try_pop(output) && output.value == 3 &&
                       wrapping_queue.try_pop(output) && output.value == 4,
                   "non-power-of-two queue did not preserve FIFO through wrap");

  realtime::Queue<Payload, 1> single_slot_queue;
  passed &= expect(single_slot_queue.try_push(Payload{5}) &&
                       !single_slot_queue.try_push(Payload{6}) &&
                       single_slot_queue.try_pop(output) && output.value == 5 &&
                       single_slot_queue.try_push(Payload{6}) &&
                       single_slot_queue.try_pop(output) && output.value == 6,
                   "capacity-one queue did not alternate correctly");

  realtime::Queue<OverAlignedPayload, 3> over_aligned_queue;
  OverAlignedPayload over_aligned_output{0};
  passed &= expect(over_aligned_queue.try_push(OverAlignedPayload{7}) &&
                       over_aligned_queue.try_pop(over_aligned_output) &&
                       over_aligned_output.value == 7,
                   "over-aligned payload was not transferred correctly");

  return passed ? 0 : 1;
}
