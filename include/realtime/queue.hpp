#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace realtime {

template <typename T, std::size_t N>
class Queue {
    static_assert(N > 0, "Queue capacity must be non-zero");
    static_assert(
        std::is_nothrow_copy_constructible_v<T>, "Queue values must be nothrow copy constructible");
    static_assert(
        std::is_nothrow_copy_assignable_v<T>, "Queue values must be nothrow copy assignable");

public:
    bool try_push(const T& value) noexcept {
        const auto write = write_index_.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == read_index_.load(std::memory_order_acquire)) return false;
        if (slots_[write])
            *slots_[write] = value;
        else
            slots_[write].emplace(value);
        write_index_.store(next, std::memory_order_release);
        return true;
    }
    bool try_pop(T& value) noexcept {
        const auto read = read_index_.load(std::memory_order_relaxed);
        if (read == write_index_.load(std::memory_order_acquire)) return false;
        value = *slots_[read];
        read_index_.store(increment(read), std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kStorageSize = N + 1;
    static constexpr std::size_t increment(std::size_t index) noexcept {
        return (index + 1) % kStorageSize;
    }
    std::array<std::optional<T>, kStorageSize> slots_{};
    alignas(64) std::atomic<std::size_t> write_index_{0};
    alignas(64) std::atomic<std::size_t> read_index_{0};
};

}  // namespace realtime
