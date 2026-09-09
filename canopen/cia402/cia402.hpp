#pragma once
#include "canopen/process_image.hpp"
#include "canopen/cia402/virtual_motor.hpp"
#include <cstdint>
namespace canopen::cia402 {
enum class PdsState : std::uint8_t {
    NotReadyToSwitchOn,
    SwitchOnDisabled,
    ReadyToSwitchOn,
    SwitchedOn,
    OperationEnabled,
    QuickStopActive,
    FaultReactionActive,
    Fault
};
enum class OperationMode : std::int8_t {
    ProfilePosition = 1,
    Velocity = 3,
    ProfileVelocity = 3,
    Homing = 6,
    CyclicSynchronousPosition = 8,
    CyclicSynchronousVelocity = 9,
    CyclicSynchronousTorque = 10
};
PdsState decode_statusword(std::uint16_t statusword) noexcept;
std::uint16_t controlword_for(
    PdsState current, PdsState desired, bool fault_reset = false) noexcept;
struct ProcessDataBinding {
    std::uint8_t controlword{0};
    std::uint8_t statusword{0};
    std::uint8_t target{0};
    std::uint8_t feedback{0};
    std::uint8_t mode{0};
    std::uint8_t mode_display{0};
};
class Master {
public:
    explicit Master(ProcessDataBinding binding) : binding_(binding) {}
    PdsState state(const ProcessImage& image) const noexcept;
    void request(ProcessImage& image, PdsState desired, bool fault_reset = false) const noexcept;
    void set_mode(ProcessImage& image, OperationMode mode) const noexcept;

private:
    ProcessDataBinding binding_;
};
class Slave {
public:
    explicit Slave(ProcessDataBinding binding, VirtualMotor* motor = nullptr)
    : binding_(binding), motor_(motor) {}
    void update(ProcessImage& image) noexcept;
    void inject_fault() noexcept { fault_ = true; }

private:
    ProcessDataBinding binding_;
    VirtualMotor* motor_{nullptr};
    bool fault_{false};
};
}  // namespace canopen::cia402
