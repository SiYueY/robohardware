#include <serial/port.hpp>
#include <serial/tool.hpp>
#include <type_traits>
static_assert(std::is_same_v<
              decltype(std::declval<serial::Port&>().read(nullptr, 0)),
              hardware::Result<std::size_t, serial::Error>>);
static_assert(std::is_same_v<
              decltype(serial::list_ports()),
              hardware::Result<std::vector<serial::PortInfo>, serial::Error>>);
int main() {}
