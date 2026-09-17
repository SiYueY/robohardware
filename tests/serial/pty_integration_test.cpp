#include <serial/port.hpp>
#include <serial/tool.hpp>

#include <cassert>
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
};
}  // namespace
int main() {
    Pty pty;
    serial::Config config{115200};
    serial::Port port;
    assert(port.open(pty.slave, config));
    assert(port.is_open());
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
    const auto timeout = port.read(received, sizeof(received), std::chrono::milliseconds(1));
    assert(!timeout && timeout.error() == serial::Error::TimedOut);
    const auto would_block = port.try_read(received, sizeof(received));
    assert(!would_block && would_block.error() == serial::Error::WouldBlock);
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
}
