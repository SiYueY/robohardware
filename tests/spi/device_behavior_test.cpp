#include <spi/device.hpp>

#include "spidev_fake.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr spi::Config kConfig{
    spi::Mode::Mode3,
    1'000'000,
    16,
    spi::BitOrder::LsbFirst,
};

using Operation = spi::spidev::fake::Operation;

void expect_closed_after_failed_open(spi::Device& device, spi::Error expected) {
    auto result = device.open("/dev/spidev0.0", kConfig);
    assert(!result && result.error() == expected);
    assert(!device.is_open());
}

}  // namespace

int main() {
    {
        spi::spidev::fake::reset();
        spi::Device device;
        auto result = device.open("", kConfig);
        assert(!result && result.error() == spi::Error::InvalidArgument);
        assert(spi::spidev::fake::calls().empty());
    }

    {
        spi::spidev::fake::reset();
        spi::spidev::fake::fail(Operation::Open, 1, EBUSY);
        spi::Device device;
        expect_closed_after_failed_open(device, spi::Error::DeviceOrResourceBusy);
    }

    {
        spi::spidev::fake::reset();
        spi::spidev::fake::fail(Operation::WriteMode, 1, EIO);
        spi::Device device;
        expect_closed_after_failed_open(device, spi::Error::InputOutputError);
    }

    {
        spi::spidev::fake::reset();
        spi::spidev::fake::fail(Operation::ReadMode, 2, EIO);
        spi::Device device;
        expect_closed_after_failed_open(device, spi::Error::InputOutputError);
    }

    {
        spi::spidev::fake::reset();
        spi::spidev::fake::fail(Operation::WriteMaxSpeed, 1, EIO);
        spi::Device device;
        expect_closed_after_failed_open(device, spi::Error::InputOutputError);
    }

    {
        spi::spidev::fake::reset();
        spi::spidev::fake::ignore_write(Operation::WriteMode);
        spi::Device device;
        expect_closed_after_failed_open(device, spi::Error::ConfigurationMismatch);
    }

    {
        spi::spidev::fake::reset();
        spi::spidev::fake::ignore_write(Operation::WriteMaxSpeed);
        spi::Device device;
        expect_closed_after_failed_open(device, spi::Error::ConfigurationMismatch);

        const std::vector<Operation>& calls = spi::spidev::fake::calls();
        assert(calls[calls.size() - 4] == Operation::WriteMaxSpeed);
        assert(calls[calls.size() - 3] == Operation::WriteBitsPerWord);
        assert(calls[calls.size() - 2] == Operation::WriteMode);
        assert(calls.back() == Operation::Close);
    }

    {
        spi::spidev::fake::reset();
        spi::spidev::fake::fail(Operation::WriteBitsPerWord, 1, EIO);
        spi::spidev::fake::fail(Operation::WriteMode, 2, EPERM);
        spi::Device device;
        expect_closed_after_failed_open(device, spi::Error::InputOutputError);
    }

    {
        spi::spidev::fake::reset();
        spi::Device device;
        assert(device.open("/dev/spidev0.0", kConfig));

        std::byte buffer[2]{};
        spi::spidev::fake::set_message_result(1);
        auto short_transfer = device.transfer(buffer, nullptr, 2);
        assert(!short_transfer && short_transfer.error() == spi::Error::TransferMismatch);

        const auto oversized = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1U;
        auto oversized_transfer = device.transfer(buffer, nullptr, oversized);
        assert(!oversized_transfer && oversized_transfer.error() == spi::Error::MessageTooLong);
        assert(device.close());
    }
}
