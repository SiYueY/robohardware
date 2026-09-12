#include <realtime/buffer.hpp>

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
  realtime::Buffer<Payload> buffer;
  std::atomic<bool> start{false};
  std::atomic<bool> writer_finished{false};
  std::atomic<bool> failed{false};

  std::thread writer([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (std::uint64_t sequence = 1; sequence <= kOperationCount; ++sequence) {
      buffer.write(Payload{sequence, ~sequence});
    }
    writer_finished.store(true, std::memory_order_release);
  });

  std::thread reader([&] {
    while (!start.load(std::memory_order_acquire)) {
    }

    std::uint64_t previous = 0;
    Payload payload{};
    while (!writer_finished.load(std::memory_order_acquire)) {
      if (!buffer.try_read(payload)) {
        continue;
      }
      if (payload.complement != ~payload.sequence || payload.sequence < previous) {
        failed.store(true, std::memory_order_release);
        return;
      }
      previous = payload.sequence;
    }

    while (!buffer.try_read(payload) || payload.sequence != kOperationCount) {
    }
    if (payload.complement != ~payload.sequence) {
      failed.store(true, std::memory_order_release);
    }
  });

  start.store(true, std::memory_order_release);
  writer.join();
  reader.join();

  if (failed.load(std::memory_order_acquire)) {
    std::cerr << "buffer stress test observed a torn or regressing publication\n";
    return 1;
  }
  return 0;
}
