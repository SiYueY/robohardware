#include "canopen/cia402/cia402.hpp"
namespace canopen::cia402 {
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
    if (desired == PdsState::SwitchOnDisabled) return 0x00;
    return 0;
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
void Slave::update(ProcessImage& i) noexcept {
    auto control = i.read(binding_.controlword);
    auto c = static_cast<std::uint16_t>(control ? control.value() : 0);
    if (motor_ && motor_->faulted()) fault_ = true;
    if (fault_) {
        if (c & 0x80) {
            fault_ = false;
            if (motor_) motor_->reset_fault();
        } else {
            i.write(binding_.statusword, 0x08);
            return;
        }
    }
    auto old = decode_statusword(static_cast<std::uint16_t>(i.read(binding_.statusword).value()));
    PdsState next = old;
    if (c == 0)
        next = PdsState::SwitchOnDisabled;
    else if (c == 0x06)
        next = PdsState::ReadyToSwitchOn;
    else if (c == 0x07)
        next = PdsState::SwitchedOn;
    else if (c == 0x0F)
        next = PdsState::OperationEnabled;
    std::uint16_t sw = 0x40;
    if (next == PdsState::ReadyToSwitchOn)
        sw = 0x21;
    else if (next == PdsState::SwitchedOn)
        sw = 0x23;
    else if (next == PdsState::OperationEnabled)
        sw = 0x27;
    i.write(binding_.statusword, sw);
    auto target = i.read(binding_.target);
    if (target) {
        if (motor_) {
            motor_->set_target(static_cast<std::int64_t>(target.value()));
            motor_->step();
            i.write(binding_.feedback, static_cast<std::uint64_t>(motor_->position()));
        } else {
            i.write(binding_.feedback, target.value());
        }
    }
    auto mode = i.read(binding_.mode);
    if (mode) i.write(binding_.mode_display, mode.value());
}
}  // namespace canopen::cia402
