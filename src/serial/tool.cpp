#include <serial/tool.hpp>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace serial {
namespace {
namespace fs = std::filesystem;

[[nodiscard]] std::string read_text(const fs::path& path) {
    std::ifstream file(path);
    std::string value;
    std::getline(file, value);
    return value;
}

[[nodiscard]] bool parse_hex(std::string_view value, std::uint16_t& output) noexcept {
    unsigned int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 16);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    output = static_cast<std::uint16_t>(parsed);
    return true;
}

void enrich_usb(const fs::path& device, PortInfo& info) {
    std::error_code error;
    auto current = fs::weakly_canonical(device, error);
    if (error) return;

    for (;;) {
        const auto vendor = read_text(current / "idVendor");
        const auto product = read_text(current / "idProduct");
        std::uint16_t vendor_id = 0;
        std::uint16_t product_id = 0;
        if (parse_hex(vendor, vendor_id) && parse_hex(product, product_id)) {
            info.usb.available = true;
            info.usb.vendor_id = vendor_id;
            info.usb.product_id = product_id;
            info.usb.serial_number = read_text(current / "serial");
            info.usb.manufacturer = read_text(current / "manufacturer");
            info.usb.product = read_text(current / "product");
            if (!info.usb.product.empty()) info.description = info.usb.product;
            return;
        }

        const auto parent = current.parent_path();
        if (parent == current || parent.empty()) return;
        current = parent;
    }
}

}  // namespace

hardware::Result<std::vector<PortInfo>, Error> list_ports() noexcept {
    try {
        std::vector<PortInfo> ports;
        std::error_code error;
        fs::directory_iterator iterator("/sys/class/tty", error);
        if (error) return hardware::Result<std::vector<PortInfo>, Error>::failure(Error::Io);

        for (const auto& entry : iterator) {
            const auto device = entry.path() / "device";
            if (!fs::exists(device, error)) {
                error.clear();
                continue;
            }

            const auto name = entry.path().filename().string();
            const fs::path dev_path = fs::path("/dev") / name;
            if (!fs::exists(dev_path, error)) {
                error.clear();
                continue;
            }

            PortInfo info{};
            info.path = dev_path.string();
            info.description = name;
            enrich_usb(device, info);
            ports.push_back(std::move(info));
        }

        std::sort(ports.begin(), ports.end(), [](const PortInfo& left, const PortInfo& right) {
            return left.path < right.path;
        });
        return hardware::Result<std::vector<PortInfo>, Error>::success(std::move(ports));
    } catch (const std::bad_alloc&) {
        return hardware::Result<std::vector<PortInfo>, Error>::failure(Error::OutOfMemory);
    } catch (const std::length_error&) {
        return hardware::Result<std::vector<PortInfo>, Error>::failure(Error::OutOfMemory);
    } catch (const fs::filesystem_error&) {
        return hardware::Result<std::vector<PortInfo>, Error>::failure(Error::Io);
    }
}

}  // namespace serial
