#pragma once
#include "can/frame.hpp"
#include "canopen/error.hpp"
#include "canopen/object_dictionary.hpp"
#include "canopen/process_image.hpp"
#include <array>
#include <cstdint>
#include <vector>
namespace canopen {
struct PdoMappingEntry {
    ObjectKey object{};
    std::uint8_t bit_length{0};
    bool is_signed{false};
};
struct PdoConfig {
    std::uint32_t cob_id{0};
    std::uint8_t transmission_type{1};
    std::vector<PdoMappingEntry> mappings;
};
class PdoPlan {
public:
    static constexpr std::size_t kMaxMappings = 8;
    struct Entry {
        std::uint8_t slot;
        std::uint8_t bit_length;
        bool is_signed;
        std::uint8_t bit_offset;
    };
    Result<void> build(
        const PdoConfig& config, const ObjectDictionary& dictionary, ProcessImage& image);
    Result<void> decode(const can::Frame& frame, ProcessImage& image) const noexcept;
    Result<void> encode(const ProcessImage& image, can::Frame& frame) const noexcept;
    Result<void> commit(const ProcessImage& pending, ProcessImage& active) const noexcept;
    std::uint32_t cob_id() const noexcept { return cob_id_; }
    std::uint8_t transmission_type() const noexcept { return transmission_type_; }
    bool frozen() const noexcept { return frozen_; }
    bool synchronous() const noexcept {
        return transmission_type_ >= 1 && transmission_type_ <= 240;
    }

private:
    std::array<Entry, kMaxMappings> entries_{};
    std::uint8_t count_{0};
    std::uint8_t bits_{0};
    std::uint32_t cob_id_{0};
    std::uint8_t transmission_type_{0};
    bool frozen_{false};
};
}  // namespace canopen
