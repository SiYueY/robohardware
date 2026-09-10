#include "serial/port.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

class Pty {
public:
    Pty() {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (master_ < 0 || ::grantpt(master_) < 0 || ::unlockpt(master_) < 0) {
            const int code = errno;
            if (master_ >= 0) ::close(master_);
            throw std::runtime_error("could not create PTY: " + std::to_string(code));
        }
        const char* value = ::ptsname(master_);
        if (value == nullptr) {
            const int code = errno;
            ::close(master_);
            throw std::runtime_error("could not name PTY: " + std::to_string(code));
        }
        slave_ = value;
    }

    ~Pty() { ::close(master_); }
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    int master() const noexcept { return master_; }
    const std::string& slave() const noexcept { return slave_; }

private:
    int master_{-1};
    std::string slave_;
};

serial::Port open_port(const Pty& pty, std::uint32_t baud = 115200) {
    auto result = serial::Port::open({pty.slave(), baud});
    if (!result) throw std::runtime_error("could not open serial PTY");
    return std::move(result.value());
}

void write_all(int fd, const std::uint8_t* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(fd, data + offset, size - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            ::pollfd pollfd{fd, POLLOUT, 0};
            ASSERT_GT(::poll(&pollfd, 1, 1000), 0);
            continue;
        }
        throw std::runtime_error("PTY write failed");
    }
}

TEST(Port, HasExpectedOwnershipProperties) {
    static_assert(!std::is_default_constructible_v<serial::Port>);
    static_assert(!std::is_copy_constructible_v<serial::Port>);
    static_assert(std::is_nothrow_move_constructible_v<serial::Port>);
}

TEST(Port, RejectsInvalidConfiguration) {
    EXPECT_EQ(serial::Port::open({}).error().code, serial::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        serial::Port::open({"/dev/null", 0}).error().code, serial::ErrorCode::InvalidArgument);
}

TEST(Port, ReportsOpenFailureForMissingDevice) {
    const auto opened = serial::Port::open({"/definitely/not/a/serial/device", 115200});
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error().code, serial::ErrorCode::OpenFailed);
}

TEST(Port, OpensPtyAsNonBlockingRawByteStream) {
    Pty pty;
    auto port = open_port(pty);
    EXPECT_NE(port.fd(), -1);
    EXPECT_NE(::fcntl(port.fd(), F_GETFL) & O_NONBLOCK, 0);

    const std::array<std::uint8_t, 4> sent{{0x00, 0x0A, 0x0D, 0xFF}};
    write_all(pty.master(), sent.data(), sent.size());
    ASSERT_TRUE(port.wait_readable(std::chrono::milliseconds(100)));
    std::array<std::uint8_t, 8> received{};
    const auto count = port.read(received.data(), received.size());
    ASSERT_TRUE(count);
    ASSERT_EQ(count.value(), sent.size());
    EXPECT_EQ(std::equal(sent.begin(), sent.end(), received.begin()), true);
    const serial::Stats stats = port.stats();
    EXPECT_EQ(stats.rx_bytes, sent.size());
    EXPECT_EQ(stats.read_calls, 1U);
    EXPECT_EQ(stats.read_errors, 0U);
}

TEST(Port, ConfiguresRequiredStandardBaudRates) {
    for (const std::uint32_t baud : {9600U, 115200U, 921600U}) {
        Pty pty;
        const auto port = serial::Port::open({pty.slave(), baud});
        ASSERT_TRUE(port);
        EXPECT_EQ(port.value().config().baud_rate, baud);
    }
}

TEST(Port, EmptyReadAndZeroSizeOperationsSucceed) {
    Pty pty;
    auto port = open_port(pty);
    std::array<std::uint8_t, 1> data{};
    ASSERT_TRUE(port.read(data.data(), data.size()));
    EXPECT_EQ(port.read(data.data(), data.size()).value(), 0U);
    EXPECT_EQ(port.read(nullptr, 0).value(), 0U);
    EXPECT_EQ(port.write(nullptr, 0).value(), 0U);
    EXPECT_EQ(port.read(nullptr, 1).error().code, serial::ErrorCode::InvalidArgument);
    EXPECT_EQ(port.write(nullptr, 1).error().code, serial::ErrorCode::InvalidArgument);
}

