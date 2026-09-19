#include <serial/port.hpp>

#include "tty_fake.hpp"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <poll.h>
#include <utility>
#include <vector>

namespace {

using Operation = serial::tty::fake::Operation;
using namespace std::chrono_literals;

constexpr serial::Config kConfig{115200};

void expect_failed_open(serial::Error expected) {
    serial::Port port;
    auto result = port.open("/dev/ttyS0", kConfig);
    assert(!result && result.error() == expected);
    assert(!port.is_open());
}

[[nodiscard]] std::size_t count(Operation operation) {
    std::size_t total = 0;
    for (const auto call : serial::tty::fake::calls()) {
        if (call == operation) ++total;
    }
    return total;
}

}  // namespace

int main() {
    {
        serial::tty::fake::reset();
        serial::Port port;
        auto result = port.open("", kConfig);
        assert(!result && result.error() == serial::Error::InvalidArgument);
        assert(serial::tty::fake::calls().empty());
    }

    {
        serial::tty::fake::reset();
        serial::Config config{12345};
        serial::Port port;
        auto result = port.open("/dev/ttyS0", config);
        assert(!result && result.error() == serial::Error::Unsupported);
        assert(serial::tty::fake::calls().empty());
    }

    {
        serial::tty::fake::reset();
        serial::Config config;
        serial::Port port;
        auto result = port.open("/dev/ttyS0", config);
        assert(!result && result.error() == serial::Error::InvalidArgument);
        assert(serial::tty::fake::calls().empty());
    }

    {
        serial::tty::fake::reset();
        serial::Config config{115200};
        config.data_bits = static_cast<serial::DataBits>(0);
        serial::Port port;
        auto result = port.open("/dev/ttyS0", config);
        assert(!result && result.error() == serial::Error::InvalidArgument);
        assert(serial::tty::fake::calls().empty());
    }

    {
        serial::tty::fake::reset();
        serial::Config config{115200};
        config.rs485.delay_before_send = -1ms;
        serial::Port port;
        auto result = port.open("/dev/ttyS0", config);
        assert(!result && result.error() == serial::Error::InvalidArgument);
        assert(serial::tty::fake::calls().empty());
    }

    {
        serial::tty::fake::reset();
        serial::Config config{115200};
        config.rs485.delay_after_send = std::chrono::milliseconds{
            static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<std::uint32_t>::max()) +
            1};
        serial::Port port;
        auto result = port.open("/dev/ttyS0", config);
        assert(!result && result.error() == serial::Error::InvalidArgument);
        assert(serial::tty::fake::calls().empty());
    }

    {
        serial::tty::fake::reset();
        serial::Config config{115200};
        config.flow_control = serial::FlowControl::RtsCts;
        config.rs485.enabled = true;
        serial::Port port;
        auto result = port.open("/dev/ttyS0", config);
        assert(!result && result.error() == serial::Error::InvalidArgument);
        assert(serial::tty::fake::calls().empty());
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::Open, 1, EBUSY);
        expect_failed_open(serial::Error::Busy);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::Open, 1, ENOENT);
        expect_failed_open(serial::Error::DeviceNotFound);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::Open, 1, ENOMEM);
        expect_failed_open(serial::Error::OutOfMemory);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::Open, 1, EIO);
        expect_failed_open(serial::Error::Io);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::SetExclusive, 1, EBUSY);
        expect_failed_open(serial::Error::Busy);
        assert(count(Operation::Close) == 1);
        assert(count(Operation::ClearExclusive) == 0);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::SetExclusive, 1, EACCES);
        expect_failed_open(serial::Error::PermissionDenied);
        assert(count(Operation::Close) == 1);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::Open, 1, ENODEV);
        expect_failed_open(serial::Error::DeviceNotFound);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::set_terminal(false);
        expect_failed_open(serial::Error::NotTerminal);
        assert(count(Operation::Close) == 1);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::ReadRs485, 1, EIO);
        expect_failed_open(serial::Error::Disconnected);
        assert(count(Operation::Close) == 1);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::fail(Operation::WriteAttributes, 1, EIO);
        expect_failed_open(serial::Error::Disconnected);
        assert(count(Operation::WriteAttributes) == 2);
        assert(count(Operation::ClearExclusive) == 1);
        assert(count(Operation::Close) == 1);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::ignore_write(Operation::WriteAttributes);
        expect_failed_open(serial::Error::Unsupported);
        assert(count(Operation::WriteAttributes) == 2);
        assert(count(Operation::Close) == 1);
    }

    {
        serial::tty::fake::reset();
        serial::tty::fake::set_rs485_supported(true);
        serial::tty::fake::ignore_write(Operation::WriteRs485);
        serial::Config config{115200};
        config.rs485.enabled = true;
        serial::Port port;
        auto result = port.open("/dev/ttyS0", config);
        assert(!result && result.error() == serial::Error::Unsupported);
        assert(!port.is_open());
        assert(count(Operation::WriteRs485) >= 2);
        assert(count(Operation::Close) == 1);
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::fail(Operation::Read, 1, EAGAIN);
        std::byte buffer[8]{};
        auto result = port.try_read(buffer, sizeof(buffer));
        assert(!result && result.error() == serial::Error::WouldBlock);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::fail(Operation::Read, 1, ENODEV);
        std::byte buffer[8]{};
        auto result = port.try_read(buffer, sizeof(buffer));
        assert(!result && result.error() == serial::Error::Disconnected);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::fail(Operation::InputQueueSize, 1, ENOTTY);
        auto result = port.bytes_available();
        assert(!result && result.error() == serial::Error::Unsupported);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_result(1, POLLIN);
        serial::tty::fake::set_read_results({{-1, EAGAIN}});
        std::byte buffer[8]{};
        auto result = port.read(buffer, sizeof(buffer), 0ns);
        assert(!result && result.error() == serial::Error::TimedOut);
        assert(count(Operation::Wait) == 1);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_result(1, POLLOUT);
        serial::tty::fake::set_write_results({{-1, EAGAIN}});
        const std::byte buffer[8]{};
        auto result = port.write(buffer, sizeof(buffer), 0ns);
        assert(!result && result.error() == serial::Error::TimedOut);
        assert(count(Operation::Wait) == 1);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port source;
        assert(source.open("/dev/ttyS0", kConfig));
        serial::Port destination(std::move(source));
        assert(!source.is_open());
        assert(destination.is_open());
        assert(destination.close());
        assert(count(Operation::ClearExclusive) == 1);
        assert(count(Operation::Close) == 1);
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_result(1, POLLIN);
        std::byte buffer[8]{};
        auto result = port.read(buffer, sizeof(buffer), 0ns);
        assert(result && result.value() == 1);
        const auto& timeouts = serial::tty::fake::wait_timeouts();
        assert(timeouts.size() == 1);
        assert(timeouts[0].tv_sec == 0 && timeouts[0].tv_nsec == 0);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_result(0);
        auto result = port.wait_readable(0ns);
        assert(!result && result.error() == serial::Error::TimedOut);
        const auto& timeouts = serial::tty::fake::wait_timeouts();
        assert(timeouts.size() == 1);
        assert(timeouts[0].tv_sec == 0 && timeouts[0].tv_nsec == 0);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        auto result = port.wait_writable(0ns);
        assert(result);
        const auto& timeouts = serial::tty::fake::wait_timeouts();
        assert(timeouts.size() == 1);
        assert(timeouts[0].tv_sec == 0 && timeouts[0].tv_nsec == 0);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_results({{1, POLLIN}, {0, 0}});
        serial::tty::fake::set_read_results({{0}});
        std::byte buffer[8]{};
        auto result = port.read(buffer, sizeof(buffer), 10ms);
        assert(!result && result.error() == serial::Error::TimedOut);
        assert(count(Operation::Read) == 1);
        assert(count(Operation::Wait) == 2);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_results({{1, POLLIN}, {1, POLLIN}});
        serial::tty::fake::set_read_results({{0}, {0}});
        std::byte buffer[8]{};
        auto result = port.read(buffer, sizeof(buffer));
        assert(!result && result.error() == serial::Error::Io);
        assert(count(Operation::Read) == 2);
        assert(count(Operation::Wait) == 2);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_results({{1, POLLIN}, {1, POLLIN}});
        serial::tty::fake::set_read_results({{-1, EAGAIN}, {2}});
        std::byte buffer[8]{};
        auto result = port.read(buffer, sizeof(buffer));
        assert(result && result.value() == 2);
        assert(count(Operation::Wait) == 2);
        assert(count(Operation::Read) == 2);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_results({{1, POLLIN}, {1, POLLIN}});
        serial::tty::fake::set_read_results({{-1, EAGAIN}, {-1, EAGAIN}});
        std::byte buffer[8]{};
        auto result = port.read(buffer, sizeof(buffer));
        assert(!result && result.error() == serial::Error::Io);
        assert(count(Operation::Wait) == 2);
        assert(count(Operation::Read) == 2);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_results({{1, POLLIN}, {1, POLLIN}});
        serial::tty::fake::set_read_results({{0}, {-1, EAGAIN}});
        std::byte buffer[8]{};
        auto result = port.read(buffer, sizeof(buffer));
        assert(!result && result.error() == serial::Error::Io);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_results({{1, POLLOUT}, {1, POLLOUT}});
        serial::tty::fake::set_write_results({{-1, EAGAIN}, {2}});
        const std::byte buffer[8]{};
        auto result = port.write(buffer, sizeof(buffer));
        assert(result && result.value() == 2);
        assert(count(Operation::Wait) == 2);
        assert(count(Operation::Write) == 2);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_results({{1, POLLOUT}, {1, POLLOUT}});
        serial::tty::fake::set_write_results({{-1, EAGAIN}, {-1, EAGAIN}});
        const std::byte buffer[8]{};
        auto result = port.write(buffer, sizeof(buffer));
        assert(!result && result.error() == serial::Error::Io);
        assert(count(Operation::Wait) == 2);
        assert(count(Operation::Write) == 2);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_write_result(2);
        const std::byte buffer[8]{};
        auto result = port.write(buffer, sizeof(buffer));
        assert(result && result.value() == 2);
        assert(count(Operation::Write) == 1);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_read_result(2);
        std::byte buffer[8]{};
        auto result = port.read(buffer, sizeof(buffer));
        assert(result && result.value() == 2);
        assert(count(Operation::Read) == 1);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::fail(Operation::Wait, 1, EINVAL);
        auto result = port.wait_readable(100ms);
        assert(!result && result.error() == serial::Error::Io);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_monotonic_times({{0, 0}, {0, 0}, {0, 40'000'000}});
        serial::tty::fake::set_wait_result(0);
        serial::tty::fake::fail(Operation::Wait, 1, EINTR);
        auto result = port.wait_readable(100ms);
        assert(!result && result.error() == serial::Error::TimedOut);

        const auto& timeouts = serial::tty::fake::wait_timeouts();
        assert(timeouts.size() == 2);
        assert(timeouts[0].tv_sec == 0 && timeouts[0].tv_nsec == 100'000'000);
        assert(timeouts[1].tv_sec == 0 && timeouts[1].tv_nsec == 60'000'000);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_result(1, POLLHUP);
        auto result = port.wait_writable(100ms);
        assert(!result && result.error() == serial::Error::Disconnected);
        assert(port.close());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));

        serial::tty::fake::set_wait_result(1, POLLIN | POLLHUP);
        auto result = port.wait_readable(100ms);
        assert(result);
        assert(port.close());
    }

#ifdef CRTSCTS
    {
        serial::tty::fake::reset();
        serial::Config config{115200};
        config.flow_control = serial::FlowControl::RtsCts;
        serial::Port port;
        assert(port.open("/dev/ttyS0", config));
        auto result = port.set_rts(true);
        assert(!result && result.error() == serial::Error::InvalidState);
        assert(count(Operation::WriteModemLine) == 0);
        assert(port.close());
    }
#endif

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));
        serial::tty::fake::fail(Operation::Close, 1, EIO);
        auto result = port.close();
        assert(!result && result.error() == serial::Error::Disconnected);
        assert(count(Operation::ClearExclusive) == 1);
        assert(!port.is_open());
    }

    {
        serial::tty::fake::reset();
        serial::Port port;
        assert(port.open("/dev/ttyS0", kConfig));
        serial::tty::fake::fail(Operation::ClearExclusive, 1, EIO);
        auto result = port.close();
        assert(!result && result.error() == serial::Error::Disconnected);
        assert(count(Operation::Close) == 1);
        assert(!port.is_open());
    }
}
