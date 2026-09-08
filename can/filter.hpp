#pragma once

#include "can/frame.hpp"

#include <cstdint>

namespace can {

struct Filter {
    std::uint32_t id{0};
    std::uint32_t mask{0};
    FrameFormat format{FrameFormat::Standard};
};

}  // namespace can
