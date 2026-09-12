#include <realtime/timing_statistics.hpp>

#include <cstdlib>
#include <limits>

namespace realtime {
namespace {

using Rep = Duration::rep;

[[nodiscard]] bool checked_difference(
    Rep later,
    Rep earlier,
    Duration& result) noexcept {
  if (later < earlier ||
      (earlier < 0 && later > std::numeric_limits<Rep>::max() + earlier)) {
    return false;
  }
  result = Duration{later - earlier};
  return true;
}

[[nodiscard]] bool checked_add(Rep left, Rep right, Rep& result) noexcept {
  if ((right > 0 && left > std::numeric_limits<Rep>::max() - right) ||
      (right < 0 && left < std::numeric_limits<Rep>::min() - right)) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] std::uint64_t saturating_add(
    std::uint64_t value,
    std::uint64_t increment,
    bool& saturated) noexcept {
  if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
    saturated = true;
    return std::numeric_limits<std::uint64_t>::max();
  }
  return value + increment;
}

}  // namespace

void TimingStatistics::add_sample(
    TimingStatistics::DurationAccumulator& accumulator,
    std::uint64_t sample_count,
    Duration sample,
    bool& saturated) noexcept {
  if (sample_count == 0) {
    accumulator.minimum = sample;
    accumulator.maximum = sample;
  } else {
    if (sample < accumulator.minimum) {
      accumulator.minimum = sample;
    }
    if (sample > accumulator.maximum) {
      accumulator.maximum = sample;
    }
  }
  accumulator.total_nanoseconds = saturating_add(
      accumulator.total_nanoseconds, static_cast<std::uint64_t>(sample.count()), saturated);
}

DurationSummary TimingStatistics::summary(
    const TimingStatistics::DurationAccumulator& accumulator,
    std::uint64_t sample_count) noexcept {
  if (sample_count == 0) {
    return {};
  }

  const std::uint64_t mean = accumulator.total_nanoseconds / sample_count;
  if (mean > static_cast<std::uint64_t>(std::numeric_limits<Rep>::max())) {
    std::abort();
  }
  return {accumulator.minimum, accumulator.maximum, Duration{static_cast<Rep>(mean)}};
}

bool TimingStatistics::try_observe(
    const PeriodicWaitResult& wait,
    TimePoint completion_time) noexcept {
  if (wait.error || wait.period <= Duration::zero()) {
    return false;
  }

  Duration release_latency{};
  Duration execution_time{};
  Rep next_deadline_count{};
  if (!checked_difference(
          wait.wake_time.time_since_epoch().count(),
          wait.scheduled_time.time_since_epoch().count(),
          release_latency) ||
      release_latency != wait.lateness ||
      !checked_difference(
          completion_time.time_since_epoch().count(),
          wait.wake_time.time_since_epoch().count(),
          execution_time) ||
      !checked_add(
          wait.scheduled_time.time_since_epoch().count(),
          wait.period.count(),
          next_deadline_count)) {
    return false;
  }

  const TimePoint next_deadline{Duration{next_deadline_count}};
  Duration overrun{};
  if (completion_time > next_deadline &&
      !checked_difference(
          completion_time.time_since_epoch().count(),
          next_deadline.time_since_epoch().count(),
          overrun)) {
    return false;
  }

  Duration jitter{};
  if (cycle_count_ != 0) {
    jitter = release_latency >= previous_release_latency_
                 ? release_latency - previous_release_latency_
                 : previous_release_latency_ - release_latency;
  }

  bool saturated = saturated_;
  add_sample(release_latency_, cycle_count_, release_latency, saturated);
  add_sample(execution_time_, cycle_count_, execution_time, saturated);
  if (cycle_count_ != 0) {
    add_sample(jitter_, jitter_sample_count_, jitter, saturated);
    jitter_sample_count_ = saturating_add(jitter_sample_count_, 1, saturated);
  }
  if (overrun > Duration::zero()) {
    overrun_count_ = saturating_add(overrun_count_, 1, saturated);
    if (overrun > maximum_overrun_) {
      maximum_overrun_ = overrun;
    }
  }
  missed_release_count_ = saturating_add(
      missed_release_count_, wait.missed_releases, saturated);
  cycle_count_ = saturating_add(cycle_count_, 1, saturated);
  previous_release_latency_ = release_latency;
  saturated_ = saturated;
  return true;
}

TimingSnapshot TimingStatistics::snapshot() const noexcept {
  TimingSnapshot result{};
  result.cycle_count = cycle_count_;
  result.release_latency = summary(release_latency_, cycle_count_);
  result.execution_time = summary(execution_time_, cycle_count_);
  result.jitter_sample_count = jitter_sample_count_;
  result.jitter = summary(jitter_, jitter_sample_count_);
  result.overrun_count = overrun_count_;
  result.maximum_overrun = maximum_overrun_;
  result.missed_release_count = missed_release_count_;
  result.saturated = saturated_;
  return result;
}

void TimingStatistics::reset() noexcept {
  cycle_count_ = 0;
  release_latency_ = {};
  execution_time_ = {};
  jitter_sample_count_ = 0;
  jitter_ = {};
  previous_release_latency_ = {};
  overrun_count_ = 0;
  maximum_overrun_ = {};
  missed_release_count_ = 0;
  saturated_ = false;
}

}  // namespace realtime
