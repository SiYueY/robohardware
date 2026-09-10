#pragma once

#include "serial/interface.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace serial {

enum class DataBits : std::uint8_t { Five = 5, Six = 6, Seven = 7, Eight = 8 };
enum class Parity : std::uint8_t { None, Odd, Even };
enum class StopBits : std::uint8_t { One, Two };
enum class FlowControl : std::uint8_t { None, Software, Hardware };

struct Config {
    std::string device;
    std::uint32_t baud_rate{115200};
    DataBits data_bits{DataBits::Eight};
    Parity parity{Parity::None};
    StopBits stop_bits{StopBits::One};
    FlowControl flow_control{FlowControl::None};
};

struct Signals {
    bool cts{false};
    bool dsr{false};
    bool dcd{false};
    bool ri{false};
};

struct Stats {
    std::uint64_t rx_bytes{0};
    std::uint64_t tx_bytes{0};
    std::uint64_t read_calls{0};
    std::uint64_t write_calls{0};
    std::uint64_t read_errors{0};
    std::uint64_t write_errors{0};
};

class Port final : public Interface {
public:
    static Result<Port> open(Config config);

    ~Port();
    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;
    Port(Port&& other) noexcept;
    Port& operator=(Port&& other) noexcept;

    /// Performs one non-blocking read attempt; zero means no byte is available.
    Result<std::size_t> read(std::uint8_t* data, std::size_t size) noexcept override;
    /// Performs one non-blocking write attempt; partial writes are successful.
    Result<std::size_t> write(const std::uint8_t* data, std::size_t size) noexcept override;

    Result<bool> wait_readable(std::chrono::milliseconds timeout) noexcept;
    Result<bool> wait_writable(std::chrono::milliseconds timeout) noexcept;
    Result<std::size_t> available() const noexcept;
    Result<std::size_t> pending_write() const noexcept;
    Result<void> flush_input() noexcept;
    Result<void> flush_output() noexcept;
    /// Blocks until the driver has transmitted queued output. Do not call on an RT path.
    Result<void> drain() noexcept;
    Result<void> set_break(bool enabled) noexcept;
    Result<void> set_rts(bool enabled) noexcept;
    Result<void> set_dtr(bool enabled) noexcept;
    Result<Signals> signals() const noexcept;
    const Config& config() const noexcept;
    Stats stats() const noexcept;
    int fd() const noexcept;

private:
    Port(int fd, Config config) noexcept;
    void close() noexcept;

    int fd_{-1};
    Config config_;
    std::atomic<std::uint64_t> rx_bytes_{0};
    std::atomic<std::uint64_t> tx_bytes_{0};
    std::atomic<std::uint64_t> read_calls_{0};
    std::atomic<std::uint64_t> write_calls_{0};
    std::atomic<std::uint64_t> read_errors_{0};
    std::atomic<std::uint64_t> write_errors_{0};
};

}  // namespace serial
