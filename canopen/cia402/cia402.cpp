#include "canopen/cia402/cia402.hpp"
namespace canopen::cia402 {
namespace {
constexpr std::uint16_t statusword(PdsState state) noexcept {
    switch (state) {
        case PdsState::NotReadyToSwitchOn:
            return 0x0000;
        case PdsState::SwitchOnDisabled:
            return 0x0040;
        case PdsState::ReadyToSwitchOn:
            return 0x0021;
        case PdsState::SwitchedOn:
            return 0x0023;
        case PdsState::OperationEnabled:
            return 0x0027;
        case PdsState::QuickStopActive:
            return 0x0007;
        case PdsState::FaultReactionActive:
            return 0x000F;
        case PdsState::Fault:
            return 0x0008;
    }
    return 0;
}
bool matches(std::uint16_t word, std::uint16_t mask, std::uint16_t value) noexcept {
    return (word & mask) == value;
}
}  // namespace
PdsState decode_statusword(std::uint16_t s) noexcept {
    if ((s & 0x4F) == 0x00) return PdsState::NotReadyToSwitchOn;
    if ((s & 0x4F) == 0x40) return PdsState::SwitchOnDisabled;
    if ((s & 0x6F) == 0x21) return PdsState::ReadyToSwitchOn;
    if ((s & 0x6F) == 0x23) return PdsState::SwitchedOn;
    if ((s & 0x6F) == 0x27) return PdsState::OperationEnabled;
    if ((s & 0x6F) == 0x07) return PdsState::QuickStopActive;
    if ((s & 0x4F) == 0x0F) return PdsState::FaultReactionActive;
    return PdsState::Fault;
}
std::uint16_t controlword_for(PdsState current, PdsState desired, bool reset) noexcept {
    if (reset && current == PdsState::Fault) return 0x80;
    if (desired == PdsState::OperationEnabled) {
        if (current == PdsState::SwitchOnDisabled) return 0x06;
        if (current == PdsState::ReadyToSwitchOn) return 0x07;
        return 0x0F;
    }
    if (desired == PdsState::SwitchedOn) return 0x07;
    if (desired == PdsState::ReadyToSwitchOn) return 0x06;
    if (desired == PdsState::QuickStopActive) return 0x02;
    if (desired == PdsState::SwitchOnDisabled) return 0x00;
    return 0;
}
Result<ProcessDataBinding> ProcessDataBinding::resolve(const ProcessImage& image) noexcept {
    ProcessDataBinding binding{};
    const auto resolve = [&image](ObjectKey key, ProcessSlot& slot) -> bool {
        auto found = image.find(key);
        if (!found) return false;
        slot = *found;
        return true;
    };
    if (!resolve({0x6040, 0}, binding.controlword) || !resolve({0x6041, 0}, binding.statusword) ||
        !resolve({0x6060, 0}, binding.mode) || !resolve({0x6061, 0}, binding.mode_display) ||
        !resolve({0x607A, 0}, binding.target_position) ||
        !resolve({0x6064, 0}, binding.position_actual) ||
        !resolve({0x60FF, 0}, binding.target_velocity) ||
        !resolve({0x606C, 0}, binding.velocity_actual) ||
        !resolve({0x6071, 0}, binding.target_torque) ||
        !resolve({0x6077, 0}, binding.torque_actual))
        return Error{ErrorCode::InvalidObject};
    return binding;
}
PdsState Master::state(const ProcessImage& i) const noexcept {
    auto s = i.read(binding_.statusword);
    return decode_statusword(static_cast<std::uint16_t>(s ? s.value() : 0));
}
void Master::request(ProcessImage& i, PdsState d, bool r) const noexcept {
    i.write(binding_.controlword, controlword_for(state(i), d, r));
}
void Master::set_mode(ProcessImage& i, OperationMode m) const noexcept {
    i.write(binding_.mode, static_cast<std::uint8_t>(m));
}
void Master::set_target_position(ProcessImage& i, std::int32_t value) const noexcept {
    i.write(binding_.target_position, static_cast<std::uint32_t>(value));
}
void Master::set_target_velocity(ProcessImage& i, std::int32_t value) const noexcept {
    i.write(binding_.target_velocity, static_cast<std::uint32_t>(value));
}
void Master::set_target_torque(ProcessImage& i, std::int16_t value) const noexcept {
    i.write(binding_.target_torque, static_cast<std::uint16_t>(value));
}
void Slave::update(ProcessImage& i) noexcept {
    auto control = i.read(binding_.controlword);
    auto c = static_cast<std::uint16_t>(control ? control.value() : 0);
    if (fault_) {
        if (c & 0x80) {
            fault_ = false;
            state_ = PdsState::SwitchOnDisabled;
        } else {
            state_ = PdsState::Fault;
            i.write(binding_.statusword, statusword(state_));
            return;
        }
    }
    const bool disable_voltage = matches(c, 0x0082, 0x0000);
    const bool shutdown = matches(c, 0x0087, 0x0006);
    const bool switch_on = matches(c, 0x008F, 0x0007);
    const bool enable_operation = matches(c, 0x008F, 0x000F);
    const bool quick_stop = matches(c, 0x0086, 0x0002);
    if (disable_voltage)
        state_ = PdsState::SwitchOnDisabled;
    else if (state_ == PdsState::SwitchOnDisabled && shutdown)
        state_ = PdsState::ReadyToSwitchOn;
    else if (state_ == PdsState::ReadyToSwitchOn && switch_on)
        state_ = PdsState::SwitchedOn;
    else if (
        (state_ == PdsState::SwitchedOn || state_ == PdsState::QuickStopActive) && enable_operation)
        state_ = PdsState::OperationEnabled;
    else if (state_ == PdsState::OperationEnabled && switch_on)
        state_ = PdsState::SwitchedOn;
    else if (state_ == PdsState::OperationEnabled && quick_stop)
        state_ = PdsState::QuickStopActive;
    else if ((state_ == PdsState::SwitchedOn || state_ == PdsState::OperationEnabled) && shutdown)
        state_ = PdsState::ReadyToSwitchOn;
    i.write(binding_.statusword, statusword(state_));
    auto mode = i.read(binding_.mode);
    if (mode) i.write(binding_.mode_display, mode.value());
}
}  // namespace canopen::cia402
