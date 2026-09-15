#include <serial/port.hpp>
#include <type_traits>
static_assert(
    std::is_same_v<
        decltype(std::declval<serial::Port&>().read(nullptr, 0, serial::Timeout::immediate())),
        hardware::Result<std::size_t, serial::Error>>);
int main() {}
