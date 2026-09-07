# RoboHardware Realtime 模块设计

## 1. 模块定位与设计原则

`realtime` 是 RoboHardware 中面向 Linux 的 C++17 实时基础模块。

它提供：

```text
Clock
Realtime Scheduling
CPU Affinity
Memory Lock
PeriodicTask
RT / non-RT Buffer
Bounded Queue
Status / Stats
Error
```

它不负责：

```text
CAN
Serial
CANopen
CiA402
Device
System Lifecycle
Safety Policy
Robot Controller
ROS2
```

模块目标是：

> 提供一组轻量、确定、可独立复用的 Linux realtime primitive，使 RoboHardware 的 System 和 Device 层能够可靠构建 500 Hz 等实时任务，而不是构建新的通用 runtime framework。

### 平台范围

V1 明确：

```text
Language:
    C++17

Platform:
    Linux only

Development:
    Generic Linux kernel

Formal realtime validation:
    PREEMPT_RT
```

V1 不承诺：

```text
Windows
macOS
BSD
Generic POSIX portability
```

因此可以直接使用：

```text
pthread_setschedparam
pthread_setaffinity_np
mlockall
clock_gettime
clock_nanosleep
CLOCK_MONOTONIC
```

而不提前引入跨平台 backend abstraction。

### 核心原则

1. RT fast path 不进行动态内存分配。
2. RT fast path 不进行不可预测阻塞。
3. RT fast path 不依赖异常控制流。
4. 周期任务采用 absolute deadline。
5. Overrun 跳过已错过周期，不进行 catch-up。
6. Realtime 只报告时间与执行事实，不决定机器人安全动作。
7. RT / non-RT 数据结构必须 bounded。
8. Buffer 与 Queue 按数据语义区分。
9. 所有 realtime 时间属于统一 `CLOCK_MONOTONIC` time domain。
10. 对外时间类型兼容 `std::chrono`。
11. Required 模式禁止静默降级。
12. 并发 primitive 必须符合 C++17 memory model，不依赖特定 CPU 的偶然行为。
13. 接口预留，实现推迟。
14. 不建设 Executor、TaskGraph、MessageBus 等通用 runtime。
15. 所有未来扩展必须复用现有语义，而不是重新设计第二套 realtime API。

主要参考：

```text
cactus-rt
    Linux realtime thread
    scheduling
    affinity
    memory locking
    statistics

ros-controls/realtime_tools
    RT/non-RT exchange
    latest-value semantics
    realtime data structures

ros2-realtime-examples
    Linux realtime setup
    PREEMPT_RT practices

Linux / C++17
    pthread
    sched
    clock_nanosleep
    memory model
```

开源项目提供工程实现参考，Linux API 和 C++ memory model 是最终语义依据。

---

## 2. 模块结构与核心接口

推荐目录：

```text
include/realtime/
├── clock.hpp
├── scheduler.hpp
├── affinity.hpp
├── memory.hpp
├── periodic_task.hpp
├── buffer.hpp
├── queue.hpp
├── status.hpp
├── statistics.hpp
└── error.hpp

src/realtime/
├── clock.cpp
├── scheduler.cpp
├── affinity.cpp
├── memory.cpp
├── periodic_task.cpp
├── statistics.cpp
└── error.cpp
```

V1 不公开：

```text
thread.hpp
```

因为当前实际需要的 realtime thread 语义已经由：

```text
PeriodicTask
```

覆盖。

如果未来出现明确的：

```text
non-periodic realtime worker
```

需求，再增加 `realtime::Thread`。

但未来如果引入 `realtime::Thread`，必须复用：

```text
SchedulerConfig
AffinityConfig
MemoryConfig
RealtimeMode
Status
Error
```

等已有语义，不允许重新建立第二套 realtime thread 配置和生命周期模型。

namespace：

```cpp
namespace realtime {}
```

建议 V1 核心公共 API：

```cpp
namespace realtime {

class Clock;

using TimePoint = Clock::time_point;
using Duration = Clock::duration;

enum class RealtimeMode {
    Required,
    BestEffort,
};

enum class SchedulingPolicy {
    Other,
    Fifo,
};

struct SchedulerConfig;
struct AffinityConfig;
struct MemoryConfig;

struct PeriodicTaskOptions;
struct CycleInfo;

struct Status;
struct Stats;

class PeriodicTask;

template <typename T>
class Buffer;

template <typename T, std::size_t N>
class Queue;

}
```

