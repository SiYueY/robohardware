#pragma once

#include <atomic>
#include <cerrno>
#include <pthread.h>
#include <sys/mman.h>

namespace realtime::detail {
struct TestHooks {
    std::atomic<int> mlockall_errno{0};
    std::atomic<int> setsched_rc{0};
    std::atomic<int> setaffinity_rc{0};
    std::atomic<int> clock_nanosleep_rc{0};
};
inline TestHooks test_hooks{};
inline int memory_lock() noexcept {
    const int forced = test_hooks.mlockall_errno.load();
    if (forced != 0) {
        errno = forced;
        return -1;
    }
    return mlockall(MCL_CURRENT | MCL_FUTURE);
}
inline int set_scheduler(pthread_t thread, int policy, const sched_param* param) noexcept {
    const int forced = test_hooks.setsched_rc.load();
    return forced != 0 ? forced : pthread_setschedparam(thread, policy, param);
}
inline int set_affinity(pthread_t thread, std::size_t size, const cpu_set_t* set) noexcept {
    const int forced = test_hooks.setaffinity_rc.load();
    return forced != 0 ? forced : pthread_setaffinity_np(thread, size, set);
}
inline int sleep_until(const timespec* target) noexcept {
    const int forced = test_hooks.clock_nanosleep_rc.load();
    return forced != 0 ? forced : clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, target, nullptr);
}
}  // namespace realtime::detail
