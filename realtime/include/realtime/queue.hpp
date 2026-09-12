#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace realtime {

template <typename T, std::size_t Capacity>
class Queue final {
  static_assert(Capacity > 0, "Queue capacity must be greater than zero");
  static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                "Queue payload must not be const or volatile");
  static_assert(std::is_trivially_copy_constructible_v<T>,
                "Queue payload must be trivially copy constructible");
  static_assert(std::is_trivially_copy_assignable_v<T>,
                "Queue payload must be trivially copy assignable");
  static_assert(std::is_trivially_destructible_v<T>,
                "Queue payload must be trivially destructible");
  static_assert(std::is_nothrow_copy_constructible_v<T>,
                "Queue payload copy construction must not throw");
  static_assert(std::is_nothrow_copy_assignable_v<T>,
                "Queue payload copy assignment must not throw");
  static_assert(std::atomic<std::size_t>::is_always_lock_free,
                "Queue requires always-lock-free size_t atomics");

 public:
  using value_type = T;

  Queue() noexcept = default;
  ~Queue() noexcept = default;

  Queue(const Queue&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(Queue&&) = delete;

  [[nodiscard]] bool try_push(const T& value) noexcept {
    const std::size_t write = write_index_.load(std::memory_order_relaxed);
    const std::size_t next_write = next(write);
    const std::size_t read = read_index_.load(std::memory_order_acquire);

    if (next_write == read) {
      return false;
    }

    ::new (static_cast<void*>(slot_address(write))) T(value);
    write_index_.store(next_write, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop(T& value) noexcept {
    const std::size_t read = read_index_.load(std::memory_order_relaxed);
    const std::size_t write = write_index_.load(std::memory_order_acquire);

    if (read == write) {
      return false;
    }

    T* const stored = std::launder(reinterpret_cast<T*>(slot_address(read)));
    value = *stored;
    std::destroy_at(stored);
    read_index_.store(next(read), std::memory_order_release);
    return true;
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

 private:
  using Slot = std::aligned_storage_t<sizeof(T), alignof(T)>;

  [[nodiscard]] static constexpr std::size_t next(std::size_t index) noexcept {
    return index == Capacity ? 0 : index + 1;
  }

  [[nodiscard]] Slot* slot_address(std::size_t index) noexcept {
    return index == Capacity ? &extra_slot_ : &slots_[index];
  }

  std::array<Slot, Capacity> slots_;
  Slot extra_slot_;
  std::atomic<std::size_t> write_index_{0};
  std::atomic<std::size_t> read_index_{0};
};

}  // namespace realtime
