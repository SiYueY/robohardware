#pragma once

#include <cstddef>
#include <string>
#include <hardware/result.hpp>

#include <serial/configuration.hpp>
#include <serial/error.hpp>
#include <serial/timeout.hpp>

namespace serial {

enum class FlushDirection { Input, Output, Both };

class Port final {
public:
    Port() noexcept;
    ~Port() noexcept;
    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;
    Port(Port&& other) noexcept;
    Port& operator=(Port&& other) = delete;

    [[nodiscard]] hardware::Result<void, Error> open(
        const std::string& path, const PortConfig& config) noexcept;
    [[nodiscard]] hardware::Result<void, Error> close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> read(
        std::byte* data, std::size_t capacity, Timeout timeout) noexcept;
    [[nodiscard]] hardware::Result<std::size_t, Error> write(
        const std::byte* data, std::size_t size, Timeout timeout) noexcept;
    [[nodiscard]] hardware::Result<void, Error> flush(FlushDirection direction) noexcept;
    [[nodiscard]] hardware::Result<void, Error> drain(Timeout timeout) noexcept;

private:
    int fd_;
};

}  // namespace serial
