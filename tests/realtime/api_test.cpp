#include <realtime/current_thread.hpp>
#include <realtime/periodic_schedule.hpp>
#include <realtime/process_memory.hpp>
#include <type_traits>
static_assert(std::is_same_v<
              decltype(realtime::lock_process_memory()), hardware::Result<void, realtime::Error>>);
static_assert(std::is_same_v<
              decltype(std::declval<realtime::PeriodicSchedule&>().wait_next()),
              hardware::Result<realtime::PeriodicWait, realtime::Error>>);
int main() {}
