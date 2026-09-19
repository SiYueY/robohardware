#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include <hardware/result.hpp>
#include <serial/config.hpp>
#include <serial/error.hpp>

namespace serial {

/**
 * @brief Uniquely owns and operates a configured Linux serial TTY.
 *
 * A Port is move-constructible but neither copyable nor assignable. A
 * successful open freezes its configuration until close(). The class is not
 * thread-safe; callers must externally synchronize shared access.
 */
class Port final {
public:
    /**
     * @brief Creates a closed Port.
     */
    Port() noexcept = default;

    /**
     * @brief Closes an owned TTY if necessary.
     *
     * Destruction ignores a close error after committing the Port to its
     * closed state.
     */
    ~Port() noexcept;

    /** @brief Copy construction is unsupported because a Port uniquely owns its TTY. */
    Port(const Port&) = delete;
    /** @brief Copy assignment is unsupported because a Port uniquely owns its TTY. */
    Port& operator=(const Port&) = delete;

    /**
     * @brief Transfers TTY ownership from another Port.
     *
     * @param other Port whose ownership is transferred.
     * @post other is closed.
     */
    Port(Port&& other) noexcept;

    /** @brief Move assignment is intentionally unsupported. */
    Port& operator=(Port&& other) = delete;

    /**
     * @brief Opens and configures a Linux serial TTY.
     *
     * Configuration is applied transactionally and read back before ownership
     * is committed. On failure, the prior device configuration is restored on
     * a best-effort basis and this Port remains closed. A successful open
     * requests Linux TIOCEXCL ownership, which prevents subsequent ordinary
     * opens but cannot exclude an already-concurrent opener or privileged
     * access.
     *
     * @param path Path to the TTY device.
     * @param config Configuration to validate and apply.
     * @return Success when the Port is configured and owns the TTY; otherwise
     *         a Serial error.
     * @pre This Port is closed.
     */
    [[nodiscard]] hardware::Result<void, Error> open(
        const std::string& path, const Config& config) noexcept;

    /**
     * @brief Releases the owned TTY.
     *
     * Calling close() on a closed Port is a successful no-op. Once closing
     * starts, the Port commits to the closed state even if the native close
     * operation reports an error.
     *
     * @return Success when no close operation fails; otherwise a Serial error
     *         after the Port has become closed.
     */
    [[nodiscard]] hardware::Result<void, Error> close() noexcept;

    /**
     * @brief Reports whether this Port owns an open TTY.
     *
     * @return true when the Port is open; otherwise false.
     */
    [[nodiscard]] bool is_open() const noexcept;

