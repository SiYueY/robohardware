#include <realtime/buffer.hpp>
#include <realtime/clock.hpp>
#include <realtime/current_thread.hpp>
#include <realtime/periodic_schedule.hpp>
#include <realtime/process_memory.hpp>
#include <realtime/queue.hpp>
#include <realtime/timing_statistics.hpp>

int main() {
  realtime::Queue<int, 1> queue;
  return realtime::Clock::now().time_since_epoch().count() >= 0 &&
                 queue.try_push(1)
             ? 0
             : 1;
}
