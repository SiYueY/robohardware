#include <realtime/queue.hpp>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

struct Payload {
  std::uint64_t sequence;
  std::uint64_t complement;
};

constexpr std::uint64_t kOperationCount = 200'000;

}  // namespace

int main() {
  realtime::Queue<Payload, 3> queue;
  std::atomic<bool> start{false};
  std::atomic<bool> failed{false};

  std::thread producer([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (std::uint64_t sequence = 1; sequence <= kOperationCount; ++sequence) {
      if (failed.load(std::memory_order_acquire)) {
        return;
      }
      const Payload payload{sequence, ~sequence};
      while (!queue.try_push(payload) && !failed.load(std::memory_order_acquire)) {
      }
    }
  });

  std::thread consumer([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (std::uint64_t expected = 1; expected <= kOperationCount; ++expected) {
      Payload payload{};
      while (!queue.try_pop(payload)) {
      }
      if (payload.sequence != expected || payload.complement != ~expected) {
        failed.store(true, std::memory_order_release);
        return;
      }
    }
  });

  start.store(true, std::memory_order_release);
  producer.join();
  consumer.join();

  if (failed.load(std::memory_order_acquire)) {
    std::cerr << "queue stress test observed a reordered or torn payload\n";
    return 1;
  }
  return 0;
}
