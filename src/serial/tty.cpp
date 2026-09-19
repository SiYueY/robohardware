#include "tty.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace serial::tty {

int open(const char* path) noexcept {
    return ::open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
}

int close(int fd) noexcept { return ::close(fd); }

int is_terminal(int fd) noexcept { return ::isatty(fd); }

int set_exclusive(int fd) noexcept { return ::ioctl(fd, TIOCEXCL); }

int clear_exclusive(int fd) noexcept { return ::ioctl(fd, TIOCNXCL); }

int read_attributes(int fd, termios& attributes) noexcept { return ::tcgetattr(fd, &attributes); }

int write_attributes(int fd, int action, const termios& attributes) noexcept {
    return ::tcsetattr(fd, action, &attributes);
}

int read_rs485(int fd, serial_rs485& configuration) noexcept {
    return ::ioctl(fd, TIOCGRS485, &configuration);
}

int write_rs485(int fd, const serial_rs485& configuration) noexcept {
    auto request = configuration;
    return ::ioctl(fd, TIOCSRS485, &request);
}

int monotonic_now(timespec& value) noexcept { return ::clock_gettime(CLOCK_MONOTONIC, &value); }

int wait(int fd, short events, const timespec* timeout, short& revents) noexcept {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = events;
    const int result = ::ppoll(&descriptor, 1, timeout, nullptr);
    revents = descriptor.revents;
    return result;
}

ssize_t read(int fd, void* data, std::size_t size) noexcept { return ::read(fd, data, size); }

ssize_t write(int fd, const void* data, std::size_t size) noexcept {
    return ::write(fd, data, size);
}

int input_queue_size(int fd, int& size) noexcept { return ::ioctl(fd, FIONREAD, &size); }

int output_queue_size(int fd, int& size) noexcept { return ::ioctl(fd, TIOCOUTQ, &size); }

int discard(int fd, int selector) noexcept { return ::tcflush(fd, selector); }

int drain(int fd) noexcept { return ::tcdrain(fd); }

int read_modem_lines(int fd, int& lines) noexcept { return ::ioctl(fd, TIOCMGET, &lines); }

int write_modem_line(int fd, int bits, bool asserted) noexcept {
    return ::ioctl(fd, asserted ? TIOCMBIS : TIOCMBIC, &bits);
}

int write_break(int fd, bool asserted) noexcept {
    return ::ioctl(fd, asserted ? TIOCSBRK : TIOCCBRK);
}

}  // namespace serial::tty
