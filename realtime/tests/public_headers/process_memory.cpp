#include <realtime/process_memory.hpp>

namespace {

[[maybe_unused]] void compile_process_memory_header() {
  const auto error = realtime::lock_process_memory();
  static_cast<void>(error);
}

}  // namespace
