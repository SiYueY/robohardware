#include <realtime/periodic_schedule.hpp>

#include <iostream>
#include <system_error>

namespace {

using realtime::Duration;
using realtime::MissedPeriodPolicy;
using realtime::PeriodicSchedule;
using realtime::TimePoint;

bool expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "periodic schedule configuration test failed: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  PeriodicSchedule schedule;
  bool passed = expect(!schedule.is_configured(), "default schedule is configured");

  const auto invalid_period = schedule.configure(
      TimePoint{Duration{1'000}}, Duration::zero(), MissedPeriodPolicy::CatchUp);
  passed &= expect(invalid_period == std::errc::invalid_argument,
                   "zero period did not return invalid_argument");
  passed &= expect(!schedule.is_configured(),
                   "invalid first configuration changed lifecycle state");

  const auto configured = schedule.configure(
      TimePoint{Duration{1'000}}, Duration{100}, MissedPeriodPolicy::SkipMissed);
  passed &= expect(!configured, "valid configuration returned an error");
  passed &= expect(schedule.is_configured(), "valid configuration did not configure schedule");

  const auto invalid_reconfiguration = schedule.configure(
      TimePoint{Duration{2'000}}, Duration{-1}, MissedPeriodPolicy::CatchUp);
  passed &= expect(invalid_reconfiguration == std::errc::invalid_argument,
                   "negative period did not return invalid_argument");
  passed &= expect(schedule.is_configured(),
                   "invalid reconfiguration cleared configured state");

  return passed ? 0 : 1;
}
