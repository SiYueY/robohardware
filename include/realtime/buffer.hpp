#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace realtime {

// The publication word packs an index and monotonically increasing generation.
// A reader claims the observed slot, then verifies the exact publication word.
// Thus a writer may reuse an old slot only when no reader can still copy it;
// the release/acquire publication makes completed payload writes visible.
template <typename T>
class Buffer {
    static_assert(
        std::is_nothrow_copy_constructible_v<T>,
        "Buffer values must be nothrow copy constructible");
    static_assert(
        std::is_nothrow_copy_assignable_v<T>, "Buffer values must be nothrow copy assignable");

    static constexpr std::size_t kSlots = 3;
    static constexpr std::uint64_t kEmpty = ~std::uint64_t{0};
    static constexpr std::uint64_t kIndexMask = 0x3;

public:
    bool write(const T& value) noexcept {
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
        if (slot == kSlots) return false;

        if (slots_[slot])
            *slots_[slot] = value;
        else
            slots_[slot].emplace(value);
        published_.store((++generation_ << 2) | slot, std::memory_order_release);
        return true;
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
    std::uint64_t generation_{0};  // Single-writer owned.
};

}  // namespace realtime
