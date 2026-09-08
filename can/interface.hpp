#pragma once

#include "can/error.hpp"
#include "can/event.hpp"
#include "can/frame.hpp"
#include "can/timestamp.hpp"

namespace can {

class Interface {
public:
    virtual ~Interface() = default;

    virtual Result<void> send(const Frame& frame) noexcept = 0;
    virtual Result<bool> receive(Frame& frame, RxInfo& info) noexcept = 0;
    virtual bool try_pop_event(Event& event) noexcept = 0;
};

}  // namespace can