总体关系：

```text
                  System / Device
                        │
                        ▼
                  PeriodicTask
                        │
       ┌────────────────┼────────────────┐
       ▼                ▼                ▼
     Clock          Scheduler          Stats
                        │
                Affinity / Memory
                        │
                        ▼
                   Linux Thread


               RT / non-RT data
                        │
             ┌──────────┴──────────┐
             ▼                     ▼
         Buffer<T>             Queue<T,N>
       latest-value          ordered-events
```

---

## 3. 时间、调度与周期执行

### 3.1 Clock

Realtime 不直接使用：

```cpp
using Clock = std::chrono::steady_clock;
```

作为底层事实时钟，因为 C++ 标准并不保证它与 Linux `CLOCK_MONOTONIC` 使用相同实现和 epoch。

V1 定义明确绑定 Linux `CLOCK_MONOTONIC` 的 chrono-compatible Clock：

```cpp
namespace realtime {

class Clock {
public:
    using rep        = std::int64_t;
    using period     = std::nano;
    using duration   = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<Clock>;

    static constexpr bool is_steady = true;

    static time_point now() noexcept;
};

using TimePoint = Clock::time_point;
using Duration = Clock::duration;

}
```

`Clock::now()` 底层使用：

```text
clock_gettime(CLOCK_MONOTONIC)
```

并转换成：

```text
nanoseconds since CLOCK_MONOTONIC epoch
```

这样同时满足：

```text
明确 Linux time domain
+
std::chrono duration/time_point 兼容
```

所有参与：

```text
period
deadline
control dt
timeout
freshness
lateness
execution time
statistics
```

的时间都必须使用该 Clock。

禁止：

```text
CLOCK_REALTIME
std::chrono::system_clock
wall clock
```

参与 realtime 控制逻辑。

`Clock::time_point` 与 Linux `timespec` 的转换属于内部实现细节，使用标准 chrono duration 转换保证纳秒精度。

---

### 3.2 Scheduler

V1 正式支持：

```text
SCHED_FIFO
SCHED_OTHER
```

其中：

```text
SCHED_FIFO
    realtime execution

SCHED_OTHER
    development / BestEffort fallback
```

定义：

```cpp
enum class SchedulingPolicy {
    Other,
    Fifo,
};

struct SchedulerConfig {
    SchedulingPolicy policy{SchedulingPolicy::Fifo};
    int priority{0};
};
```

底层：

```text
pthread_setschedparam()
```

V1 不支持：

```text
SCHED_RR
SCHED_DEADLINE
```

也不为这些调度策略提前设计复杂配置。

如果未来出现真实需求，再进行受控 API 演进。

---

### 3.3 CPU Affinity

V1 支持：

```text
no affinity
single CPU affinity
```

接口：

```cpp
struct AffinityConfig {
    std::optional<int> cpu;
};
```

底层：

```text
pthread_setaffinity_np()
```

不提供：

```text
NUMA framework
automatic core assignment
CPU topology manager
```

CPU isolation、IRQ affinity 等属于系统部署策略，不属于 realtime module API。

---

### 3.4 Memory Lock

封装：

```text
mlockall(MCL_CURRENT | MCL_FUTURE)
```

配置：

```cpp
struct MemoryConfig {
    bool lock_memory{true};
    std::size_t prefault_bytes{0};
};
```

可在 startup 阶段进行：

```text
stack prefault
preallocated memory prefault
```

目标：

> 降低 realtime runtime 中发生 page fault 的风险。

需要明确：

```text
mlockall != hard realtime guarantee
```

它只是实时运行条件之一。

---

### 3.5 RealtimeMode

定义：

```cpp
enum class RealtimeMode {
    Required,
    BestEffort,
};
```

#### Required

如果用户明确要求的：

```text
SCHED_FIFO
memory lock
CPU affinity
```

任一关键能力失败：

```text
PeriodicTask::start() fails
```

禁止静默退化。

#### BestEffort

允许部分 realtime 能力失败并继续运行，但：

```text
实际状态必须通过 Status 可见
```

---

### 3.6 Status

统一提供：

