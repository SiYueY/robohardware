#include "canopen/pdo.hpp"
namespace canopen {
namespace {
std::uint64_t mask(std::uint8_t n) noexcept { return n == 64 ? ~0ULL : ((1ULL << n) - 1ULL); }
std::uint64_t bits_at(
    const std::array<std::byte, 8>& d, std::uint8_t off, std::uint8_t n) noexcept {
    std::uint64_t v = 0;
    for (std::uint8_t i = 0; i < n; ++i)
        if ((static_cast<unsigned char>(d[(off + i) / 8]) >> ((off + i) % 8)) & 1U) v |= 1ULL << i;
    return v;
}
void put(std::array<std::byte, 8>& d, std::uint8_t off, std::uint8_t n, std::uint64_t v) noexcept {
    for (std::uint8_t i = 0; i < n; ++i) {
        auto& b = d[(off + i) / 8];
        auto x = static_cast<unsigned char>(b);
        x = static_cast<unsigned char>(
            (x & ~(1U << ((off + i) % 8))) | (((v >> i) & 1U) << ((off + i) % 8)));
        b = static_cast<std::byte>(x);
    }
}
}  // namespace
Result<void> PdoPlan::build(
    const PdoConfig& config, const ObjectDictionary& dictionary, ProcessImage& image) {
    if (frozen_ || config.cob_id > 0x7FF || config.mappings.empty() ||
        config.mappings.size() > kMaxMappings || config.transmission_type == 0 ||
        config.transmission_type > 240)
        return Error{
            config.transmission_type == 0 || config.transmission_type > 240
                ? ErrorCode::UnsupportedTransmissionType
                : ErrorCode::InvalidPdoMapping};
    std::uint8_t offset = 0;
    for (const auto& m : config.mappings) {
        auto object = dictionary.find(m.object);
        if (!object || m.bit_length == 0 || m.bit_length != object.value()->bit_length ||
            m.is_signed != object.value()->is_signed || offset + m.bit_length > 64)
            return Error{ErrorCode::InvalidPdoMapping, 0, m.object.index, m.object.subindex};
        auto slot = image.add(m.object, m.bit_length, m.is_signed);
        if (!slot) return slot.error();
        entries_[count_++] = {slot.value(), m.bit_length, m.is_signed, offset};
        offset = static_cast<std::uint8_t>(offset + m.bit_length);
    }
    cob_id_ = config.cob_id;
    transmission_type_ = config.transmission_type;
    bits_ = offset;
    frozen_ = true;
    return {};
}
Result<void> PdoPlan::decode(const can::Frame& frame, ProcessImage& image) const noexcept {
    if (!frozen_ || frame.id != cob_id_ || frame.size * 8 < bits_)
        return Error{ErrorCode::PdoDecodeError};
    for (std::uint8_t i = 0; i < count_; ++i) {
        auto v = bits_at(frame.data, entries_[i].bit_offset, entries_[i].bit_length);
        if (!image.write(entries_[i].slot, v)) return Error{ErrorCode::PdoDecodeError};
    }
    return {};
}
Result<void> PdoPlan::encode(const ProcessImage& image, can::Frame& frame) const noexcept {
    if (!frozen_) return Error{ErrorCode::PdoEncodeError};
    frame = {};
    frame.id = cob_id_;
    frame.format = can::FrameFormat::Standard;
    frame.type = can::FrameType::Data;
    frame.size = static_cast<std::uint8_t>((bits_ + 7) / 8);
    for (std::uint8_t i = 0; i < count_; ++i) {
        auto value = image.read(entries_[i].slot);
        if (!value) return Error{ErrorCode::PdoEncodeError};
        put(frame.data, entries_[i].bit_offset, entries_[i].bit_length,
            value.value() & mask(entries_[i].bit_length));
    }
    return {};
}
Result<void> PdoPlan::commit(const ProcessImage& pending, ProcessImage& active) const noexcept {
    if (!frozen_) return Error{ErrorCode::InvalidState};
    for (std::uint8_t i = 0; i < count_; ++i) {
        auto value = pending.read(entries_[i].slot);
        if (!value || !active.write(entries_[i].slot, value.value()))
            return Error{ErrorCode::PdoDecodeError};
    }
    return {};
}
}  // namespace canopen
