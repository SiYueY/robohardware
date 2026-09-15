#include <spi/device.hpp>

#include <cassert>

namespace {

constexpr spi::Config kConfig{
    spi::Mode::Mode0,
    1'000'000,
    8,
    spi::BitOrder::MsbFirst,
};

}  // namespace

int main() {
    spi::Device device;

    auto invalid_path = device.open("", kConfig);
    assert(!invalid_path && invalid_path.error() == spi::Error::InvalidArgument);
    assert(!device.is_open());

    auto invalid_config = device.open(
        "/dev/null", {static_cast<spi::Mode>(42), 1'000'000, 8, spi::BitOrder::MsbFirst});
    assert(!invalid_config && invalid_config.error() == spi::Error::InvalidArgument);
    assert(!device.is_open());

    auto unsupported = device.open("/dev/null", kConfig);
    assert(!unsupported && unsupported.error() == spi::Error::InappropriateIoControlOperation);
    assert(!device.is_open());

    auto missing = device.open("/dev/robohardware-spi-does-not-exist", kConfig);
    assert(!missing && missing.error() == spi::Error::NoSuchFileOrDirectory);
    assert(!device.is_open());
}
