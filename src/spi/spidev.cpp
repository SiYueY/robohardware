#include "spidev.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace spi::spidev {

int open(const char* path) noexcept { return ::open(path, O_RDWR | O_CLOEXEC); }

int close(int fd) noexcept { return ::close(fd); }

int read_mode(int fd, std::uint32_t& mode) noexcept {
    return ::ioctl(fd, SPI_IOC_RD_MODE32, &mode);
}

int write_mode(int fd, std::uint32_t mode) noexcept {
    return ::ioctl(fd, SPI_IOC_WR_MODE32, &mode);
}

int read_bits_per_word(int fd, std::uint8_t& bits_per_word) noexcept {
    return ::ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits_per_word);
}

int write_bits_per_word(int fd, std::uint8_t bits_per_word) noexcept {
    return ::ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word);
}

int read_max_speed(int fd, std::uint32_t& max_speed) noexcept {
    return ::ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &max_speed);
}

int write_max_speed(int fd, std::uint32_t max_speed) noexcept {
    return ::ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed);
}

int message(int fd, spi_ioc_transfer* transfers, std::size_t count) noexcept {
    const auto request = SPI_IOC_MESSAGE(count);
    if (count == 0 || request == 0) {
        errno = EMSGSIZE;
        return -1;
    }
    return ::ioctl(fd, request, transfers);
}

}  // namespace spi::spidev
