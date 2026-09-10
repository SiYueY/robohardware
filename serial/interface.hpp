#pragma once

#include "serial/error.hpp"

#include <cstddef>
#include <cstdint>

namespace serial {

class Interface {
public:
    virtual ~Interface() = default;

    virtual Result<std::size_t> read(std::uint8_t* data, std::size_t size) noexcept = 0;
    virtual Result<std::size_t> write(const std::uint8_t* data, std::size_t size) noexcept = 0;
};

}  // namespace serial
