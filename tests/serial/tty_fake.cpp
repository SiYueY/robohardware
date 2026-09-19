#include "tty_fake.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <utility>

namespace serial::tty {
namespace {

using fake::Operation;
constexpr std::size_t kOperationCount = static_cast<std::size_t>(Operation::Count);

struct Failure final {
    std::size_t occurrence{0};
    int error{0};
};

struct State final {
    std::array<std::size_t, kOperationCount> occurrences{};
    std::array<Failure, kOperationCount> failures{};
    std::array<bool, kOperationCount> ignored_writes{};
    std::vector<Operation> calls;
    std::vector<timespec> monotonic_times;
    std::size_t monotonic_index{0};
    std::vector<timespec> wait_timeouts;
    std::vector<fake::WaitResult> wait_results;
    std::size_t wait_result_index{0};
    std::vector<fake::TransferResult> read_results;
    std::size_t read_result_index{0};
    std::vector<fake::TransferResult> write_results;
    std::size_t write_result_index{0};

    bool terminal{true};
    bool rs485_supported{false};
    int wait_result{1};
    short wait_revents{0};
    ssize_t read_result{1};
    ssize_t write_result{1};
    int input_queue_size{0};
    int output_queue_size{0};
    int modem_lines{0};
    termios attributes{};
    serial_rs485 rs485{};
};

State state;

[[nodiscard]] std::size_t index(Operation operation) noexcept {
    return static_cast<std::size_t>(operation);
}

[[nodiscard]] bool begin(Operation operation) noexcept {
    state.calls.push_back(operation);
    const std::size_t operation_index = index(operation);
    const std::size_t occurrence = ++state.occurrences[operation_index];
    const Failure failure = state.failures[operation_index];
    if (failure.occurrence != 0 && failure.occurrence == occurrence) {
        errno = failure.error;
        return false;
    }
    return true;
}

[[nodiscard]] bool ignored(Operation operation) noexcept {
    return state.ignored_writes[index(operation)];
}

}  // namespace

int open(const char*) noexcept { return begin(Operation::Open) ? 42 : -1; }

int close(int) noexcept { return begin(Operation::Close) ? 0 : -1; }

int is_terminal(int) noexcept {
    if (!begin(Operation::IsTerminal)) return -1;
    return state.terminal ? 1 : 0;
}

int set_exclusive(int) noexcept { return begin(Operation::SetExclusive) ? 0 : -1; }

int clear_exclusive(int) noexcept { return begin(Operation::ClearExclusive) ? 0 : -1; }

int read_attributes(int, termios& attributes) noexcept {
    if (!begin(Operation::ReadAttributes)) return -1;
    attributes = state.attributes;
    return 0;
}

int write_attributes(int, int, const termios& attributes) noexcept {
    if (!begin(Operation::WriteAttributes)) return -1;
    if (!ignored(Operation::WriteAttributes)) state.attributes = attributes;
    return 0;
}

int read_rs485(int, serial_rs485& configuration) noexcept {
    if (!begin(Operation::ReadRs485)) return -1;
    if (!state.rs485_supported) {
        errno = ENOTTY;
        return -1;
    }
    configuration = state.rs485;
    return 0;
}

int write_rs485(int, const serial_rs485& configuration) noexcept {
    if (!begin(Operation::WriteRs485)) return -1;
    if (!state.rs485_supported) {
        errno = ENOTTY;
        return -1;
    }
    if (!ignored(Operation::WriteRs485)) state.rs485 = configuration;
    return 0;
}

int monotonic_now(timespec& value) noexcept {
    if (!begin(Operation::MonotonicNow)) return -1;
    if (state.monotonic_times.empty()) {
        value = {};
        return 0;
    }
    const std::size_t position = std::min(state.monotonic_index, state.monotonic_times.size() - 1);
    value = state.monotonic_times[position];
    if (state.monotonic_index < state.monotonic_times.size()) ++state.monotonic_index;
    return 0;
}

int wait(int, short events, const timespec* timeout, short& revents) noexcept {
    if (timeout != nullptr) state.wait_timeouts.push_back(*timeout);
    if (!begin(Operation::Wait)) return -1;
    if (!state.wait_results.empty()) {
        const std::size_t position =
            std::min(state.wait_result_index, state.wait_results.size() - 1);
        const fake::WaitResult result = state.wait_results[position];
        if (state.wait_result_index < state.wait_results.size()) ++state.wait_result_index;
        revents = result.revents != 0 ? result.revents : events;
        return result.result;
    }
    revents = state.wait_revents != 0 ? state.wait_revents : events;
    return state.wait_result;
}

ssize_t read(int, void*, std::size_t size) noexcept {
    if (!begin(Operation::Read)) return -1;
    fake::TransferResult transfer{state.read_result};
    if (!state.read_results.empty()) {
        const std::size_t position =
            std::min(state.read_result_index, state.read_results.size() - 1);
        transfer = state.read_results[position];
        if (state.read_result_index < state.read_results.size()) ++state.read_result_index;
    }
    if (transfer.result < 0) {
        if (transfer.native_error != 0) errno = transfer.native_error;
        return transfer.result;
    }
    return std::min<ssize_t>(transfer.result, static_cast<ssize_t>(size));
}

ssize_t write(int, const void*, std::size_t size) noexcept {
    if (!begin(Operation::Write)) return -1;
    fake::TransferResult transfer{state.write_result};
    if (!state.write_results.empty()) {
        const std::size_t position =
            std::min(state.write_result_index, state.write_results.size() - 1);
        transfer = state.write_results[position];
        if (state.write_result_index < state.write_results.size()) ++state.write_result_index;
    }
    if (transfer.result < 0) {
        if (transfer.native_error != 0) errno = transfer.native_error;
        return transfer.result;
    }
    return std::min<ssize_t>(transfer.result, static_cast<ssize_t>(size));
}

int input_queue_size(int, int& size) noexcept {
    if (!begin(Operation::InputQueueSize)) return -1;
    size = state.input_queue_size;
    return 0;
}

int output_queue_size(int, int& size) noexcept {
    if (!begin(Operation::OutputQueueSize)) return -1;
    size = state.output_queue_size;
    return 0;
}

int discard(int, int) noexcept { return begin(Operation::Discard) ? 0 : -1; }

int drain(int) noexcept { return begin(Operation::Drain) ? 0 : -1; }

int read_modem_lines(int, int& lines) noexcept {
    if (!begin(Operation::ReadModemLines)) return -1;
    lines = state.modem_lines;
    return 0;
}

int write_modem_line(int, int bits, bool asserted) noexcept {
    if (!begin(Operation::WriteModemLine)) return -1;
    if (asserted) {
        state.modem_lines |= bits;
    } else {
        state.modem_lines &= ~bits;
    }
    return 0;
}

int write_break(int, bool) noexcept { return begin(Operation::WriteBreak) ? 0 : -1; }

namespace fake {

void reset() noexcept { state = State{}; }

void fail(Operation operation, std::size_t occurrence, int native_error) noexcept {
    state.failures[index(operation)] = {occurrence, native_error};
}

void ignore_write(Operation operation) noexcept { state.ignored_writes[index(operation)] = true; }

void set_terminal(bool terminal) noexcept { state.terminal = terminal; }

void set_rs485_supported(bool supported) noexcept { state.rs485_supported = supported; }

void set_wait_result(int result, short revents) noexcept {
    state.wait_result = result;
    state.wait_revents = revents;
}

void set_wait_results(std::vector<WaitResult> values) {
    state.wait_results = std::move(values);
    state.wait_result_index = 0;
}

void set_read_result(ssize_t result) noexcept { state.read_result = result; }

void set_read_results(std::vector<TransferResult> values) {
    state.read_results = std::move(values);
    state.read_result_index = 0;
}

void set_write_result(ssize_t result) noexcept { state.write_result = result; }

void set_write_results(std::vector<TransferResult> values) {
    state.write_results = std::move(values);
    state.write_result_index = 0;
}

void set_monotonic_times(std::vector<timespec> values) {
    state.monotonic_times = std::move(values);
    state.monotonic_index = 0;
}

void set_input_queue_size(int value) noexcept { state.input_queue_size = value; }

void set_output_queue_size(int value) noexcept { state.output_queue_size = value; }

void set_modem_lines(int lines) noexcept { state.modem_lines = lines; }

const std::vector<Operation>& calls() noexcept { return state.calls; }

const std::vector<timespec>& wait_timeouts() noexcept { return state.wait_timeouts; }

}  // namespace fake
}  // namespace serial::tty
