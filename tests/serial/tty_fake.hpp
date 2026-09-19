#pragma once

#include "tty.hpp"

#include <cstddef>
#include <ctime>
#include <vector>

namespace serial::tty::fake {

enum class Operation : std::size_t {
    Open,
    Close,
    IsTerminal,
    SetExclusive,
    ClearExclusive,
    ReadAttributes,
    WriteAttributes,
    ReadRs485,
    WriteRs485,
    MonotonicNow,
    Wait,
    Read,
    Write,
    Discard,
    ReadInputQueueSize,
    ReadOutputQueueSize,
    Drain,
    ReadModemLines,
    WriteModemLine,
    WriteBreak,
    Count,
};

struct WaitResult final {
    int result;
    short revents{0};
};

struct TransferResult final {
    ssize_t result;
    int native_error{0};
};

void reset() noexcept;
void fail(Operation operation, std::size_t occurrence, int native_error) noexcept;
void ignore_write(Operation operation) noexcept;
void set_terminal(bool terminal) noexcept;
void set_rs485_supported(bool supported) noexcept;
void set_wait_result(int result, short revents = 0) noexcept;
void set_wait_results(std::vector<WaitResult> values);
void set_read_result(ssize_t result) noexcept;
void set_read_results(std::vector<TransferResult> values);
void set_write_result(ssize_t result) noexcept;
void set_write_results(std::vector<TransferResult> values);
void set_monotonic_times(std::vector<timespec> values);
void set_input_queue_size(int value) noexcept;
void set_output_queue_size(int value) noexcept;
void set_modem_lines(int lines) noexcept;

[[nodiscard]] const std::vector<Operation>& calls() noexcept;
[[nodiscard]] const std::vector<timespec>& wait_timeouts() noexcept;

}  // namespace serial::tty::fake
