#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace realtime {

/// Latest-value exchange for exactly one writer and one reader.
/// Reads and writes are non-blocking and allocation-free.
template <typename T>
class Value {
    static_assert(
        std::is_nothrow_copy_constructible_v<T>, "Value values must be nothrow copy constructible");
    static_assert(
        std::is_nothrow_copy_assignable_v<T>, "Value values must be nothrow copy assignable");
    static constexpr std::size_t kSlots = 3;
    static constexpr std::uint64_t kEmpty = ~std::uint64_t{0};
    static constexpr std::uint64_t kIndexMask = 0x3;

public:
    void write(const T& value) noexcept {
        const auto current = published_.load(std::memory_order_acquire);
        const auto current_slot =
            current == kEmpty ? kSlots : static_cast<std::size_t>(current & kIndexMask);
        std::size_t slot = kSlots;
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (i != current_slot && !reader_claimed_[i].load(std::memory_order_acquire)) {
                slot = i;
                break;
            }
        }
        if (slot == kSlots) return;
        if (slots_[slot])
            *slots_[slot] = value;
        else
            slots_[slot].emplace(value);
        published_.store((++generation_ << 2) | slot, std::memory_order_release);
    }

    bool read(T& value) const noexcept {
        for (;;) {
            const auto publication = published_.load(std::memory_order_acquire);
            if (publication == kEmpty) return false;
            const auto slot = static_cast<std::size_t>(publication & kIndexMask);
            reader_claimed_[slot].store(true, std::memory_order_release);
            if (published_.load(std::memory_order_acquire) != publication) {
                reader_claimed_[slot].store(false, std::memory_order_release);
                continue;
            }
            value = *slots_[slot];
            reader_claimed_[slot].store(false, std::memory_order_release);
            return true;
        }
    }

private:
    std::array<std::optional<T>, kSlots> slots_{};
    mutable std::array<std::atomic<bool>, kSlots> reader_claimed_{{false, false, false}};
    std::atomic<std::uint64_t> published_{kEmpty};
    std::uint64_t generation_{0};
};

}  // namespace realtime