```cpp
struct Status {
    bool running{false};

    bool realtime_scheduling{false};
    bool memory_locked{false};
    bool affinity_applied{false};

    SchedulingPolicy policy{SchedulingPolicy::Other};
    int priority{0};
    std::optional<int> cpu;
};
```

System 可以直接聚合：

```text
realtime::Status
    ↓
robohardware::SystemStatus
```

而不依赖日志文本判断 realtime 是否生效。

---

### 3.7 PeriodicTask

`PeriodicTask` 是 V1 最核心的执行 primitive。

配置：

```cpp
struct PeriodicTaskOptions {
    Duration period;

    SchedulerConfig scheduler;
    AffinityConfig affinity;
    MemoryConfig memory;

    RealtimeMode mode{RealtimeMode::Required};
};
```

典型：

```cpp
realtime::PeriodicTask task(options);

auto result = task.start(
    [&](const realtime::CycleInfo& cycle) noexcept {
        // bounded realtime work
    });
```

callback 必须：

```text
bounded
noexcept
return within predictable time
```

---

### 3.8 Absolute Deadline

周期调度必须使用：

```text
CLOCK_MONOTONIC
+
clock_nanosleep(
    TIMER_ABSTIME
)
```

执行：

```text
T0
 ↓
deadline = T0 + period
 ↓
sleep until deadline
 ↓
capture wakeup time
 ↓
callback
 ↓
advance absolute deadline
```

禁止：

```cpp
std::this_thread::sleep_for(period);
```

避免：

```text
execution time
+
scheduler latency
+
relative sleep
```

逐周期累积形成 drift。

---

### 3.9 CycleInfo

定义：

```cpp
struct CycleInfo {
    std::uint64_t sequence;

    TimePoint scheduled_time;
    TimePoint wakeup_time;

    Duration lateness;
    std::uint32_t missed_periods;
};
```

语义固定：

```text
scheduled_time
    当前周期理论 deadline

wakeup_time
    当前 worker 实际获得执行机会的时间

lateness
    max(wakeup_time - scheduled_time, 0)

missed_periods
    floor(lateness / period)
```

即：

```cpp
lateness =
    wakeup_time > scheduled_time
        ? wakeup_time - scheduled_time
        : Duration::zero();

missed_periods =
    static_cast<std::uint32_t>(
        lateness / period);
```

例如：

```text
period = 2 ms

lateness = 1.9 ms
    missed_periods = 0

lateness = 2.0 ms
    missed_periods = 1

lateness = 4.1 ms
    missed_periods = 2
```

下一 deadline：

```text
scheduled_time
+
(missed_periods + 1) × period
```

例如：

```text
scheduled = 10 ms
wakeup    = 14.1 ms
period    = 2 ms

lateness = 4.1 ms
missed   = 2

next deadline = 16 ms
```

---

### 3.10 Overrun

V1 固定采用：

```text
SkipMissedPeriods
```

不提供可配置 `OverrunPolicy`。

逻辑：

```text
deadline miss
    ↓
calculate missed_periods
    ↓
skip elapsed periods
    ↓
align to next future absolute deadline
```

禁止：

```text
catch-up execution
```

Realtime 只报告：

```text
lateness
missed_periods
Stats
```

是否：

```text
Hold
Quick Stop
Disable
Fault
```

由 System/Safety 决定。

---

### 3.11 Callback 与 Stop

`PeriodicTask` 自己负责 stop 检查。

内部运行模型：

```cpp
while (!stop_requested_.load(std::memory_order_acquire)) {

    wait_until(deadline);

    if (stop_requested_.load(std::memory_order_acquire)) {
        break;
    }

    callback(cycle);

    advance_deadline();
}
```

普通 callback 不需要自行查询 stop token。

但是 callback 必须 bounded。

如果用户实现：

```cpp
while (true) {}
```

则 cooperative shutdown 无法终止正在执行的 callback。

Realtime V1 不使用：

```text
pthread_cancel
async callback termination
forced preemption
```

---

### 3.12 stop() Contract

V1：

```cpp
Result<void> stop();
```

定义为同步 stop：

```text
request stop
    ↓
wake / finish current wait
    ↓
allow current callback to return
    ↓
worker exits
    ↓
join
    ↓
stop() returns
```

因此：

> `stop()` 返回时 worker thread 已完全退出。

callback 所访问的资源必须至少存活到：

