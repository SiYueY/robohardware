#pragma once

#include <cstddef>
#include <string>
#include <hardware/result.hpp>

#include <spi/config.hpp>
#include <spi/error.hpp>

namespace spi {

class Device final {
public:
    Device() noexcept = default;
    ~Device() noexcept;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) = delete;

    [[nodiscard]] hardware::Result<void, Error> open(
        const std::string& path, const Config& config) noexcept;
    [[nodiscard]] hardware::Result<void, Error> close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] hardware::Result<void, Error> transfer(
        const std::byte* tx, std::byte* rx, std::size_t size) noexcept;

private:
    int fd_{-1};
};

}  // namespace spi
