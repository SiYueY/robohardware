# Realtime

`realtime` is a small Linux/C++17 library for periodic tasks and real-time
thread setup. It depends on pthread and the Linux scheduling, affinity, memory
locking, and monotonic clock APIs.

## Build

Add the module to a CMake project and link its stable target:

```cmake
add_subdirectory(realtime)
target_link_libraries(my_target PRIVATE realtime::realtime)
```

The public headers retain their existing include paths:

```cpp
#include <realtime/periodic_task.hpp>

realtime::PeriodicTask::Options options;
options.period = std::chrono::milliseconds(2);
realtime::PeriodicTask task(options);
```

See [the realtime design documentation](../docs/realtime.md) for contracts,
runtime behavior, and API guidance.
