#include <realtime/timing_statistics.hpp>

namespace {

[[maybe_unused]] void compile_timing_statistics_header() {
  realtime::TimingStatistics statistics;
  const auto snapshot = statistics.snapshot();
  static_cast<void>(snapshot);
}

}  // namespace
