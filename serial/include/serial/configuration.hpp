#pragma once

#include <chrono>
#include <cstdint>

namespace serial {

enum class DataBits { Five, Six, Seven, Eight };
enum class Parity { None, Even, Odd };
enum class StopBits { One, Two };
enum class FlowControl { None, Software, Hardware };
enum class RtsLevel { Asserted, Deasserted };

class Rs485Config final {
 public:
  Rs485Config() = delete;

  [[nodiscard]] static Rs485Config disabled() noexcept;
  [[nodiscard]] static Rs485Config enabled(
      RtsLevel rts_during_send,
      RtsLevel rts_after_send,
      std::chrono::milliseconds delay_before_send,
      std::chrono::milliseconds delay_after_send) noexcept;

 private:
  Rs485Config(
      bool enabled,
      RtsLevel rts_during_send,
      RtsLevel rts_after_send,
      std::chrono::milliseconds delay_before_send,
      std::chrono::milliseconds delay_after_send) noexcept;

  bool enabled_;
  RtsLevel rts_during_send_;
  RtsLevel rts_after_send_;
  std::chrono::milliseconds delay_before_send_;
  std::chrono::milliseconds delay_after_send_;

  friend class Port;
  friend struct PortAccess;
};

class PortConfig final {
 public:
  PortConfig(
      std::uint32_t baud_rate,
      DataBits data_bits,
      Parity parity,
      StopBits stop_bits,
      FlowControl flow_control,
      Rs485Config rs485) noexcept;

 private:
  std::uint32_t baud_rate_;
  DataBits data_bits_;
  Parity parity_;
  StopBits stop_bits_;
  FlowControl flow_control_;
  Rs485Config rs485_;

  friend class Port;
  friend struct PortAccess;
};

}  // namespace serial
