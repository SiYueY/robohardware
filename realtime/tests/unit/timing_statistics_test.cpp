#include <realtime/timing_statistics.hpp>

#include <iostream>
#include <limits>
#include <system_error>

namespace {

using realtime::Duration;
using realtime::PeriodicWaitResult;
using realtime::TimePoint;
using realtime::TimingStatistics;

PeriodicWaitResult observation(
    std::int64_t scheduled,
    std::int64_t wake,
    std::int64_t period,
    std::uint64_t missed) {
  PeriodicWaitResult result{};
  result.scheduled_time = TimePoint{Duration{scheduled}};
  result.wake_time = TimePoint{Duration{wake}};
  result.period = Duration{period};
  result.lateness = Duration{wake - scheduled};
  result.missed_releases = missed;
  return result;
}

bool expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "timing statistics test failed: " << message << '\n';
  }
  return condition;
}

bool same_snapshot(
    const realtime::TimingSnapshot& left,
    const realtime::TimingSnapshot& right) {
  return left.cycle_count == right.cycle_count &&
         left.release_latency.minimum == right.release_latency.minimum &&
         left.release_latency.maximum == right.release_latency.maximum &&
         left.release_latency.mean == right.release_latency.mean &&
         left.execution_time.minimum == right.execution_time.minimum &&
         left.execution_time.maximum == right.execution_time.maximum &&
         left.execution_time.mean == right.execution_time.mean &&
         left.jitter_sample_count == right.jitter_sample_count &&
         left.jitter.minimum == right.jitter.minimum && left.jitter.maximum == right.jitter.maximum &&
         left.jitter.mean == right.jitter.mean && left.overrun_count == right.overrun_count &&
         left.maximum_overrun == right.maximum_overrun &&
         left.missed_release_count == right.missed_release_count && left.saturated == right.saturated;
}

}  // namespace

int main() {
  TimingStatistics statistics;
  bool passed = expect(statistics.try_observe(
      observation(100, 110, 20, 1), TimePoint{Duration{125}}),
      "first valid observation was rejected");
  passed &= expect(statistics.try_observe(
      observation(120, 135, 20, 0), TimePoint{Duration{140}}),
      "second valid observation was rejected");

  const auto snapshot = statistics.snapshot();
  passed &= expect(snapshot.cycle_count == 2, "cycle count is wrong");
  passed &= expect(snapshot.release_latency.minimum == Duration{10} &&
                       snapshot.release_latency.maximum == Duration{15} &&
                       snapshot.release_latency.mean == Duration{12},
                   "release latency summary is wrong");
  passed &= expect(snapshot.execution_time.minimum == Duration{5} &&
                       snapshot.execution_time.maximum == Duration{15} &&
                       snapshot.execution_time.mean == Duration{10},
                   "execution time summary is wrong");
  passed &= expect(snapshot.jitter_sample_count == 1 &&
                       snapshot.jitter.minimum == Duration{5} &&
                       snapshot.jitter.maximum == Duration{5} &&
                       snapshot.jitter.mean == Duration{5},
                   "jitter summary is wrong");
  passed &= expect(snapshot.overrun_count == 1 &&
                       snapshot.maximum_overrun == Duration{5},
                   "overrun summary is wrong");
  passed &= expect(snapshot.missed_release_count == 1, "missed release count is wrong");

  PeriodicWaitResult failed_wait = observation(140, 140, 20, 0);
  failed_wait.error = std::make_error_code(std::errc::io_error);
  PeriodicWaitResult mismatched_lateness = observation(140, 150, 20, 0);
  mismatched_lateness.lateness = Duration{9};
  const auto maximum_time =
      std::numeric_limits<Duration::rep>::max();
  const auto deadline_overflow = observation(maximum_time, maximum_time, 1, 0);

  passed &= expect(!statistics.try_observe(failed_wait, TimePoint{Duration{145}}),
                   "failed wait observation was accepted");
  passed &= expect(!statistics.try_observe(
                       mismatched_lateness, TimePoint{Duration{155}}),
                   "mismatched lateness observation was accepted");
  passed &= expect(!statistics.try_observe(
                       observation(140, 150, 20, 0), TimePoint{Duration{145}}),
                   "completion before wake observation was accepted");
  passed &= expect(!statistics.try_observe(
                       deadline_overflow, TimePoint{Duration{maximum_time}}),
                   "next-deadline overflow observation was accepted");
  passed &= expect(same_snapshot(snapshot, statistics.snapshot()),
                   "invalid observation modified statistics state");

  statistics.reset();
  const auto reset_snapshot = statistics.snapshot();
  passed &= expect(reset_snapshot.cycle_count == 0 &&
                       reset_snapshot.jitter_sample_count == 0 &&
                       reset_snapshot.overrun_count == 0 &&
                       reset_snapshot.missed_release_count == 0 &&
                       !reset_snapshot.saturated,
                   "reset did not restore the default snapshot");
  passed &= expect(statistics.try_observe(
                       observation(200, 200, 20, 0), TimePoint{Duration{210}}),
                   "valid observation after reset was rejected");
  passed &= expect(statistics.snapshot().jitter_sample_count == 0,
                   "reset did not clear the previous jitter sample");

  return passed ? 0 : 1;
}