```text
stop() / join complete
```

以后如有实际需求，可拆成：

```text
request_stop()
join()
```

但 V1 不需要。

---

## 4. RT / Non-RT 数据交换

Realtime V1 只提供：

```text
Buffer<T>
Queue<T,N>
```

两个 primitive。

不提供：

```text
MessageBus
EventBus
Channel
Mailbox Framework
Generic IPC
```

### 4.1 Buffer<T>

语义：

> 单生产者、单消费者、只关心最新完整值。

V1 Contract：

```text
SPSC
latest-value
fixed/preallocated storage
non-blocking
runtime no allocation
initially empty
```

接口：

```cpp
template <typename T>
class Buffer {
public:
    bool write(const T& value) noexcept;
    bool read(T& value) const noexcept;
};
```

`read()`：

```text
false
    Buffer 创建后尚未 publish 任何 value

true
    value 包含最近一次完整 publish 的数据
```

不会自动发布：

```cpp
T{}
```

也不需要：

```cpp
is_initialized()
```

以避免重复状态查询。

---

### 4.2 Buffer 不负责 Freshness

Buffer 只表达：

```text
never published
published
```

它不表达：

```text
stale
timeout
sensor validity
freshness
```

这些语义由：

```text
T 自身
或
System / Device
```

负责。

---

### 4.3 Publication Memory Ordering

publication contract：

```text
Writer:
    complete payload write
          ↓
    release publication

Reader:
    acquire publication
          ↓
    read stable published slot
```

release/acquire 建立：

```text
payload writes
    happens-before
reader payload reads
```

不使用：

```text
atomic_signal_fence
compiler barrier
```

代替真正的跨线程同步。

---

### 4.4 Buffer Slot Ownership

真正的并发难点不是 publication ordering，而是：

```text
slot reuse
```

禁止 naive double buffer：

```text
Writer                       Reader

publish A
                             read A

publish B

rewrite A  ← race
                             still reading A
```

V1 Buffer 必须：

```text
固定多槽
明确 slot ownership
明确 publication generation/version
禁止 writer 重写 reader 仍可能访问的 slot
```

实现必须具备：

```text
documented ownership state machine
C++17 data-race-free reasoning
long-running concurrency tests
```

V1 实现策略固定为：

> **SPSC fixed multi-slot latest-value buffer with explicit sequence/version publication and slot ownership protection.**

这里已经足以指导实现。

具体原子变量布局，例如：

```text
generation/index packing
atomic<uint64_t>
per-slot sequence
reader ownership marker
```

属于代码实现细节，不在公共设计文档中继续固定。

不得实现未经证明的 naive double-buffer 变体。

实现应参考成熟项目，并通过并发测试验证。

---

### 4.5 Buffer 类型约束

`T` 应优先：

```text
fixed-size
trivially copyable where practical
no owning dynamic allocation
cheap to copy
```

不推荐：

```text
std::string
std::vector
std::map
std::unordered_map
dynamic ownership graph
```

进入 RT Buffer。

不设置：

```text
T <= 64 bytes
T <= cache line
```

之类硬限制。

大对象优化必须：

```text
benchmark first
```

而不是根据 cache-line 尺寸猜测。

同时明确：

> `Buffer<T>` 不是 System Runtime Snapshot 的唯一实现方式。

System 可以使用专用 frozen RuntimeStorage + stable View。

---

### 4.6 Queue<T,N>

语义：

> 单生产者、单消费者、有序、有界事件流。

V1 Contract：

```text
SPSC
bounded
fixed capacity
runtime no allocation
ordered FIFO
```

接口：

```cpp
template <typename T, std::size_t N>
class Queue {
public:
    bool try_push(const T& value) noexcept;
    bool try_pop(T& value) noexcept;
};
```

满：

```text
try_push() == false
```

空：

```text
try_pop() == false
```

Queue 不：

```text
block
resize
allocate
overwrite oldest item automatically
```

---

### 4.7 SPSC Ownership

一个 Queue 实例的：

```text
producer thread
consumer thread
```

必须在初始化阶段确定，并在 Runtime 内保持不变。

不允许：

```text
Device A ─┐
Device B ─┼→ same Queue::try_push()
Device C ─┘
```

如需要多个 producer：

```text
Device A → Queue A ─┐
Device B → Queue B ─┼→ System Aggregator
Device C → Queue C ─┘
```

