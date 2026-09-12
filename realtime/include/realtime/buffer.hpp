#pragma once

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
    ::new (static_cast<void*>(slot_address(writer_slot_))) T(value);
    writer_sequence_ += kSequenceIncrement;
    const std::uint32_t previous =
        published_slot_.exchange(
            kValidBit | writer_sequence_ | writer_slot_, std::memory_order_acq_rel);
    writer_slot_ = previous & kSlotMask;
  }

  [[nodiscard]] bool try_read(T& value) noexcept {
    const std::uint32_t state = published_slot_.load(std::memory_order_acquire);
    if ((state & kValidBit) == 0U) {
      return false;
    }

    const std::uint32_t sequence = state & kSequenceMask;
    if (sequence == reader_sequence_) {
      value = *std::launder(reinterpret_cast<T*>(slot_address(reader_slot_)));
      return true;
    }

    const std::uint32_t previous =
        published_slot_.exchange(
            kValidBit | sequence | reader_slot_, std::memory_order_acq_rel);
    reader_slot_ = previous & kSlotMask;
    // A writer can publish between load and exchange. In that case `previous`
    // identifies the newer snapshot we acquired, while the exchange publishes
    // our retired slot under the sequence we observed. Keep that observed
    // sequence so a following read retains the snapshot just acquired.
    reader_sequence_ = sequence;
    value = *std::launder(reinterpret_cast<T*>(slot_address(reader_slot_)));
    return true;
  }

 private:
  using Slot = std::aligned_storage_t<sizeof(T), alignof(T)>;

  static constexpr std::uint32_t kSlotMask = 3U;
  static constexpr std::uint32_t kValidBit = 4U;
  static constexpr std::uint32_t kSequenceIncrement = 8U;
  static constexpr std::uint32_t kSequenceMask = ~std::uint32_t{7U};

  [[nodiscard]] Slot* slot_address(std::uint32_t slot) noexcept {
    return &slots_[slot];
  }

  Slot slots_[3];
  std::uint32_t writer_slot_{0};
  std::uint32_t reader_slot_{2};
  std::uint32_t writer_sequence_{0};
  std::uint32_t reader_sequence_{0};
  std::atomic<std::uint32_t> published_slot_{1};
};

}  // namespace realtime
