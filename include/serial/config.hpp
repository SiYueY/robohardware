#pragma once

#include <chrono>
#include <cstdint>

namespace serial {

/**
 * @brief Number of data bits in each serial character.
 */
enum class DataBits : std::uint8_t {
    Five = 5,   ///< Five data bits.
    Six = 6,    ///< Six data bits.
    Seven = 7,  ///< Seven data bits.
    Eight = 8,  ///< Eight data bits.
};

/**
 * @brief Parity mode applied to serial characters.
 */
enum class Parity : std::uint8_t {
    None,   ///< Do not generate or check parity.
    Odd,    ///< Use odd parity.
    Even,   ///< Use even parity.
    Mark,   ///< Use mark parity when supported by the platform and driver.
    Space,  ///< Use space parity when supported by the platform and driver.
};

/**
 * @brief Number of stop bits following each serial character.
 */
enum class StopBits : std::uint8_t {
    One = 1,  ///< One stop bit.
    Two = 2,  ///< Two stop bits.
};

/**
 * @brief Serial flow-control mode.
 */
enum class FlowControl : std::uint8_t {
    None,     ///< Disable flow control.
    XonXoff,  ///< Use software XON/XOFF flow control.
    RtsCts,   ///< Use hardware RTS/CTS flow control when supported.
};

/**
 * @brief Configuration applied when opening a Serial Port.
 *
 * Port::open() validates and transactionally applies this configuration. A
 * successfully opened Port retains the applied configuration until it is
 * closed.
 */
struct Config final {
    /**
     * @brief RS-485 direction-control configuration.
     *
     * These settings are requested only when enabled is true. Driver support
     * for individual options is validated by Port::open().
     */
    struct RS485 final {
        bool enabled{false};                  ///< Enable RS-485 mode.
        bool rts_on_send{true};               ///< Assert RTS while transmitting.
        bool rts_after_send{false};           ///< Keep RTS asserted after transmission.
        bool receive_during_transmit{false};  ///< Request receive capability during transmission.
        std::chrono::milliseconds delay_before_send{0};  ///< Delay before transmission.
        std::chrono::milliseconds delay_after_send{0};   ///< Delay after transmission.
    };

    std::uint32_t baud_rate{0};                   ///< Baud rate in baud; zero is invalid.
    DataBits data_bits{DataBits::Eight};          ///< Data-bit count.
    Parity parity{Parity::None};                  ///< Parity mode.
    StopBits stop_bits{StopBits::One};            ///< Stop-bit count.
    FlowControl flow_control{FlowControl::None};  ///< Flow-control mode.
    RS485 rs485{};                                ///< RS-485 direction-control settings.
};

}  // namespace serial