或由上层 non-RT context 先序列化。

System 层负责这种多 producer 编排，Realtime 模块本身不升级成 MPSC。

V1 不实现：

```text
MPSC
MPMC
```

---

### 4.8 Buffer 与 Queue 的选择

依据数据语义：

```text
只关心最新状态
    → Buffer

每个事件都有意义
    → Queue
```

不要因为：

```text
数据对象较大
```

就机械将 Buffer 替换成 Queue。

---

## 5. RT 约束、状态、错误与测试

### 5.1 RT Fast Path

禁止：

```text
heap allocation
free/delete
blocking mutex
condition_variable wait
filesystem I/O
blocking network I/O
formatted synchronous logging
configuration parsing
dynamic plugin loading
exception as normal flow
```

运行模型：

```text
configure
 ↓
allocate
 ↓
precompute
 ↓
freeze
 ↓
run
```

核心 RT API 推荐：

```cpp
noexcept
```

---

### 5.2 Mutex

并非模块完全禁止 mutex。

允许用于：

```text
configuration
startup
shutdown
non-RT control path
```

禁止的是：

> deadline-sensitive RT fast path 中不可预测地等待 mutex。

---

### 5.3 Error

Startup/non-RT API 使用轻量错误：

```cpp
enum class ErrorCode {
    InvalidArgument,
    PermissionDenied,
    SchedulingFailed,
    AffinityFailed,
    MemoryLockFailed,
    ThreadCreateFailed,
    ClockError,
    InvalidState,
};
```

系统原生错误来源必须显式区分：

```cpp
enum class NativeErrorDomain : std::uint8_t {
    None,
    Errno,
    Pthread,
};
```

错误：

```cpp
struct Error {
    ErrorCode code;
    NativeErrorDomain native_domain{NativeErrorDomain::None};
    int native_code{0};
};
```

原因是 Linux API 存在两种常见错误语义。

#### syscall 风格

例如：

```text
mlockall
clock_gettime
```

通常：

```text
return -1
errno contains error
```

此时：

```cpp
native_domain = NativeErrorDomain::Errno;
native_code = errno;
```

#### pthread 风格

例如：

```text
pthread_setschedparam
pthread_setaffinity_np
pthread_create
```

通常：

```text
return error number directly
```

此时：

```cpp
native_domain = NativeErrorDomain::Pthread;
native_code = rc;
```

不得混淆两种来源。

Realtime 只保留底层错误事实，不转换成：

```text
SystemFault
SafetyAction
RobotFault
```

---

### 5.4 RT Fast Path Error

RT primitive 使用：

```text
bool
small enum
```

例如：

```cpp
enum class QueueStatus {
    Ok,
    Empty,
    Full,
};
```

避免在 RT path 构造复杂 Error/Result。

---

### 5.5 Stats

长期统计：

```cpp
struct Stats {
    std::uint64_t cycles{0};
    std::uint64_t deadline_misses{0};
    std::uint64_t missed_periods{0};

    Duration min_lateness{};
    Duration max_lateness{};

    Duration min_execution{};
    Duration max_execution{};
};
```

职责：

```text
CycleInfo
    当前周期事实

Stats
    长期聚合事实
```

System 可读取：

```text
Status
Stats
```

用于整体状态聚合。

---

### 5.6 高频 Trace

如果需要完整周期样本：

```cpp
struct CycleSample {
    std::uint64_t sequence;
    std::int64_t lateness_ns;
    std::int64_t execution_ns;
    std::uint32_t flags;
};
```

使用：

```text
RT
 ↓
Queue<CycleSample,N>
 ↓
non-RT collector
```

Realtime 不实现：

```text
CSV
Prometheus
InfluxDB
ROS publisher
UI
```

---

### 5.7 Ownership

callback 使用的所有对象：

```text
start 前创建
 ↓
Runtime 中冻结
 ↓
worker 生命周期内保持有效
 ↓
stop/join 完成
 ↓
才能释放
```

禁止：

```text
worker still running
       ↓
callback dependency destroyed
```

Realtime 模块不通过大量 `shared_ptr` 隐藏不正确的 ownership。

---

### 5.8 Testing

测试关注四类问题：

```text
Unit / Functional
Concurrency Correctness
PREEMPT_RT Performance
Long-running Stress
```

