#pragma once

#include <realtime/periodic_schedule.hpp>

#include <cstdint>
#include <type_traits>

namespace realtime {

struct DurationSummary final {
  Duration minimum{};
  Duration maximum{};
  Duration mean{};
};

struct TimingSnapshot final {
  std::uint64_t cycle_count{0};

  DurationSummary release_latency{};
  DurationSummary execution_time{};

  std::uint64_t jitter_sample_count{0};
  DurationSummary jitter{};

  std::uint64_t overrun_count{0};
  Duration maximum_overrun{};

  std::uint64_t missed_release_count{0};
  bool saturated{false};
};

static_assert(std::is_trivially_copy_constructible_v<DurationSummary>);
static_assert(std::is_trivially_copy_assignable_v<DurationSummary>);
static_assert(std::is_trivially_destructible_v<DurationSummary>);
static_assert(std::is_nothrow_copy_constructible_v<DurationSummary>);
static_assert(std::is_nothrow_copy_assignable_v<DurationSummary>);
static_assert(std::is_trivially_copy_constructible_v<TimingSnapshot>);
static_assert(std::is_trivially_copy_assignable_v<TimingSnapshot>);
static_assert(std::is_trivially_destructible_v<TimingSnapshot>);
static_assert(std::is_nothrow_copy_constructible_v<TimingSnapshot>);
static_assert(std::is_nothrow_copy_assignable_v<TimingSnapshot>);

class TimingStatistics final {
 public:
  TimingStatistics() noexcept = default;

  TimingStatistics(const TimingStatistics&) = delete;
  TimingStatistics& operator=(const TimingStatistics&) = delete;
  TimingStatistics(TimingStatistics&&) = delete;
  TimingStatistics& operator=(TimingStatistics&&) = delete;

  [[nodiscard]] bool try_observe(
      const PeriodicWaitResult& wait,
      TimePoint completion_time) noexcept;

  [[nodiscard]] TimingSnapshot snapshot() const noexcept;
  void reset() noexcept;

 private:
  struct DurationAccumulator {
    std::uint64_t total_nanoseconds{0};
    Duration minimum{};
    Duration maximum{};
  };

  std::uint64_t cycle_count_{0};
  DurationAccumulator release_latency_{};
  DurationAccumulator execution_time_{};

  std::uint64_t jitter_sample_count_{0};
  DurationAccumulator jitter_{};
  Duration previous_release_latency_{};

  std::uint64_t overrun_count_{0};
  Duration maximum_overrun_{};

  std::uint64_t missed_release_count_{0};
  bool saturated_{false};

  static void add_sample(
      DurationAccumulator& accumulator,
      std::uint64_t sample_count,
      Duration sample,
      bool& saturated) noexcept;
  [[nodiscard]] static DurationSummary summary(
      const DurationAccumulator& accumulator,
      std::uint64_t sample_count) noexcept;
};

}  // namespace realtime
