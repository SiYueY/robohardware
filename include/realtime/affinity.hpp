#pragma once

#include <optional>

namespace realtime {
struct AffinityConfig {
    std::optional<int> cpu;
};
}  // namespace realtime
