#pragma once
#include "canopen/error.hpp"
#include <array>
#include <cstdint>
namespace canopen {
class ProcessImage {
public:
    static constexpr std::size_t kMaxValues = 64;
    Result<std::uint8_t> add(std::uint8_t bit_length, bool is_signed) noexcept {
        if (frozen_ || bit_length == 0 || bit_length > 64 || count_ == kMaxValues)
            return Error{ErrorCode::InvalidArgument};
        slots_[count_] = {bit_length, is_signed, 0};
        return count_++;
    }
    Result<void> freeze() noexcept {
        frozen_ = true;
        return {};
    }
    bool frozen() const noexcept { return frozen_; }
    Result<void> write(std::uint8_t slot, std::uint64_t raw) noexcept {
        if (slot >= count_) return Error{ErrorCode::InvalidArgument};
        slots_[slot].raw = raw & mask(slots_[slot].bits);
        return {};
    }
    Result<std::uint64_t> read(std::uint8_t slot) const noexcept {
        if (slot >= count_) return Error{ErrorCode::InvalidArgument};
        return slots_[slot].raw;
    }
    Result<std::int64_t> read_signed(std::uint8_t slot) const noexcept {
        if (slot >= count_ || !slots_[slot].is_signed) return Error{ErrorCode::InvalidArgument};
        const auto bits = slots_[slot].bits;
        const auto raw = slots_[slot].raw;
        return static_cast<std::int64_t>(
            bits == 64 ? raw : ((raw & (1ULL << (bits - 1))) ? (raw | ~mask(bits)) : raw));
    }
    std::uint8_t bits(std::uint8_t slot) const noexcept {
        return slot < count_ ? slots_[slot].bits : 0;
    }
    bool signedness(std::uint8_t slot) const noexcept {
        return slot < count_ && slots_[slot].is_signed;
    }

private:
    struct Slot {
        std::uint8_t bits;
        bool is_signed;
        std::uint64_t raw;
    };
    static constexpr std::uint64_t mask(std::uint8_t n) noexcept {
        return n == 64 ? ~0ULL : ((1ULL << n) - 1ULL);
    };
    std::array<Slot, kMaxValues> slots_{};
    std::uint8_t count_{0};
    bool frozen_{false};
};
}  // namespace canopen
