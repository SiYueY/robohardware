#include <serial/configuration.hpp>

namespace serial {

Rs485Config::Rs485Config(
    bool enabled,
    RtsLevel rts_during_send,
    RtsLevel rts_after_send,
    std::chrono::milliseconds delay_before_send,
    std::chrono::milliseconds delay_after_send) noexcept
    : enabled_(enabled),
      rts_during_send_(rts_during_send),
      rts_after_send_(rts_after_send),
      delay_before_send_(delay_before_send),
      delay_after_send_(delay_after_send) {}

Rs485Config Rs485Config::disabled() noexcept {
  return Rs485Config{
      false,
      RtsLevel::Asserted,
      RtsLevel::Deasserted,
      std::chrono::milliseconds::zero(),
      std::chrono::milliseconds::zero()};
}

Rs485Config Rs485Config::enabled(
    RtsLevel rts_during_send,
    RtsLevel rts_after_send,
    std::chrono::milliseconds delay_before_send,
    std::chrono::milliseconds delay_after_send) noexcept {
  return Rs485Config{
      true, rts_during_send, rts_after_send, delay_before_send, delay_after_send};
}

PortConfig::PortConfig(
    std::uint32_t baud_rate,
    DataBits data_bits,
    Parity parity,
    StopBits stop_bits,
    FlowControl flow_control,
    Rs485Config rs485) noexcept
    : baud_rate_(baud_rate),
      data_bits_(data_bits),
      parity_(parity),
      stop_bits_(stop_bits),
      flow_control_(flow_control),
      rs485_(rs485) {}

}  // namespace serial
