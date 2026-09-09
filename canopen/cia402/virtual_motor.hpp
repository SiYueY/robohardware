#pragma once

#include <cstdint>

namespace canopen::cia402 {
/// Small protocol-independent drive simulation used only by CiA402 integration tests.
class VirtualMotor {
public:
    void set_target(std::int64_t target) noexcept { target_ = target; }
    void step() noexcept { position_ = target_; }
    std::int64_t position() const noexcept { return position_; }
    void inject_fault() noexcept { fault_ = true; }
    void reset_fault() noexcept { fault_ = false; }
    bool faulted() const noexcept { return fault_; }

private:
    std::int64_t target_{0};
    std::int64_t position_{0};
    bool fault_{false};
};
}  // namespace canopen::cia402
