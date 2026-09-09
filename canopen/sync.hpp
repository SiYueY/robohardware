#pragma once

#include <cstdint>

namespace canopen {
class Sync {
public:
    void reset() noexcept { count_ = 0; }
    void on_event() noexcept { ++count_; }
    std::uint64_t count() const noexcept { return count_; }
    bool due(std::uint8_t transmission_type) const noexcept {
        return transmission_type >= 1 && transmission_type <= 240 &&
               count_ % transmission_type == 0;
    }

private:
    std::uint64_t count_{0};
};
}  // namespace canopen
