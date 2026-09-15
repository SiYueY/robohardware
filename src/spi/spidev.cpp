#include "spidev.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/ioctl.h>
#include <unistd.h>

namespace spi::spidev {

int open(const char* path) noexcept { return ::open(path, O_RDWR | O_CLOEXEC); }

int close(int fd) noexcept { return ::close(fd); }

int read_config(int fd, Config& config) noexcept {
    if (::ioctl(fd, SPI_IOC_RD_MODE32, &config.mode) < 0) return -1;
    if (::ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &config.bits_per_word) < 0) return -1;
    return ::ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &config.max_speed);
}

int write_mode(int fd, std::uint32_t mode) noexcept {
    return ::ioctl(fd, SPI_IOC_WR_MODE32, &mode);
}

int write_bits_per_word(int fd, std::uint8_t bits_per_word) noexcept {
    return ::ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word);
}

int write_max_speed(int fd, std::uint32_t max_speed) noexcept {
    return ::ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed);
}

int message(int fd, const std::byte* tx, std::byte* rx, std::size_t size) noexcept {
    constexpr std::size_t kMaximumTransferSize =
        std::numeric_limits<__u32>::max() < std::numeric_limits<int>::max()
            ? std::numeric_limits<__u32>::max()
            : std::numeric_limits<int>::max();
    if (size > kMaximumTransferSize) {
        errno = EMSGSIZE;
        return -1;
    }

    spi_ioc_transfer transfer{};
    transfer.tx_buf = static_cast<__u64>(reinterpret_cast<std::uintptr_t>(tx));
    transfer.rx_buf = static_cast<__u64>(reinterpret_cast<std::uintptr_t>(rx));
    transfer.len = static_cast<__u32>(size);
    return ::ioctl(fd, SPI_IOC_MESSAGE(1), &transfer);
}

}  // namespace spi::spidev
