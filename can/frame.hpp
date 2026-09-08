#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace can {

enum class FrameFormat : std::uint8_t {
    Standard,
    Extended,
};

enum class FrameType : std::uint8_t {
    Data,
    Remote,
};

struct Frame {
    std::uint32_t id{0};
    std::uint8_t size{0};
    FrameFormat format{FrameFormat::Standard};
    FrameType type{FrameType::Data};
    std::array<std::byte, 8> data{};
};

}  // namespace can
