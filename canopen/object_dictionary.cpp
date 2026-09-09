#include "canopen/object_dictionary.hpp"
#include <algorithm>
namespace canopen {
Result<void> ObjectDictionary::add(ObjectEntry entry) {
    if (entry.bit_length == 0 || entry.bit_length > 64 || entry.value.size() * 8 < entry.bit_length)
        return Error{ErrorCode::InvalidObject};
    if (find(entry.key)) return Error{ErrorCode::InvalidObject};
    entries_.push_back(std::move(entry));
    return {};
}
Result<ObjectEntry*> ObjectDictionary::find(ObjectKey key) noexcept {
    for (auto& e : entries_)
        if (e.key == key) return &e;
    return Error{ErrorCode::InvalidObject, 0, key.index, key.subindex};
}
Result<const ObjectEntry*> ObjectDictionary::find(ObjectKey key) const noexcept {
    for (const auto& e : entries_)
        if (e.key == key) return &e;
    return Error{ErrorCode::InvalidObject, 0, key.index, key.subindex};
}
Result<void> ObjectDictionary::read(
    ObjectKey key, std::byte* data, std::size_t& size) const noexcept {
    auto e = find(key);
    if (!e || e.value()->access == ObjectAccess::WriteOnly || size < e.value()->value.size())
        return Error{ErrorCode::InvalidObject, 0, key.index, key.subindex};
    std::copy(e.value()->value.begin(), e.value()->value.end(), data);
    size = e.value()->value.size();
    return {};
}
Result<void> ObjectDictionary::write(
    ObjectKey key, const std::byte* data, std::size_t size) noexcept {
    auto e = find(key);
    if (!e || e.value()->access == ObjectAccess::ReadOnly || size != e.value()->value.size())
        return Error{ErrorCode::InvalidObject, 0, key.index, key.subindex};
    std::copy(data, data + size, e.value()->value.begin());
    return {};
}
}  // namespace canopen
