#include "realtime/periodic_task.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    realtime::PeriodicTaskOptions options;
    options.period = 2ms;
    options.scheduler = {realtime::SchedulingPolicy::Other, 0};
    options.memory.lock_memory = false;
    options.mode = realtime::RealtimeMode::BestEffort;

    realtime::PeriodicTask task(options);
    const auto started = task.start([](const realtime::CycleInfo&) noexcept {});
    if (!started) return 1;
    std::this_thread::sleep_for(5s);
    const auto stopped = task.stop();
    const auto stats = task.stats();
    std::printf(
        "500 Hz functional benchmark: cycles=%llu misses=%llu missed_periods=%llu "
        "max_lateness_ns=%lld max_execution_ns=%lld\n",
        static_cast<unsigned long long>(stats.cycles),
        static_cast<unsigned long long>(stats.deadline_misses),
        static_cast<unsigned long long>(stats.missed_periods),
        static_cast<long long>(stats.max_lateness.count()),
        static_cast<long long>(stats.max_execution.count()));
    return stopped ? 0 : 1;
}
