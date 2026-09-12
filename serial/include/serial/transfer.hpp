#pragma once

#include <cstddef>
#include <system_error>

namespace serial {

struct TransferResult final {
  std::size_t bytes_transferred;
  std::error_code error;
};

}  // namespace serial
