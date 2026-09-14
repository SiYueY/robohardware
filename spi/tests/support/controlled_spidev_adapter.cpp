#include "controlled_spidev_adapter.hpp"

#include "spidev_adapter.hpp"

#include <array>

namespace {

using spi::spidev_adapter::Result;
using spi::test::Operation;

constexpr std::size_t kOperationCount = 9;
constexpr std::size_t kMaxScheduledCalls = 4;

constexpr std::size_t index(Operation operation) noexcept {
  return static_cast<std::size_t>(operation);
}

struct State final {
  std::array<Result, kOperationCount> results{};
  std::array<std::array<Result, kMaxScheduledCalls>, kOperationCount> scheduled_results{};
  std::array<std::array<bool, kMaxScheduledCalls>, kOperationCount> has_scheduled_result{};
  std::array<std::size_t, kOperationCount> operation_calls{};
  std::array<Operation, 64> operations{};
  std::size_t operations_size{0};
  std::uint32_t mode{0};
  std::uint8_t bits_per_word{8};
  std::uint32_t max_speed_hz{1'000'000};
  bool ignore_mode{false};
  bool ignore_bits_per_word{false};
  bool ignore_max_speed_hz{false};
  int close_calls{0};
  int fd{11};
  int flags{0};
  spi_ioc_transfer transferred{};
} state;

void record(Operation operation) noexcept {
  state.operations[state.operations_size++] = operation;
  ++state.operation_calls[index(operation)];
}

Result result(Operation operation) noexcept {
  const auto operation_index = index(operation);
  const auto call_index = state.operation_calls[operation_index] - 1;
  if (call_index < kMaxScheduledCalls && state.has_scheduled_result[operation_index][call_index]) {
    return state.scheduled_results[operation_index][call_index];
  }
  return state.results[index(operation)];
}

}  // namespace

namespace spi::test {

void reset_adapter() noexcept {
  state = State{};
  for (auto& result : state.results) result = {0, 0};
  state.results[index(Operation::Open)] = {11, 0};
  state.results[index(Operation::Transfer)] = {0, 0};
}

void set_result(Operation operation, int value, int error) noexcept {
  state.results[index(operation)] = {value, error};
}
void set_result_on_call(Operation operation, std::size_t call_number, int value, int error) noexcept {
  if (call_number == 0 || call_number > kMaxScheduledCalls) return;
  const auto operation_index = index(operation);
  state.scheduled_results[operation_index][call_number - 1] = {value, error};
  state.has_scheduled_result[operation_index][call_number - 1] = true;
}
void set_mode(std::uint32_t value) noexcept { state.mode = value; }
void set_bits_per_word(std::uint8_t value) noexcept { state.bits_per_word = value; }
void set_max_speed_hz(std::uint32_t value) noexcept { state.max_speed_hz = value; }
void ignore_mode_writes() noexcept { state.ignore_mode = true; }
void ignore_bits_per_word_writes() noexcept { state.ignore_bits_per_word = true; }
void ignore_max_speed_hz_writes() noexcept { state.ignore_max_speed_hz = true; }
std::size_t operation_count() noexcept { return state.operations_size; }
Operation operation_at(std::size_t index_value) noexcept { return state.operations[index_value]; }
int close_count() noexcept { return state.close_calls; }
int opened_fd() noexcept { return state.fd; }
int open_flags() noexcept { return state.flags; }
std::uint32_t current_mode() noexcept { return state.mode; }
std::uint8_t current_bits_per_word() noexcept { return state.bits_per_word; }
std::uint32_t current_max_speed_hz() noexcept { return state.max_speed_hz; }
const spi_ioc_transfer& last_transfer() noexcept { return state.transferred; }

}  // namespace spi::test

namespace spi::spidev_adapter {

Result open_path(const char*, int flags) noexcept {
  record(Operation::Open);
  state.flags = flags;
  return result(Operation::Open);
}
Result close_fd(int) noexcept {
  record(Operation::Close);
  ++state.close_calls;
  return result(Operation::Close);
}
Result read_mode32(int, std::uint32_t& value) noexcept {
  record(Operation::ReadMode);
  value = state.mode;
  return result(Operation::ReadMode);
}
Result write_mode32(int, std::uint32_t value) noexcept {
  record(Operation::WriteMode);
  if (!state.ignore_mode && result(Operation::WriteMode).value >= 0) state.mode = value;
  return result(Operation::WriteMode);
}
Result read_bits_per_word(int, std::uint8_t& value) noexcept {
  record(Operation::ReadBits);
  value = state.bits_per_word;
  return result(Operation::ReadBits);
}
Result write_bits_per_word(int, std::uint8_t value) noexcept {
  record(Operation::WriteBits);
  if (!state.ignore_bits_per_word && result(Operation::WriteBits).value >= 0) {
    state.bits_per_word = value;
  }
  return result(Operation::WriteBits);
}
Result read_max_speed_hz(int, std::uint32_t& value) noexcept {
  record(Operation::ReadSpeed);
  value = state.max_speed_hz;
  return result(Operation::ReadSpeed);
}
Result write_max_speed_hz(int, std::uint32_t value) noexcept {
  record(Operation::WriteSpeed);
  if (!state.ignore_max_speed_hz && result(Operation::WriteSpeed).value >= 0) {
    state.max_speed_hz = value;
  }
  return result(Operation::WriteSpeed);
}
Result transfer(int, spi_ioc_transfer& value) noexcept {
  record(Operation::Transfer);
  state.transferred = value;
  return result(Operation::Transfer);
}

}  // namespace spi::spidev_adapter
