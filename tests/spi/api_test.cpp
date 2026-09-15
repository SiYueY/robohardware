#include <spi/device.hpp>
#include <type_traits>
static_assert(std::is_same_v<
              decltype(std::declval<spi::Device&>().close()), hardware::Result<void, spi::Error>>);
int main() {}
