#include <serial/port.hpp>
#include <serial/tool.hpp>

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

namespace {
struct Pty final {
    int master{-1};
    char slave[128]{};
    Pty() {
        master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
        assert(master >= 0);
        assert(::grantpt(master) == 0);
        assert(::unlockpt(master) == 0);
        assert(::ptsname_r(master, slave, sizeof(slave)) == 0);
    }
    ~Pty() {
        if (master >= 0) static_cast<void>(::close(master));
    }
    void close_master() {
        assert(master >= 0);
        assert(::close(master) == 0);
        master = -1;
    }
};
}  // namespace
int main() {
    Pty pty;
    serial::Config config{115200};
    serial::Port port;
    assert(port.open(pty.slave, config));
    assert(port.is_open());
    const int concurrent = ::open(pty.slave, O_RDWR | O_NOCTTY | O_CLOEXEC);
    assert(concurrent < 0 && errno == EBUSY);

    const std::byte incoming[] = {std::byte{0x12}, std::byte{0x34}};
    assert(
        ::write(pty.master, incoming, sizeof(incoming)) == static_cast<ssize_t>(sizeof(incoming)));
    assert(port.wait_readable(std::chrono::milliseconds(100)));
    const auto available = port.bytes_available();
    assert(available && available.value() >= sizeof(incoming));
    std::byte received[8]{};
    const auto read = port.read(received, sizeof(received), std::chrono::milliseconds(100));
    assert(read && read.value() == sizeof(incoming));
    assert(std::memcmp(received, incoming, sizeof(incoming)) == 0);
    const auto timeout = port.read(received, sizeof(received), std::chrono::nanoseconds(0));
    assert(!timeout && timeout.error() == serial::Error::TimedOut);
    const auto would_block = port.try_read(received, sizeof(received));
    assert(!would_block && would_block.error() == serial::Error::WouldBlock);

    const std::byte fragment_one[] = {std::byte{0x56}};
    const std::byte fragment_two[] = {std::byte{0x78}};
    assert(::write(pty.master, fragment_one, sizeof(fragment_one)) == 1);
    assert(port.read(received, sizeof(received), std::chrono::nanoseconds(0)));
    assert(::write(pty.master, fragment_two, sizeof(fragment_two)) == 1);
    assert(port.read(received, sizeof(received), std::chrono::nanoseconds(0)));

    const std::byte burst[] = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    assert(::write(pty.master, burst, sizeof(burst)) == static_cast<ssize_t>(sizeof(burst)));
    const auto burst_read = port.read(received, sizeof(received), std::chrono::milliseconds(100));
    assert(burst_read && burst_read.value() == sizeof(burst));
    assert(std::memcmp(received, burst, sizeof(burst)) == 0);

    assert(port.wait_writable(std::chrono::nanoseconds(0)));
    const std::byte outgoing[] = {std::byte{0xab}, std::byte{0xcd}};
    const auto written = port.write(outgoing, sizeof(outgoing), std::chrono::milliseconds(100));
    assert(written && written.value() > 0);
    std::byte peer_received[8]{};
    assert(
        ::read(pty.master, peer_received, sizeof(peer_received)) ==
        static_cast<ssize_t>(written.value()));
    assert(std::memcmp(peer_received, outgoing, written.value()) == 0);
    assert(port.drain());
    assert(port.discard_buffers());
    const auto ports = serial::list_ports();
    assert(ports);
    assert(port.close());
    assert(!port.is_open());
    const int reopened = ::open(pty.slave, O_RDWR | O_NOCTTY | O_CLOEXEC);
    assert(reopened >= 0);
    assert(::close(reopened) == 0);

    Pty disconnected_pty;
    serial::Port disconnected_port;
    assert(disconnected_port.open(disconnected_pty.slave, config));
    disconnected_pty.close_master();
    const auto disconnected_read = disconnected_port.wait_readable(std::chrono::milliseconds(100));
    assert(!disconnected_read && disconnected_read.error() == serial::Error::Disconnected);
    const auto disconnected = disconnected_port.wait_writable(std::chrono::milliseconds(100));
    assert(!disconnected && disconnected.error() == serial::Error::Disconnected);
    const auto disconnected_close = disconnected_port.close();
    assert(!disconnected_close && disconnected_close.error() == serial::Error::Disconnected);
    assert(!disconnected_port.is_open());
}