#### Unit / Functional

覆盖：

```text
Clock conversion
Scheduler validation
Affinity validation
Memory lock failure
Required failure
BestEffort fallback

PeriodicTask start/stop
CycleInfo calculation
missed-period calculation
absolute deadline realignment

Buffer empty/read/write
Queue empty/full/wrap-around

Status
Stats
Error native domain
```

---

### 5.9 Concurrency Tests

Buffer：

```text
single writer
single reader
millions / hundreds of millions operations
sequence verification
payload checksum
slot reuse stress
torn-read detection
```

Queue：

```text
wrap-around
full/empty transition
sequence continuity
producer/consumer speed imbalance
```

构建：

```text
-O0
-O2
-O3
```

并使用：

```text
ThreadSanitizer
AddressSanitizer
UndefinedBehaviorSanitizer
```

其中：

> TSan 可以发现实际 data race，但不能替代 C++ memory-model correctness proof。

Sanitizer 测试与 realtime latency benchmark 必须分开，因为 sanitizer 会显著改变时间行为。

---

### 5.10 PREEMPT_RT Validation

目标：

```text
500 Hz
period = 2 ms
```

测量：

```text
wakeup lateness
callback execution duration
deadline misses
missed periods
stop latency
```

建议：

```text
1 h development soak
24 h release soak
```

并施加：

```text
CPU stress
memory pressure
disk I/O
network load
background tasks
logging load
```

工具：

```text
cyclictest
trace-cmd
ftrace
perf sched
```

---

### 5.11 性能目标

早期工程参考：

```text
period:
    2 ms

deadline misses:
    0 under validated workload

p99 wakeup lateness:
    < 100 us

max callback execution:
    < 1 ms
```

这些是项目初期 target，不是公共 API guarantee。

最终指标由：

```text
CPU
kernel
PREEMPT_RT version
CPU isolation
IRQ load
application workload
```

实测确定。

---

### 5.12 CI

至少提供：

```text
Normal Build
Unit Tests

Realtime no-exceptions Build

ASan / UBSan

TSan / concurrency stress

PREEMPT_RT validation
    独立硬件/环境执行
```

`-fno-exceptions` 只作用于：

```text
RoboHardware::Realtime
```

独立 CI profile。

禁止全局：

```cmake
add_compile_options(-fno-exceptions)
```

影响其他 target。

正常 build 不要求关闭异常。

该 profile 的目标只是验证：

> realtime 模块内部不依赖 throw/catch 机制。

---

## 6. V1 范围、参考实现与架构不变量

### V1 MUST

必须实现：

```text
CLOCK_MONOTONIC-backed chrono-compatible Clock

SCHED_FIFO
SCHED_OTHER

CPU affinity

Memory lock

RealtimeMode
    Required
    BestEffort

PeriodicTask

absolute deadline

CycleInfo

explicit lateness/missed-period semantics

skip missed periods

cooperative synchronous stop

SPSC Buffer<T>
    fixed multi-slot
    latest-value
    initially empty
    explicit slot ownership

SPSC Queue<T,N>
    bounded
    fixed capacity

Status
Stats

ErrorCode
NativeErrorDomain
Error

unit tests
concurrency tests
basic benchmark
```

### SHOULD

建议实现：

```text
memory prefault helper

CycleSample trace support

PREEMPT_RT automated soak

additional runtime diagnostics
```

### MAY

后续按真实需求增加：

```text
SCHED_RR
SCHED_DEADLINE

generic realtime::Thread

MPSC / MPMC Queue

NUMA awareness

shared-memory exchange

advanced tracing

CPU isolation helper

kernel tuning helper

realtime allocator
```

未来如果增加：

```text
realtime::Thread
```

必须复用现有：

```text
SchedulerConfig
AffinityConfig
MemoryConfig
RealtimeMode
Status
Error
```

不得重新设计第二套线程运行语义。

明确不提前建设：

```text
Generic Executor
Task Graph
Message Bus
Coroutine Runtime
Realtime Framework
Scheduler Framework
Realtime Application Framework
```

原则：

> Define the boundary now. Implement the mechanism only when a real requirement appears.

---

### 参考实现

#### cactus-rt

参考：

```text
thread scheduling
SCHED_FIFO
affinity
memory locking
periodic execution
statistics
```

