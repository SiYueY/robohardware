#include <realtime/periodic_schedule.hpp>

namespace {

[[maybe_unused]] void compile_periodic_schedule_header() {
  realtime::PeriodicSchedule schedule;
  static_cast<void>(schedule.is_configured());
}

}  // namespace
