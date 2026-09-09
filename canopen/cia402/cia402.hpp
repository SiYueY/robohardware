#pragma once
#include "canopen/error.hpp"
#include "canopen/process_image.hpp"
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
    ProcessSlot controlword{0};
    ProcessSlot statusword{0};
    ProcessSlot mode{0};
    ProcessSlot mode_display{0};
    ProcessSlot target_position{0};
    ProcessSlot position_actual{0};
    ProcessSlot target_velocity{0};
    ProcessSlot velocity_actual{0};
    ProcessSlot target_torque{0};
    ProcessSlot torque_actual{0};

    static Result<ProcessDataBinding> resolve(const ProcessImage& image) noexcept;
};
class Master {
public:
    explicit Master(ProcessDataBinding binding) : binding_(binding) {}
    PdsState state(const ProcessImage& image) const noexcept;
    void request(ProcessImage& image, PdsState desired, bool fault_reset = false) const noexcept;
    void set_mode(ProcessImage& image, OperationMode mode) const noexcept;
    void set_target_position(ProcessImage& image, std::int32_t value) const noexcept;
    void set_target_velocity(ProcessImage& image, std::int32_t value) const noexcept;
    void set_target_torque(ProcessImage& image, std::int16_t value) const noexcept;

private:
    ProcessDataBinding binding_;
};
class Slave {
public:
    explicit Slave(ProcessDataBinding binding) : binding_(binding) {}
    void update(ProcessImage& image) noexcept;
    void inject_fault() noexcept { fault_ = true; }

private:
    ProcessDataBinding binding_;
    PdsState state_{PdsState::SwitchOnDisabled};
    bool fault_{false};
};
}  // namespace canopen::cia402
