#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <hardware/result.hpp>
#include <serial/error.hpp>

namespace serial {

struct PortInfo final {
    struct USB final {
        bool available{false};
        std::uint16_t vendor_id{0};
        std::uint16_t product_id{0};
        std::string serial_number;
        std::string manufacturer;
        std::string product;
    };

    std::string path;
    std::string description;
    USB usb{};
};

// Enumerates Linux TTY devices. No ports is a successful empty result. Missing
// per-device metadata is non-fatal. This discovery operation may allocate and
// access sysfs and is not real-time safe.
[[nodiscard]] hardware::Result<std::vector<PortInfo>, Error> list_ports() noexcept;

}  // namespace serial
