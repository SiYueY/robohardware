#include <spi/error.hpp>

#include <type_traits>

static_assert(std::is_error_code_enum_v<spi::Error>);