TEST(Port, WritesAndReportsStatistics) {
    Pty pty;
    auto port = open_port(pty);
    const std::array<std::uint8_t, 3> sent{{0x00, 0x11, 0xFF}};
    const auto count = port.write(sent.data(), sent.size());
    ASSERT_TRUE(count);
    ASSERT_EQ(count.value(), sent.size());

    ::pollfd pollfd{pty.master(), POLLIN, 0};
    ASSERT_GT(::poll(&pollfd, 1, 1000), 0);
    std::array<std::uint8_t, 3> received{};
    ASSERT_EQ(
        ::read(pty.master(), received.data(), received.size()),
        static_cast<ssize_t>(received.size()));
    EXPECT_EQ(received, sent);
    const serial::Stats stats = port.stats();
    EXPECT_EQ(stats.tx_bytes, sent.size());
    EXPECT_EQ(stats.write_calls, 1U);
    EXPECT_EQ(stats.write_errors, 0U);
}

TEST(Port, CanReportPartialNonBlockingWrite) {
    Pty pty;
    auto port = open_port(pty);
    const std::vector<std::uint8_t> data(1024 * 1024, 0xA5);
    const auto count = port.write(data.data(), data.size());
    ASSERT_TRUE(count);
    EXPECT_GT(count.value(), 0U);
    EXPECT_LT(count.value(), data.size());
    EXPECT_EQ(port.stats().tx_bytes, count.value());
}

TEST(Port, WaitAndFlushOperationsHaveDefinedResults) {
    Pty pty;
    auto port = open_port(pty);
    EXPECT_FALSE(port.wait_readable(std::chrono::milliseconds(1)).value());
    EXPECT_EQ(
        port.wait_readable(std::chrono::milliseconds(-1)).error().code,
        serial::ErrorCode::InvalidArgument);
    ASSERT_TRUE(port.wait_writable(std::chrono::milliseconds(100)));

    const std::uint8_t byte = 0x42;
    write_all(pty.master(), &byte, 1);
    ASSERT_TRUE(port.wait_readable(std::chrono::milliseconds(100)));
    ASSERT_TRUE(port.available());
    EXPECT_GE(port.available().value(), 1U);
    ASSERT_TRUE(port.flush_input());
    EXPECT_EQ(port.available().value(), 0U);
    ASSERT_TRUE(port.pending_write());
    ASSERT_TRUE(port.flush_output());
    ASSERT_TRUE(port.drain());
}

TEST(Port, SupportsOrExplicitlyRejectsDriverSpecificCapabilities) {
    Pty pty;
    auto arbitrary = serial::Port::open(
        {pty.slave(), 250000, serial::DataBits::Eight, serial::Parity::None,
         serial::StopBits::Two});
    ASSERT_TRUE(arbitrary || arbitrary.error().code == serial::ErrorCode::Unsupported);
    if (!arbitrary) return;
    auto port = std::move(arbitrary.value());
    const auto check_capability = [](const auto& result) {
        EXPECT_TRUE(
            result || result.error().code == serial::ErrorCode::Unsupported ||
            result.error().code == serial::ErrorCode::IoFailed);
    };
    check_capability(port.set_break(true));
    check_capability(port.set_break(false));
    check_capability(port.set_rts(true));
    check_capability(port.set_dtr(true));
    check_capability(port.signals());
}

TEST(Port, MoveTransfersDescriptorOwnership) {
    Pty pty;
    auto first = open_port(pty);
    const int fd = first.fd();
    serial::Port second = std::move(first);
    EXPECT_EQ(first.fd(), -1);
    EXPECT_EQ(second.fd(), fd);
    serial::Port third = open_port(pty);
    third = std::move(second);
    EXPECT_EQ(second.fd(), -1);
    EXPECT_EQ(third.fd(), fd);
}

TEST(Port, DestructorClosesOwnedDescriptor) {
    Pty pty;
    int fd = -1;
    {
        auto port = open_port(pty);
        fd = port.fd();
    }
    errno = 0;
    const int result = ::fcntl(fd, F_GETFD);
    const int code = errno;
    EXPECT_EQ(result, -1);
    EXPECT_EQ(code, EBADF);
}

}  // namespace