不复制大型 runtime/application framework。

#### realtime_tools

参考：

```text
RT/non-RT exchange
latest-value semantics
mature realtime buffer ideas
```

重点参考并发 ownership，而不是机械复制其 API 或具体实现。

#### ros2-realtime-examples

参考：

```text
Linux RT configuration
permissions
mlockall
PREEMPT_RT deployment
```

不引入 ROS2 dependency。

#### Linux / C++17

最终行为依据：

```text
Linux scheduler semantics
CLOCK_MONOTONIC
pthread API
C++17 memory model
```

---

### 架构不变量

Realtime 长期必须满足：

1. `realtime` 不依赖 System、Device、CAN、Serial、CANopen、ROS2。
2. V1 为 Linux-only。
3. `Clock` 明确基于 `CLOCK_MONOTONIC`。
4. `Clock` 对外兼容 `std::chrono`。
5. 所有 RT 时间属于同一 monotonic time domain。
6. PeriodicTask 使用 absolute deadline。
7. 不使用 relative `sleep_for(period)` 实现周期。
8. `lateness = max(wakeup - scheduled, 0)`。
9. `missed_periods = floor(lateness / period)`。
10. Overrun 跳过已错过周期。
11. 禁止 catch-up storm。
12. Realtime 只报告 overrun，不决定机器人 Safety。
13. callback 必须 bounded。
14. 不异步抢占正在执行 callback。
15. stop 使用 cooperative shutdown。
16. V1 `stop()` 返回时 worker 已退出。
17. callback 依赖资源必须至少存活到 worker join。
18. Buffer 为 SPSC latest-value primitive。
19. Buffer 初始为空。
20. `read()==false` 表示尚无任何 publish。
21. Buffer 不负责 freshness/timeout。
22. Buffer 使用固定预分配多槽。
23. Buffer publication 使用 release/acquire。
24. Buffer 必须额外解决 slot ownership。
25. 禁止 naive double-buffer reuse race。
26. Buffer 并发正确性必须符合 C++17 memory model。
27. 不用 `atomic_signal_fence` 替代跨线程同步。
28. 不假设任意 `std::atomic<T>` lock-free。
29. Buffer 不设置 cache-line 大小硬限制。
30. 大对象优化以 benchmark 为依据。
31. Queue V1 为 SPSC。
32. 一个 Queue 的 producer/consumer ownership 在 Runtime 中固定。
33. 多 producer 使用多个 Queue 或由上层序列化。
34. Queue 不阻塞、不扩容。
35. RT path 不动态分配。
36. RT path 不进入不可预测 blocking mutex。
37. RT path 不做文件、网络或格式化同步日志。
38. RT path 不使用 exception 作为正常控制流。
39. Required 初始化失败必须失败。
40. BestEffort 的降级必须通过 Status 可见。
41. Error 必须区分 `errno` 与 pthread 返回码来源。
42. Status 表示当前 runtime 能力。
43. Stats 表示长期聚合数据。
44. 高频 trace 通过 bounded RT → non-RT 通道输出。
45. 普通 Linux 仅验证功能。
46. PREEMPT_RT 用于正式 realtime 性能验证。
47. Sanitizer 测试与性能测试分离。
48. `-fno-exceptions` 只用于 Realtime target 的独立 CI profile。
49. V1 不提前设计 SCHED_RR / SCHED_DEADLINE 的复杂配置。
50. 未来 `realtime::Thread` 必须复用已有调度、亲和性、内存、状态和错误语义。
51. 模块保持 realtime primitive library 定位。

最终核心模型：

```text
                   System / Device
                         │
                         ▼
                   PeriodicTask
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
      Clock          Scheduling         Status/Stats
                         │
                 Affinity / Memory
                         │
                         ▼
                    Linux Thread


                  Data Exchange
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
          Buffer<T>             Queue<T,N>
        SPSC latest            SPSC ordered
           value                 events
```

Realtime 模块最终目标是：

> **提供一组 Linux-only、chrono-compatible、并发语义明确、可验证、可独立复用，并足以稳定支撑 RoboHardware 500 Hz 实时控制的数据与执行基础 primitive。**

**Realtime V1 的架构与公共语义至此冻结。后续修改应由实现结果、benchmark 或明确的上层需求驱动，而不是基于推测性的未来扩展。**
