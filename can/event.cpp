#include "can/native.hpp"

namespace can::native {

State apply_event(State state, const Event& event) noexcept {
    switch (event.type) {
        case EventType::Warning:
            return State::Warning;
        case EventType::Passive:
            return State::Passive;
        case EventType::BusOff:
            return State::BusOff;
        case EventType::Restarted:
            return State::Active;
        default:
            return state;
    }
}

void update_stats(Stats& stats, const Event& event) noexcept {
    ++stats.error_frames;
    if (event.type == EventType::RxOverflow) ++stats.rx_overruns;
}

}  // namespace can::native
