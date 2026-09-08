#pragma once

#include <cstddef>

namespace realtime {
struct MemoryConfig {
    bool lock_memory{true};
    std::size_t prefault_bytes{0};
};
}  // namespace realtime
