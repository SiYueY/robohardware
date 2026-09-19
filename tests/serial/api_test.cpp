#include <serial/port.hpp>
#include <serial/tool.hpp>
#include <type_traits>
static_assert(std::is_same_v<
              decltype(std::declval<serial::Port&>().read(nullptr, 0)),
              hardware::Result<std::size_t, serial::Error>>);
static_assert(std::is_same_v<
              decltype(serial::list_ports()),
              hardware::Result<std::vector<serial::PortInfo>, serial::Error>>);
static_assert(std::is_move_constructible_v<serial::Port>);
static_assert(std::is_nothrow_move_constructible_v<serial::Port>);
static_assert(!std::is_move_assignable_v<serial::Port>);
static_assert(!std::is_copy_constructible_v<serial::Port>);
static_assert(!std::is_copy_assignable_v<serial::Port>);
int main() {}
