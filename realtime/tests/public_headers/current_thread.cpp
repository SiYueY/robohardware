#include <realtime/current_thread.hpp>

namespace {

[[maybe_unused]] void compile_current_thread_header() {
  const auto policy = realtime::SchedulingPolicy::Normal;
  static_cast<void>(policy);
}

}  // namespace