    /**
     * @brief Reads one positive transfer from the TTY, waiting if needed.
     *
     * This operation may wait indefinitely. The first positive native
     * transfer succeeds and may contain fewer bytes than requested.
     *
     * @param data Destination buffer, or null when size is zero.
     * @param size Maximum number of bytes to read.
     * @return The bytes read, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<std::size_t, Error> read(
        std::byte* data, std::size_t size) noexcept;

    /**
     * @brief Writes one positive transfer to the TTY, waiting if needed.
     *
     * This operation may wait indefinitely. The first positive native
     * transfer succeeds and may be partial. Success(n) means the kernel or
     * TTY driver accepted n bytes; use drain() to wait for transmission.
     *
     * @param data Source buffer, or null when size is zero.
     * @param size Maximum number of bytes to write.
     * @return The bytes accepted, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<std::size_t, Error> write(
        const std::byte* data, std::size_t size) noexcept;

    /**
     * @brief Reads one positive transfer before a monotonic deadline.
     *
     * One CLOCK_MONOTONIC deadline covers the entire operation. A zero timeout
     * performs exactly one immediate readiness check; a negative timeout is
     * invalid. Interrupts and tolerated readiness races do not restart the
     * timeout.
     *
     * @param data Destination buffer, or null when size is zero.
     * @param size Maximum number of bytes to read.
     * @param timeout Total time allowed for readiness and transfer.
     * @return The bytes read, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<std::size_t, Error> read(
        std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept;

    /**
     * @brief Writes one positive transfer before a monotonic deadline.
     *
     * One CLOCK_MONOTONIC deadline covers the entire operation. A zero timeout
     * performs exactly one immediate readiness check; a negative timeout is
     * invalid. Success(n) means the kernel or TTY driver accepted n bytes, not
     * that physical transmission is complete.
     *
     * @param data Source buffer, or null when size is zero.
     * @param size Maximum number of bytes to write.
     * @param timeout Total time allowed for readiness and transfer.
     * @return The bytes accepted, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<std::size_t, Error> write(
        const std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept;

    /**
     * @brief Attempts to read without waiting.
     *
     * @param data Destination buffer, or null when size is zero.
     * @param size Maximum number of bytes to read.
     * @return The bytes read, Error::WouldBlock when no immediate progress is
     *         possible, or another Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<std::size_t, Error> try_read(
        std::byte* data, std::size_t size) noexcept;

    /**
     * @brief Attempts to write without waiting.
     *
     * @param data Source buffer, or null when size is zero.
     * @param size Maximum number of bytes to write.
     * @return The bytes accepted, Error::WouldBlock when no immediate progress
     *         is possible, or another Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<std::size_t, Error> try_write(
        const std::byte* data, std::size_t size) noexcept;

    /**
     * @brief Waits only for input readiness.
     *
     * This operation does not consume input. A negative timeout is invalid;
     * zero performs one immediate readiness check.
     *
     * @param timeout Maximum time to wait.
     * @return Success on input readiness, Error::TimedOut when not ready by
     *         the deadline, or another Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> wait_readable(
        std::chrono::nanoseconds timeout) noexcept;

    /**
     * @brief Waits only for output readiness.
     *
     * This operation does not write output. A negative timeout is invalid;
     * zero performs one immediate readiness check.
     *
     * @param timeout Maximum time to wait.
     * @return Success on output readiness, Error::TimedOut when not ready by
     *         the deadline, or another Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> wait_writable(
        std::chrono::nanoseconds timeout) noexcept;

    /**
     * @brief Returns a snapshot of bytes currently queued for input.
     *
     * @return Input-queue size, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<std::size_t, Error> bytes_available() const noexcept;

    /**
     * @brief Returns a snapshot of bytes currently queued for output.
     *
     * @return Output-queue size, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<std::size_t, Error> bytes_pending() const noexcept;

    /**
     * @brief Destructively discards queued input.
     *
     * @return Success, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> discard_input() noexcept;

    /**
     * @brief Destructively discards queued output.
     *
     * @return Success, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> discard_output() noexcept;

    /**
     * @brief Destructively discards queued input and output.
     *
     * @return Success, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> discard_buffers() noexcept;

    /**
     * @brief Waits for previously accepted output to complete transmission.
     *
     * This is a non-real-time operation and may block indefinitely.
     *
     * @return Success, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> drain() noexcept;

    /**
     * @brief Sets the RTS output state.
     *
     * @param asserted true to assert RTS; false to clear it.
     * @return Success, Error::InvalidState when RS-485 or RTS/CTS flow control
     *         automatically owns RTS, or another Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> set_rts(bool asserted) noexcept;

    /**
     * @brief Reads the current RTS output state.
     *
     * @return true when RTS is asserted, false when cleared, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<bool, Error> rts() const noexcept;

    /**
     * @brief Sets the DTR output state.
     *
     * @param asserted true to assert DTR; false to clear it.
     * @return Success, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> set_dtr(bool asserted) noexcept;

    /**
     * @brief Reads the current DTR output state.
     *
     * @return true when DTR is asserted, false when cleared, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<bool, Error> dtr() const noexcept;

    /**
     * @brief Reads the current CTS input state.
     *
     * @return true when CTS is asserted, false when cleared, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<bool, Error> cts() const noexcept;

    /**
     * @brief Reads the current DSR input state.
     *
     * @return true when DSR is asserted, false when cleared, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<bool, Error> dsr() const noexcept;

    /**
     * @brief Reads the current RI input state.
     *
     * @return true when RI is asserted, false when cleared, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<bool, Error> ri() const noexcept;

    /**
     * @brief Reads the current DCD input state.
     *
     * @return true when DCD is asserted, false when cleared, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<bool, Error> dcd() const noexcept;

    /**
     * @brief Asserts or clears BREAK without adding an internal delay.
     *
     * @param asserted true to assert BREAK; false to clear it.
     * @return Success, or a Serial error.
     * @pre The Port is open.
     */
    [[nodiscard]] hardware::Result<void, Error> set_break(bool asserted) noexcept;

private:
    int fd_{-1};
    bool rts_automatic_{false};
};

}  // namespace serial
