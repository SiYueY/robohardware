#pragma once

#include <array>
#include <chrono>
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

struct Filter {
    std::uint32_t id{0};
    std::uint32_t mask{0};
    FrameFormat format{FrameFormat::Standard};
};

using Timestamp = std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>;

struct RxInfo {
    Timestamp received_at{};
};

}  // namespace can
