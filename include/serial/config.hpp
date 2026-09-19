#pragma once

#include <chrono>
#include <cstdint>

namespace serial {

enum class DataBits : std::uint8_t { Five = 5, Six = 6, Seven = 7, Eight = 8 };
enum class Parity : std::uint8_t { None, Odd, Even, Mark, Space };
enum class StopBits : std::uint8_t { One = 1, Two = 2 };
enum class FlowControl : std::uint8_t { None, XonXoff, RtsCts };

struct Config final {
    struct RS485 final {
        bool enabled{false};
        bool rts_on_send{true};
        bool rts_after_send{false};
        bool receive_during_transmit{false};
        std::chrono::milliseconds delay_before_send{0};
        std::chrono::milliseconds delay_after_send{0};
    };

    // Unit: baud.
    std::uint32_t baud_rate{0};
    DataBits data_bits{DataBits::Eight};
    Parity parity{Parity::None};
    StopBits stop_bits{StopBits::One};
    FlowControl flow_control{FlowControl::None};
    RS485 rs485{};
};

}  // namespace serial
