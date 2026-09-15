#include <spi/device.hpp>

#include <type_traits>

static_assert(std::is_same_v<
              decltype(std::declval<spi::Device&>().close()), hardware::Result<void, spi::Error>>);
static_assert(std::is_same_v<decltype(spi::Config{}.max_speed), std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<spi::Error>, int>);
static_assert(static_cast<int>(spi::Error::InvalidArgument) == EINVAL);

int main() {}
