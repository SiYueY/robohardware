#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>

namespace realtime {

template <typename T>
class Buffer final {
  static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                "Buffer payload must not be const or volatile");
  static_assert(std::is_trivially_copy_constructible_v<T>,
                "Buffer payload must be trivially copy constructible");
  static_assert(std::is_trivially_copy_assignable_v<T>,
                "Buffer payload must be trivially copy assignable");
  static_assert(std::is_trivially_destructible_v<T>,
                "Buffer payload must be trivially destructible");
  static_assert(std::is_nothrow_copy_constructible_v<T>,
                "Buffer payload copy construction must not throw");
  static_assert(std::is_nothrow_copy_assignable_v<T>,
                "Buffer payload copy assignment must not throw");
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "Buffer requires always-lock-free uint32_t atomics");

 public:
  using value_type = T;

  Buffer() noexcept = default;
  ~Buffer() noexcept = default;

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&&) = delete;
  Buffer& operator=(Buffer&&) = delete;

  void write(const T& value) noexcept {
    const std::uint32_t reading_pair = reading_pair_.load(std::memory_order_seq_cst);
    const std::uint32_t target_pair = 1U - reading_pair;
    const std::uint32_t target_slot =
        1U - published_slot(target_pair).load(std::memory_order_seq_cst);

    ::new (static_cast<void*>(slot_address(target_pair, target_slot))) T(value);
    published_slot(target_pair).store(target_slot, std::memory_order_seq_cst);
    published_pair_state_.store(kValidBit | target_pair, std::memory_order_seq_cst);
  }

  [[nodiscard]] bool try_read(T& value) noexcept {
    const std::uint32_t state = published_pair_state_.load(std::memory_order_seq_cst);
    if ((state & kValidBit) == 0U) {
      return false;
    }

    const std::uint32_t pair = state & kPairBit;
    reading_pair_.store(pair, std::memory_order_seq_cst);
    const std::uint32_t slot = published_slot(pair).load(std::memory_order_seq_cst);
    value = *std::launder(reinterpret_cast<T*>(slot_address(pair, slot)));
    return true;
  }

 private:
  using Slot = std::aligned_storage_t<sizeof(T), alignof(T)>;

  static constexpr std::uint32_t kPairBit = 1U;
  static constexpr std::uint32_t kValidBit = 2U;

  [[nodiscard]] std::atomic<std::uint32_t>& published_slot(
      std::uint32_t pair) noexcept {
    return pair == 0U ? published_slot_0_ : published_slot_1_;
  }

  [[nodiscard]] Slot* slot_address(std::uint32_t pair, std::uint32_t slot) noexcept {
    return &slots_[pair][slot];
  }

  std::array<std::array<Slot, 2>, 2> slots_;
  std::atomic<std::uint32_t> reading_pair_{0};
  std::atomic<std::uint32_t> published_pair_state_{0};
  std::atomic<std::uint32_t> published_slot_0_{0};
  std::atomic<std::uint32_t> published_slot_1_{0};
};

}  // namespace realtime
