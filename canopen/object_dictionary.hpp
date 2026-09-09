#pragma once
#include "canopen/error.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace canopen {
struct ObjectKey {
    std::uint16_t index{0};
    std::uint8_t subindex{0};
    constexpr bool operator==(const ObjectKey& rhs) const noexcept {
        return index == rhs.index && subindex == rhs.subindex;
    }
};
enum class ObjectAccess : std::uint8_t { ReadOnly, WriteOnly, ReadWrite };
struct ObjectEntry {
    ObjectKey key{};
    std::uint8_t bit_length{0};
    bool is_signed{false};
    ObjectAccess access{ObjectAccess::ReadWrite};
    std::vector<std::byte> value;
};
class ObjectDictionary {
public:
    Result<void> add(ObjectEntry entry);
    Result<ObjectEntry*> find(ObjectKey key) noexcept;
    Result<const ObjectEntry*> find(ObjectKey key) const noexcept;
    Result<void> read(ObjectKey key, std::byte* data, std::size_t& size) const noexcept;
    Result<void> write(ObjectKey key, const std::byte* data, std::size_t size) noexcept;

private:
    std::vector<ObjectEntry> entries_;
};
}  // namespace canopen
