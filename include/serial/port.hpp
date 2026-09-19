#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include <hardware/result.hpp>
#include <serial/config.hpp>
#include <serial/error.hpp>

namespace serial {

class Port final {
public:
    Port() noexcept = default;
    ~Port() noexcept;
    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;
    Port(Port&& other) noexcept;
    Port& operator=(Port&& other) = delete;

    // Opens and transactionally configures a Linux TTY. Configuration is read
    // back before ownership is committed; failed configuration is rolled back
    // on a best-effort basis and leaves the Port closed.
    [[nodiscard]] hardware::Result<void, Error> open(
        const std::string& path, const Config& config) noexcept;

    // Releases the owned descriptor. A closed Port is a successful no-op.
    // The Port becomes closed even if close(2) reports an error.
    [[nodiscard]] hardware::Result<void, Error> close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    // Blocking transfers wait until one positive kernel transfer completes and
    // may return fewer bytes than requested.
    [[nodiscard]] hardware::Result<std::size_t, Error> read(
        std::byte* data, std::size_t size) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> write(
        const std::byte* data, std::size_t size) noexcept;

    // Bounded transfers use one CLOCK_MONOTONIC deadline for the whole
    // operation. A zero timeout performs one immediate readiness check. EINTR
    // and readiness races do not restart the timeout.
    [[nodiscard]] hardware::Result<std::size_t, Error> read(
        std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> write(
        const std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept;

    // Immediate transfers never wait and return WouldBlock when no progress can
    // be made immediately.
    [[nodiscard]] hardware::Result<std::size_t, Error> try_read(
        std::byte* data, std::size_t size) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> try_write(
        const std::byte* data, std::size_t size) noexcept;

    // Observes readiness without consuming or producing data.
    [[nodiscard]] hardware::Result<void, Error> wait_readable(
        std::chrono::nanoseconds timeout) noexcept;
    [[nodiscard]] hardware::Result<void, Error> wait_writable(
        std::chrono::nanoseconds timeout) noexcept;

    // Returns snapshots of the receive and transmit queues.
    [[nodiscard]] hardware::Result<std::size_t, Error> bytes_available() const noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> bytes_pending() const noexcept;

    // Destructively discards queued data. drain() instead waits for previously
    // accepted output to complete transmission.
    [[nodiscard]] hardware::Result<void, Error> discard_input() noexcept;
    [[nodiscard]] hardware::Result<void, Error> discard_output() noexcept;
    [[nodiscard]] hardware::Result<void, Error> discard_buffers() noexcept;
    [[nodiscard]] hardware::Result<void, Error> drain() noexcept;

    // RTS cannot be manually changed while RS-485 or RTS/CTS flow control owns
    // it automatically.
    [[nodiscard]] hardware::Result<void, Error> set_rts(bool asserted) noexcept;
    [[nodiscard]] hardware::Result<bool, Error> rts() const noexcept;
    [[nodiscard]] hardware::Result<void, Error> set_dtr(bool asserted) noexcept;
    [[nodiscard]] hardware::Result<bool, Error> dtr() const noexcept;
    [[nodiscard]] hardware::Result<bool, Error> cts() const noexcept;
    [[nodiscard]] hardware::Result<bool, Error> dsr() const noexcept;
    [[nodiscard]] hardware::Result<bool, Error> ri() const noexcept;
    [[nodiscard]] hardware::Result<bool, Error> dcd() const noexcept;

    // Asserts or clears BREAK without adding an internal timer or delay.
    [[nodiscard]] hardware::Result<void, Error> set_break(bool asserted) noexcept;

private:
    int fd_{-1};
    bool rts_automatic_{false};
    bool exclusive_{false};
};

}  // namespace serial
