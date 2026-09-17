#pragma once

#include <cstddef>
#include <chrono>
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

    [[nodiscard]] hardware::Result<void, Error> open(
        const std::string& path, const Config& config) noexcept;
    [[nodiscard]] hardware::Result<void, Error> close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> read(
        std::byte* data, std::size_t size) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> read(
        std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> try_read(
        std::byte* data, std::size_t size) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> write(
        const std::byte* data, std::size_t size) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> write(
        const std::byte* data, std::size_t size, std::chrono::nanoseconds timeout) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> try_write(
        const std::byte* data, std::size_t size) noexcept;
    [[nodiscard]] hardware::Result<void, Error> wait_readable(
        std::chrono::nanoseconds timeout) noexcept;
    [[nodiscard]] hardware::Result<void, Error> wait_writable(
        std::chrono::nanoseconds timeout) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> bytes_available() const noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> bytes_pending() const noexcept;
    [[nodiscard]] hardware::Result<void, Error> discard_input() noexcept;
    [[nodiscard]] hardware::Result<void, Error> discard_output() noexcept;
    [[nodiscard]] hardware::Result<void, Error> discard_buffers() noexcept;
    [[nodiscard]] hardware::Result<void, Error> drain() noexcept;
    [[nodiscard]] hardware::Result<void, Error> set_rts(bool asserted) noexcept;
    [[nodiscard]] hardware::Result<bool, Error> rts() const noexcept;
    [[nodiscard]] hardware::Result<void, Error> set_dtr(bool asserted) noexcept;
    [[nodiscard]] hardware::Result<bool, Error> dtr() const noexcept;
    [[nodiscard]] hardware::Result<bool, Error> cts() const noexcept;
    [[nodiscard]] hardware::Result<bool, Error> dsr() const noexcept;
    [[nodiscard]] hardware::Result<bool, Error> ri() const noexcept;
    [[nodiscard]] hardware::Result<bool, Error> dcd() const noexcept;
    [[nodiscard]] hardware::Result<void, Error> set_break(bool asserted) noexcept;

private:
    int fd_{-1};
    bool rts_automatic_{false};
};

}  // namespace serial
