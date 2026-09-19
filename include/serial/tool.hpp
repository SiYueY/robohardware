#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <hardware/result.hpp>
#include <serial/error.hpp>

namespace serial {

/**
 * @brief Describes a discovered Linux serial TTY.
 */
struct PortInfo final {
    /**
     * @brief Optional USB metadata associated with a serial TTY.
     *
     * available is true only when USB metadata was successfully identified.
     * The remaining fields are unspecified when it is false.
     */
    struct USB final {
        bool available{false};        ///< Whether USB metadata was identified.
        std::uint16_t vendor_id{0};   ///< USB vendor identifier.
        std::uint16_t product_id{0};  ///< USB product identifier.
        std::string serial_number;    ///< Optional USB device serial number.
        std::string manufacturer;     ///< Optional USB manufacturer name.
        std::string product;          ///< Optional USB product name.
    };

    std::string path;         ///< Path to the TTY device node.
    std::string description;  ///< Optional human-readable device description.
    USB usb{};                ///< Optional USB metadata.
};

/**
 * @brief Discovers Linux TTY devices.
 *
 * This control-plane operation accesses sysfs and may allocate; it is not
 * real-time safe. An empty device list is successful, and missing optional
 * metadata for an individual device is not an error.
 *
 * @return Discovered ports sorted by device path, or a Serial error.
 */
[[nodiscard]] hardware::Result<std::vector<PortInfo>, Error> list_ports() noexcept;

}  // namespace serial
