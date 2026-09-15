#include "spidev_fake.hpp"

#include <array>
#include <cerrno>
#include <limits>
#include <optional>

namespace spi::spidev::fake {
namespace {

constexpr std::size_t kOperationCount = 9;

struct Failure final {
    Operation operation;
    std::size_t occurrence;
    int native_error;
};

struct State final {
    Config config{0, 8, 500'000};
    std::array<std::size_t, kOperationCount> occurrences{};
    std::vector<Failure> failures;
    std::vector<Operation> ignored_writes;
    std::vector<Operation> call_log;
    std::optional<int> message_result;
};

State& state() noexcept {
    static State value;
    return value;
}

[[nodiscard]] std::size_t index(Operation operation) noexcept {
    return static_cast<std::size_t>(operation);
}

[[nodiscard]] bool ignored(Operation operation) noexcept {
    for (const Operation ignored_operation : state().ignored_writes) {
        if (ignored_operation == operation) return true;
    }
    return false;
}

[[nodiscard]] bool begin(Operation operation) noexcept {
    State& value = state();
    value.call_log.push_back(operation);
    const std::size_t occurrence = ++value.occurrences[index(operation)];
    for (const Failure& failure : value.failures) {
        if (failure.operation == operation && failure.occurrence == occurrence) {
            errno = failure.native_error;
            return false;
        }
    }
    return true;
}

}  // namespace

void reset() noexcept { state() = {}; }

void fail(Operation operation, std::size_t occurrence, int native_error) noexcept {
    state().failures.push_back({operation, occurrence, native_error});
}

void ignore_write(Operation operation) noexcept { state().ignored_writes.push_back(operation); }

void set_message_result(int result) noexcept { state().message_result = result; }

const std::vector<Operation>& calls() noexcept { return state().call_log; }

}  // namespace spi::spidev::fake

namespace spi::spidev {

int open(const char* path) noexcept {
    static_cast<void>(path);
    return fake::begin(fake::Operation::Open) ? 42 : -1;
}

int close(int fd) noexcept {
    static_cast<void>(fd);
    return fake::begin(fake::Operation::Close) ? 0 : -1;
}

int read_mode(int fd, std::uint32_t& mode) noexcept {
    static_cast<void>(fd);
    if (!fake::begin(fake::Operation::ReadMode)) return -1;
    mode = fake::state().config.mode;
    return 0;
}

int write_mode(int fd, std::uint32_t mode) noexcept {
    static_cast<void>(fd);
    if (!fake::begin(fake::Operation::WriteMode)) return -1;
    if (!fake::ignored(fake::Operation::WriteMode)) fake::state().config.mode = mode;
    return 0;
}

int read_bits_per_word(int fd, std::uint8_t& bits_per_word) noexcept {
    static_cast<void>(fd);
    if (!fake::begin(fake::Operation::ReadBitsPerWord)) return -1;
    bits_per_word = fake::state().config.bits_per_word;
    return 0;
}

int write_bits_per_word(int fd, std::uint8_t bits_per_word) noexcept {
    static_cast<void>(fd);
    if (!fake::begin(fake::Operation::WriteBitsPerWord)) return -1;
    if (!fake::ignored(fake::Operation::WriteBitsPerWord)) {
        fake::state().config.bits_per_word = bits_per_word;
    }
    return 0;
}

int read_max_speed(int fd, std::uint32_t& max_speed) noexcept {
    static_cast<void>(fd);
    if (!fake::begin(fake::Operation::ReadMaxSpeed)) return -1;
    max_speed = fake::state().config.max_speed;
    return 0;
}

int write_max_speed(int fd, std::uint32_t max_speed) noexcept {
    static_cast<void>(fd);
    if (!fake::begin(fake::Operation::WriteMaxSpeed)) return -1;
    if (!fake::ignored(fake::Operation::WriteMaxSpeed)) fake::state().config.max_speed = max_speed;
    return 0;
}

int message(int fd, spi_ioc_transfer* transfers, std::size_t count) noexcept {
    static_cast<void>(fd);
    if (!fake::begin(fake::Operation::Message)) return -1;
    if (fake::state().message_result) return *fake::state().message_result;

    std::size_t transferred = 0;
    for (std::size_t index = 0; index < count; ++index) transferred += transfers[index].len;
    return transferred > static_cast<std::size_t>(std::numeric_limits<int>::max())
               ? std::numeric_limits<int>::max()
               : static_cast<int>(transferred);
}

}  // namespace spi::spidev
