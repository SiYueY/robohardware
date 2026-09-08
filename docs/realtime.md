# RoboHardware Realtime 模块设计

## 1. 定位与边界

### 1.1 模块定位

`realtime` 是面向 Linux 的通用 C++17 实时基础工具模块。

它提供构建实时线程和周期任务时最常用的基础能力：

* monotonic clock；
* realtime scheduler；
* CPU affinity；
* process memory locking；
* periodic task；
* RT / non-RT 数据交换；
* 最小运行统计；
* 结构化错误。

典型用途包括：

```text
机器人硬件通信线程
周期控制线程
CAN / EtherCAT / Serial worker
传感器采集线程
实时数据交换
独立实时工具程序
```

`realtime` 的目标是：

> 提供少量稳定、明确、可组合的 Linux realtime primitives，使调用者不需要直接处理 pthread、scheduler、affinity、mlockall 和 absolute sleep 等底层细节。

它不是 realtime application framework。

---

### 1.2 平台与版本范围

V1：

```text
Language:
    C++17

Platform:
    Linux

Scheduling:
    SCHED_OTHER
    SCHED_FIFO

Affinity:
    single CPU

Clock:
    CLOCK_MONOTONIC

Periodic wait:
    absolute deadline

Memory:
    mlockall(MCL_CURRENT | MCL_FUTURE)
```

实现允许直接使用：

```text
pthread_create()
pthread_join()
pthread_setschedparam()
pthread_setaffinity_np()

sched_get_priority_min()
sched_get_priority_max()

clock_gettime()
clock_nanosleep()

mlockall()
```

当前没有第二个平台，因此不建立：

```text
Platform
Backend
SchedulerBackend
ThreadBackend
ClockBackend
SystemInterface
```

等跨平台抽象。

---

### 1.3 非目标

V1 不负责：

```text
Realtime App
Runtime
Executor
ThreadPool

通用 Thread abstraction
CyclicThread hierarchy

CPU Manager
CPU Topology
CPU auto assignment
NUMA

SCHED_RR
SCHED_DEADLINE

Realtime Mutex
Semaphore
ConditionVariable

Logging
Tracing
Metrics framework

ROS / ROS2 integration

general-purpose lock-free container library
```

也不负责 Linux 系统部署：

```text
isolcpus
nohz_full
rcu_nocbs
IRQ affinity
PREEMPT_RT installation
CPU frequency governor
kernel boot parameters
```

这些属于部署层。

---

### 1.4 核心原则

V1 必须遵守：

1. 每个基础工具可以独立使用。
2. `PeriodicTask` 组合 scheduler / affinity / clock，而不是独占这些能力。
3. memory locking 是 process-level capability，不属于单个 task。
4. 默认配置必须合法可运行。
5. realtime hot path 不主动进行动态内存分配。
6. realtime hot path 不执行隐藏 blocking I/O。
7. realtime hot path 不存在测试 hook。
8. 不为了测试引入 production syscall mock 分支。
9. callback 必须满足明确 realtime contract。
10. 周期调度使用 absolute deadline，避免累计漂移。
11. overrun 后不 burst catch-up。
12. 数据交换 primitive 必须明确并发 ownership。
13. 公共 API 使用简单名称，复杂实现约束写入 contract。
14. 不因理论通用性增加没有真实 caller 的公共类型。
15. benchmark 决定是否需要进一步性能优化。

最终目标：

> API 简单，实时语义完整；实现轻量，行为可预测。

---

## 2. 基础工具与数据模型

### 2.1 时间类型

公共时间类型：

```cpp
using Duration = std::chrono::nanoseconds;

using TimePoint =
    std::chrono::time_point<
        std::chrono::steady_clock,
        Duration>;
```

所有 realtime scheduling 时间必须属于：

```text
monotonic domain
```

不得使用 wall clock 作为周期调度基准。

---

### 2.2 Clock

```cpp
class Clock {
public:
    static TimePoint now() noexcept;
};
```

语义：

> 返回当前 monotonic time。

实现应基于：

```text
CLOCK_MONOTONIC
```

或具有等价 monotonic 语义的实现。

`Clock::now()`：

```text
MUST:
    noexcept
    monotonic
    allocation-free
```

V1 不增加：

```text
WallClock
SystemClock wrapper
IClock
ClockProvider
VirtualClock
```

如果测试需要模拟时间，应优先抽取纯 deadline calculation 逻辑测试，而不是污染 production Clock API。

---

### 2.3 Scheduler

公共调度类型：

```cpp
enum class Scheduler : std::uint8_t {
    Other,
    Fifo,
};
```

使用：

```cpp
realtime::set_scheduler(
    realtime::Scheduler::Fifo,
    80);
```

这里使用：

```text
Scheduler
```

而不是：

```text
SchedulingPolicy
Policy
SchedulerConfig
```

原因：

```cpp
realtime::Scheduler::Fifo
```

足够短，并且领域语义明确。

---

### 2.4 Scheduler API

```cpp
Result<void> set_scheduler(
    Scheduler scheduler,
    int priority = 0) noexcept;
```

该函数作用于：

> 调用它的线程。

因此 API 不重复写：

```text
current
thread
```

例如不使用：

```cpp
set_current_thread_scheduler()
```

#### Scheduler::Other

要求：

```text
priority == 0
```

否则：

```cpp
ErrorCode::InvalidArgument
```

#### Scheduler::Fifo

`priority` 必须满足：

```text
sched_get_priority_min(SCHED_FIFO)
    <= priority <=
sched_get_priority_max(SCHED_FIFO)
```

非法 priority：

```cpp
ErrorCode::InvalidArgument
```

系统调用失败：

* 返回结构化 Error；
* 保留 pthread/native error code；
* 不 throw。

---

### 2.5 Affinity

公共 API：

```cpp
Result<void> set_affinity(
    int cpu) noexcept;
```

语义：

> 将调用线程绑定到指定 CPU。

使用：

```cpp
realtime::set_affinity(3);
```

API 至少验证：

```text
cpu >= 0
```

最终有效性由 Linux affinity syscall 决定。

V1 不增加：

```text
AffinityConfig
CpuAffinity
CpuSet
CpuMask
std::vector<int> cpus
automatic CPU selection
```

单 CPU affinity 已覆盖最常见 realtime worker 场景。

如果不需要 affinity：

> 不调用 `set_affinity()`。

---

### 2.6 Memory Lock

公共 API：

```cpp
Result<void> lock_memory() noexcept;
```

实现：

```text
mlockall(MCL_CURRENT | MCL_FUTURE)
```

语义：

> 锁定当前进程的现有和后续映射内存，降低运行期间发生 major/minor page fault 的风险。

这是：

```text
process-level
```

能力，不属于：

```text
PeriodicTask
thread
```

因此 `PeriodicTask::Options` 不包含：

```text
MemoryConfig
lock_memory
prefault_bytes
```

典型调用：

```cpp
auto result = realtime::lock_memory();

if (!result) {
    // handle before starting RT workers
}
```

V1 不提供：

```text
MemoryConfig
unlock_memory()
prefault_bytes
memory manager
```

---

### 2.7 Prefault

V1 不公开通用 prefault API。

特别是不得通过：

```text
allocate arbitrary heap buffer
touch every 4 KB
```

就宣称完成：

```text
thread stack prefault
complete realtime memory preparation
```

如果未来真实需求要求：

```text
thread stack prefault
known workspace prefault
```

则需要明确目标对象和语义后再单独设计。

---

### 2.8 Error

公共错误模型保持最小：

```cpp
enum class ErrorCode {
    InvalidArgument,
    InvalidState,
    SystemError,
};

struct Error {
    ErrorCode code{ErrorCode::InvalidState};
    int native_code{0};
};
```

通过：

```cpp
Result<T>
```

返回。

具体 Linux / pthread 原因保存在：

```cpp
native_code
```

中。

不创建：

```text
SchedulerError
AffinityError
MemoryError
ThreadError
RealtimeException
```

等错误层级。

如果现有项目共享统一 `Result/Error` 类型，应优先复用已有统一实现，而不是 realtime 再建立第二套错误系统。

---

## 3. PeriodicTask

### 3.1 定位

`PeriodicTask` 是 realtime 模块提供的周期执行工具。

它负责：

```text
worker thread lifecycle
scheduler setup
affinity setup
absolute periodic wait
cycle information
deadline miss handling
basic runtime stats
```

它不是：

```text
general-purpose Thread
Executor
Runtime
Task scheduler
```

用户也可以完全不用 `PeriodicTask`，直接：

```cpp
std::thread worker([] {
    realtime::set_scheduler(
        realtime::Scheduler::Fifo,
        80);

    realtime::set_affinity(3);

    run();
});
```

---

### 3.2 Options

```cpp
class PeriodicTask {
public:
    struct Options {
        Duration period{};

        Scheduler scheduler{Scheduler::Other};
        int priority{0};

        std::optional<int> cpu;

        bool required{true};
    };

    ...
};
```

这里只有 `PeriodicTask` 使用 `Options`。

不建立 namespace-level：

```text
PeriodicTaskOptions
SchedulerConfig
AffinityConfig
RealtimeMode
```

等额外公共类型。

---

### 3.3 默认配置

默认配置必须合法。

默认：

```text
Scheduler::Other
priority = 0
cpu = nullopt
required = true
```

因此：

```cpp
realtime::PeriodicTask task({
    .period = 2ms,
});
```

必须能够在普通 Linux 环境启动。

禁止出现：

```text
默认 Fifo
+
priority = 0
```

这种默认构造即非法的组合。

---

### 3.4 required

```cpp
bool required{true};
```

只控制 realtime thread setup 的失败策略。

#### required == true

如果：

```text
scheduler setup failed
affinity setup failed
```

则：

```text
start() fails
worker does not enter periodic loop
```

#### required == false

如果 scheduler 或 affinity 配置失败：

```text
record failure internally as needed
continue with available thread configuration
```

即：

> best-effort realtime setup。

V1 不再额外创建：

```text
RealtimeMode
FallbackPolicy
SetupPolicy
```

具名字段：

```cpp
.required = false
```

已经足够明确。

---

### 3.5 API

推荐：

```cpp
class PeriodicTask {
public:
    struct Options {
        Duration period{};
        Scheduler scheduler{Scheduler::Other};
        int priority{0};
        std::optional<int> cpu;
        bool required{true};
    };

    explicit PeriodicTask(Options options);
    ~PeriodicTask();

    PeriodicTask(const PeriodicTask&) = delete;
    PeriodicTask& operator=(const PeriodicTask&) = delete;

    Result<void> start(Callback callback);
    Result<void> stop();

    bool running() const noexcept;
    Stats stats() const noexcept;
};
```

具体 callback storage 可以通过 template wrapper 实现，但 public semantics 必须保持简单。

---

### 3.6 Callback Contract

逻辑 callback 签名：

```cpp
void(const CycleInfo&) noexcept
```

例如：

```cpp
task.start(
    [](const realtime::CycleInfo& cycle) noexcept {
        update();
    });
```

compile-time 应尽量保证 callback：

```text
invocable
returns void
noexcept
```

Callback 在 realtime worker 上执行。

调用者必须保证 callback：

```text
MUST NOT:
    throw
    perform unbounded work

SHOULD NOT:
    allocate
    perform blocking filesystem/network I/O
    lock ordinary contended mutexes
    log synchronously
```

模块不尝试自动检测所有 realtime violation。

---

### 3.7 Callback storage

如果实现使用：

```cpp
std::function<void(const CycleInfo&)>
```

允许在：

```text
start() non-RT setup phase
```

产生一次动态分配。

但必须保证：

> worker 正式进入 realtime periodic loop 后，不再因为 callback dispatch 产生动态分配。

V1 不为了消灭一次启动期 allocation 引入复杂 type-erasure framework。

如果未来 benchmark 证明 callback storage 是明确问题，再优化。

---

### 3.8 Startup

worker 推荐执行：

```text
create thread
    ↓
apply scheduler
    ↓
apply affinity
    ↓
report startup result
    ↓
enter periodic loop
```

`PeriodicTask` 必须复用：

```cpp
set_scheduler()
set_affinity()
```

不得在 `periodic_task.cpp` 再维护第二套 scheduler/affinity syscall 逻辑。

---

### 3.9 周期调度

周期等待必须使用：

```text
CLOCK_MONOTONIC
TIMER_ABSTIME
```

等价 absolute wait。

禁止使用：

```cpp
sleep_for(period);
```

作为主周期实现。

原因：

relative sleep 会形成累计漂移：

```text
callback execution
+
relative sleep
+
callback execution
+
relative sleep
```

而 absolute deadline 维持：

```text
t0
t0 + period
t0 + 2 * period
t0 + 3 * period
...
```

---

### 3.10 CycleInfo

```cpp
struct CycleInfo {
    std::uint64_t sequence{0};

    TimePoint scheduled_time{};
    TimePoint wakeup_time{};

    Duration lateness{};

    std::uint32_t missed_periods{0};
};
```

含义：

#### sequence

当前实际执行 callback 的 sequence。

#### scheduled_time

本周期理论 absolute deadline。

#### wakeup_time

周期等待结束后的实际 monotonic 时间。

#### lateness

定义：

```text
max(wakeup_time - scheduled_time, 0)
```

如果实现允许 early wakeup，则不得将负值伪装成 lateness。

#### missed_periods

由于过晚唤醒或前一 callback overrun 而跳过的完整周期数量。

---

### 3.11 Deadline miss 与 overrun

如果：

```text
wakeup_time > scheduled_time
```

即存在 lateness。

是否计入：

```text
deadline_misses
```

必须使用固定、一致的定义。

推荐：

> `lateness > 0` 时记录一次 wakeup deadline miss。

如果执行时间导致跨过一个或多个后续周期：

```text
missed_periods > 0
```

必须记录实际跳过的周期数量。

---

### 3.12 Skip，不 catch-up

如果任务已经落后多个周期：

```text
scheduled:
    2ms
    4ms
    6ms
    8ms

worker wakes:
    7ms
```

不得：

```text
立刻执行 4ms callback
立刻执行 6ms callback
然后执行 8ms
```

形成 burst catch-up。

应跳到：

```text
下一个未来 absolute deadline
```

目标：

```text
bounded recovery
no callback burst
no historical-cycle replay
```

这是 PeriodicTask 的核心 invariant。

---

### 3.13 stop()

`stop()` 是 cooperative shutdown。

典型实现：

```text
set stop_requested
    ↓
wait worker exit
```

如果 worker 正在：

```text
clock_nanosleep()
```

V1 允许：

> worker 在当前等待 deadline 到达后观察 stop request 并退出。

因此 `stop()` 不保证立即唤醒 sleeping worker。

V1 不为了这一点增加：

```text
eventfd
signal interruption
condition variable
timerfd abstraction
```

对于典型毫秒级周期，该行为可以接受。

如果未来真实出现长周期 task，再设计 interruptible stop。

---

### 3.14 running()

不提供泛化：

```text
Status
TaskStatus
RuntimeStatus
```

仅提供：

```cpp
bool running() const noexcept;
```

回答：

> worker 当前是否处于 active lifecycle。

配置本身不需要通过 Status 再回显。

---

### 3.15 Stats

```cpp
struct Stats {
    std::uint64_t cycles{0};

    std::uint64_t deadline_misses{0};
    std::uint64_t missed_periods{0};

    Duration max_lateness{};
    Duration max_execution{};
};
```

字段语义：

```text
cycles
    callback 实际执行次数

deadline_misses
    callback 周期唤醒发生 deadline miss 的次数

missed_periods
    因 overrun / late wakeup 实际跳过的周期总数

max_lateness
    observed worst wakeup lateness

max_execution
    observed worst callback execution duration
```

V1 不在 runtime Stats 中加入：

```text
mean
variance
p95
p99
p99.9
histogram
```

这些属于 benchmark。

---

### 3.16 Stats 并发

`stats()` 必须：

```text
non-blocking
```

并且不得为了 snapshot 完全事务一致性给 realtime loop 增加普通 mutex。

允许：

```text
atomics
careful snapshot
slightly non-transactional diagnostics
```

实时执行路径优先。

---

## 4. RT 数据交换

### 4.1 总体模型

V1 提供两个不同的数据交换 primitive：

```text
Value
    只关心最近状态

Queue
    每一个中间项都有意义
```

两者不是重复能力。

典型：

```cpp
realtime::Value<State> state;
realtime::Queue<Command, 64> commands;
```

---

### 4.2 Value

公共类型：

```cpp
template <typename T>
class Value {
public:
    void write(
        const T& value) noexcept;

    bool read(
        T& value) const noexcept;
};
```

它表示：

> producer 发布一个值，consumer 获取最近完整发布的值；旧值允许被新值覆盖。

例如：

```text
writer:
    A → B → C → D

reader:
    可以直接得到 D
```

reader 不要求消费：

```text
A
B
C
```

---

### 4.3 Value 并发 Contract

V1 必须明确真实实现支持的 ownership。

如果当前算法是：

```text
single writer
single reader
```

则 header 必须明确写：

```text
Exactly one writer and one reader are supported.
```

不得因为类名是：

```cpp
Value<T>
```

就暗示任意线程安全。

`Value` 的名称保持简单，复杂并发保证通过 contract 表达。

---

### 4.4 Value Runtime Requirements

`write()` / `read()`：

```text
MUST:
    non-blocking
    allocation-free after construction
    noexcept
```

不得依赖：

```text
ordinary blocking mutex
condition variable
dynamic allocation
```

如果 `T` 本身不能满足所需操作约束，则应通过：

```text
static_assert
documented type requirement
```

明确限制。

不得静默提供错误的 realtime guarantee。

---

### 4.5 Queue

公共类型：

```cpp
template <typename T, std::size_t Capacity>
class Queue {
public:
    bool try_push(
        const T& value) noexcept;

    bool try_pop(
        T& value) noexcept;
};
```

名称保留：

```cpp
realtime::Queue<T, N>
```

而不是：

```text
SpscQueue
LockFreeQueue
RealtimeQueue
BoundedQueue
```

因为 namespace 已表达 realtime context。

SPSC / fixed capacity 等属于 contract。

---

### 4.6 Queue Contract

V1 Queue：

```text
fixed capacity
single producer
single consumer
FIFO
non-blocking
allocation-free after construction
```

因此：

```text
one producer
one consumer
```

必须在 public header 明确。

Queue 满：

```cpp
try_push() == false
```

Queue 空：

```cpp
try_pop() == false
```

不得：

```text
block
sleep
allocate
grow dynamically
```

---

### 4.7 Queue 不提供 STL 容器接口

V1 不增加：

```text
push_wait
pop_wait
front
back
resize
iterator

dynamic capacity

MPSC
MPMC
```

它不是 STL queue replacement。

如果未来确有新的并发 topology，应新增明确 primitive 或重新设计，而不是逐渐让 `Queue` 承担所有并发模型。

---

### 4.8 Value 与 Queue 的选择

使用 `Value`：

```text
joint state
sensor snapshot
target state
configuration snapshot
latest command
```

特点：

> 只关心最新数据。

使用 `Queue`：

```text
events
discrete commands
transactions
messages that must preserve ordering
```

特点：

> 中间数据不能被覆盖。

---

## 5. 实现约束

### 5.1 推荐目录

```text
realtime/
├── CMakeLists.txt
├── README.md
├── affinity.hpp
├── clock.hpp
├── error.hpp
├── memory.hpp
├── periodic_task.hpp
├── queue.hpp
├── scheduler.hpp
├── value.hpp
├── affinity.cpp
├── clock.cpp
├── memory.cpp
├── periodic_task.cpp
├── scheduler.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── realtime_test.cpp
│   └── concurrency_test.cpp
└── benchmarks/
    ├── CMakeLists.txt
    └── realtime_500hz_benchmark.cpp
```

保持扁平结构。

模块仍向 consumer 暴露 `#include <realtime/*.hpp>`；该 include 路径由
模块 CMake 的 public include directory 保持兼容。

不增加：

```text
detail/
internal/
platform/
runtime/
thread/
backend/
manager/
```

少量 internal helper 放：

```cpp
namespace {
...
}
```

即可。

---

### 5.2 Public API 基线

V1 public API 应接近：

```cpp
namespace realtime {

using Duration = std::chrono::nanoseconds;

using TimePoint =
    std::chrono::time_point<
        std::chrono::steady_clock,
        Duration>;

class Clock {
public:
    static TimePoint now() noexcept;
};


enum class Scheduler : std::uint8_t {
    Other,
    Fifo,
};

Result<void> set_scheduler(
    Scheduler scheduler,
    int priority = 0) noexcept;

Result<void> set_affinity(
    int cpu) noexcept;

Result<void> lock_memory() noexcept;


struct CycleInfo {
    std::uint64_t sequence{0};

    TimePoint scheduled_time{};
    TimePoint wakeup_time{};

    Duration lateness{};
    std::uint32_t missed_periods{0};
};


struct Stats {
    std::uint64_t cycles{0};

    std::uint64_t deadline_misses{0};
    std::uint64_t missed_periods{0};

    Duration max_lateness{};
    Duration max_execution{};
};


class PeriodicTask {
public:
    struct Options {
        Duration period{};

        Scheduler scheduler{Scheduler::Other};
        int priority{0};

        std::optional<int> cpu;

        bool required{true};
    };

    explicit PeriodicTask(
        Options options);

    ~PeriodicTask();

    PeriodicTask(
        const PeriodicTask&) = delete;

    PeriodicTask& operator=(
        const PeriodicTask&) = delete;

    Result<void> stop();

    bool running() const noexcept;

    Stats stats() const noexcept;

    // start() may be a constrained template.
};


template <typename T>
class Value {
public:
    void write(
        const T& value) noexcept;

    bool read(
        T& value) const noexcept;
};


template <typename T, std::size_t Capacity>
class Queue {
public:
    bool try_push(
        const T& value) noexcept;

    bool try_pop(
        T& value) noexcept;
};

}  // namespace realtime
```

新增 public API 前必须证明：

> V1 已存在真实 caller 或明确正确性需求。

---

### 5.3 PeriodicTask 内部结构

`periodic_task.cpp` 应让核心执行流程非常清晰。

推荐组织：

```cpp
namespace {

Result<void> validate(
    const PeriodicTask::Options&) noexcept;

Result<void> apply_scheduler(
    const PeriodicTask::Options&) noexcept;

Result<void> apply_affinity(
    const PeriodicTask::Options&) noexcept;

void run_loop(
    PeriodicTask::Impl&) noexcept;

void update_stats(
    PeriodicTask::Impl&,
    const CycleInfo&,
    Duration execution) noexcept;

}
```

其中：

```cpp
apply_scheduler()
```

必须调用公共或共享实现：

```cpp
set_scheduler()
```

而不是重新写：

```text
pthread_setschedparam()
```

逻辑。

Affinity 同理。

---

### 5.4 Production Code 不允许 TestHooks

必须删除：

```text
TestHooks
test_hooks
forced errno
fake syscall return

#ifdef UNIT_TEST
```

等生产运行路径测试注入。

尤其禁止出现在：

```text
clock_nanosleep path
scheduler path
affinity path
memory path
```

原则：

> production code 只描述 production behavior。

对于 Linux syscall 行为，优先使用真实 integration test。

---

### 5.5 Hot-path 约束

正式 realtime worker loop 启动后：

```text
MUST NOT intentionally:
    allocate heap
    resize dynamic container
    create string
    create thread
    perform hidden retry
    sleep outside explicit periodic wait
    throw
```

同时：

```text
Value::read/write
Queue::try_push/try_pop
```

必须满足各自 bounded/non-blocking contract。

---

### 5.6 注释规范

Public API 必须记录：

```text
thread scope
process scope
ownership
blocking semantics
allocation semantics
error semantics
clock domain
```

例如：

```cpp
/// Sets the scheduling policy of the calling thread.
///
/// `Scheduler::Fifo` requires a valid FIFO priority and
/// may require elevated scheduling privileges.
Result<void> set_scheduler(
    Scheduler scheduler,
    int priority = 0) noexcept;
```

```cpp
/// Pins the calling thread to one CPU.
Result<void> set_affinity(
    int cpu) noexcept;
```

```cpp
/// Fixed-capacity realtime queue.
///
/// Exactly one producer and one consumer are supported.
/// Push and pop are non-blocking and allocation-free.
template <typename T, std::size_t Capacity>
class Queue;
```

避免无信息量注释：

```cpp
/// Returns stats.
Stats stats() const noexcept;
```

---

### 5.7 明确不实现

V1 不增加：

```text
Thread
RealtimeThread
CyclicThread

App
Runtime
Executor

ThreadPool

SchedulerConfig
AffinityConfig
MemoryConfig

RealtimeMode

Status

CpuSet
CpuMask
CpuManager

RealtimeMutex

SCHED_RR
SCHED_DEADLINE

logger
tracing
metrics

platform abstraction
mock syscall interface
```

---

## 6. 测试与验收

测试分为：

```text
Unit
Linux Integration
Concurrency
Realtime Benchmark
```

---

### 6.1 Unit Tests

#### Clock

验证：

```text
Clock::now() monotonic progression
Duration / TimePoint arithmetic
```

不要依赖 wall clock。

---

#### Scheduler

验证：

```text
Scheduler::Other + priority 0
invalid Other priority

FIFO min priority
FIFO max priority
invalid FIFO low/high priority
```

纯参数验证不需要 root 权限。

---

#### Affinity

验证：

```text
negative cpu
valid allowed CPU
```

真实 affinity 行为放 integration test。

---

#### PeriodicTask

覆盖：

```text
period == 0 rejected
negative/invalid period rejected where applicable

start
stop

double start rejected

stop before start

callback sequence

scheduled_time progression

wakeup / lateness semantics

deadline miss

missed-period skip

no burst catch-up

running()

stats()
```

Missed-period calculation 应尽量抽成 deterministic pure logic 进行测试，而不是依赖测试机真的 sleep miss。

---

#### Value

覆盖：

```text
initial state semantics

write
read

multiple sequential writes
latest value

large update count
```

同时验证真实 ownership contract。

---

#### Queue

覆盖：

```text
initially empty

FIFO ordering

full
empty

wrap-around

capacity boundary

large push/pop sequence
```

---

### 6.2 Linux Integration Tests

#### Scheduler

真实调用：

```cpp
set_scheduler(
    Scheduler::Other,
    0);
```

必须可验证。

FIFO：

```text
成功
    → 验证当前 policy / priority

EPERM
    → GTEST_SKIP()
```

权限不足不是 library failure。

---

#### Affinity

步骤：

```text
sched_getaffinity()
    ↓
选择当前 allowed CPU
    ↓
set_affinity(cpu)
    ↓
pthread_getaffinity_np() / equivalent
    ↓
验证
```

不得假设：

```text
CPU 0
```

一定可用，因为容器、cpuset、systemd 等可能限制 CPU set。

---

#### Memory

真实调用：

```cpp
lock_memory();
```

如果：

```text
EPERM
ENOMEM
RLIMIT_MEMLOCK limitation
```

来自测试环境：

```text
根据环境条件 skip
```

不要 fake `mlockall()`。

---

#### PeriodicTask

至少验证：

```text
Scheduler::Other periodic execution

valid affinity

FIFO where permitted

start/stop lifecycle

multiple cycles
```

---

### 6.3 Concurrency Tests

#### Value

如果 contract 为 single writer / single reader：

```text
one writer thread
one reader thread
100k+ / 1M updates

no torn values
no invalid state
eventual progress
```

测试数据结构应设计成能够检测 partial/torn read。

---

#### Queue

```text
producer:
    0 ... N

consumer:
    0 ... N
```

必须验证：

```text
ordering
no duplication
no corruption
no unexpected loss
```

Queue 满时发生：

```text
try_push() == false
```

属于正常 API behavior，不应被测试误认为 corruption。

---

### 6.4 Sanitizers

CI 建议：

```text
TSAN:
    Value
    Queue
    non-RT concurrency tests

ASAN:
    lifecycle and container tests

UBSAN:
    general unit tests
```

不要使用 sanitizer benchmark 数据评价 realtime latency。

Sanitizer 只用于 correctness。

---

### 6.5 Realtime Benchmark

至少测试：

```text
500 Hz
1000 Hz
```

场景：

```text
Scheduler::Other

Scheduler::Fifo
    where permitted

without affinity

with affinity
```

建议记录：

```text
cycles

deadline misses
missed periods

min / mean / p99 / p99.9 / max lateness

min / mean / p99 / max callback execution

CPU usage

major/minor page faults where meaningful
```

其中高级统计只属于 benchmark。

Runtime `Stats` 仍只保留：

```text
cycles
deadline_misses
missed_periods
max_lateness
max_execution
```

---

### 6.6 Benchmark 环境

Benchmark 报告必须记录关键环境：

```text
kernel version
PREEMPT_RT or generic kernel
CPU model
CPU frequency policy
scheduler
priority
affinity CPU
memory lock status
system load
```

否则不同运行结果无法比较。

---

### 6.7 真实 RT 环境

V1 应至少在：

```text
普通 Linux kernel
```

完成功能验证。

如果目标产品使用：

```text
PREEMPT_RT
```

则冻结前应增加目标 kernel benchmark。

Benchmark 不要求 realtime 模块：

> 在任何 Linux 主机上都自动达到 hard realtime。

目标是：

> realtime 模块本身不成为明显的软件抖动来源，并正确暴露 Linux realtime primitives。

---

### 6.8 V1 完成条件

只有同时满足以下条件，Realtime V1 才可以冻结。

#### API

* Scheduler / affinity / memory 可以独立使用；
* `PeriodicTask` 不成为 runtime framework；
* 无不必要 `Config` 类型；
* 默认 PeriodicTask 配置合法；
* public API 名称简洁；
* public concurrency contract 明确。

#### Scheduler / Affinity / Memory

* `Scheduler::Other` 正确；
* `Scheduler::Fifo` priority validation 正确；
* native scheduling errors 保留；
* affinity 使用真实 allowed CPU 验证；
* memory lock 是 process-level API。

#### PeriodicTask

* absolute deadline；
* monotonic clock；
* no cumulative relative-sleep drift；
* overrun skip；
* no burst catch-up；
* required / best-effort behavior 正确；
* start / stop lifecycle 正确；
* callback noexcept contract；
* hot path 不主动 allocation。

#### Value / Queue

* ownership contract 与实现一致；
* non-blocking；
* runtime allocation-free；
* concurrency tests 通过；
* TSAN 无已知 data race。

#### Production quality

* TestHooks 为 0；
* production test branches 为 0；
* 无隐藏线程；
* 无隐藏 retry；
* 无不必要 internal framework；
* public API 有有效 contract comments。

#### Validation

* unit tests 通过；
* Linux integration tests 通过；
* concurrency tests 通过；
* 500 Hz benchmark 完成；
* 1 kHz benchmark 完成；
* compiler warnings 为零。

---

## 最终边界

模块依赖关系：

```text
Application / Hardware / Protocol
              │
      ┌───────┼────────┐
      ▼       ▼        ▼
PeriodicTask Value    Queue
      │
 ┌────┼────────┐
 ▼    ▼        ▼
Clock Scheduler Affinity

Memory
    └──── process-level independent utility
```

`realtime` 与其它基础模块保持独立：

```text
realtime
    不依赖 can

can
    不依赖 realtime
```

上层可以自由组合：

```cpp
realtime::lock_memory();

realtime::PeriodicTask task({
    .period = 2ms,
    .scheduler = realtime::Scheduler::Fifo,
    .priority = 80,
    .cpu = 3,
});

task.start(
    [](const realtime::CycleInfo& cycle) noexcept {
        // realtime work
    });
```

也可以只使用独立工具：

```cpp
std::thread worker([] {
    realtime::set_scheduler(
        realtime::Scheduler::Fifo,
        80);

    realtime::set_affinity(3);

    run();
});
```

数据交换保持：

```cpp
realtime::Value<State> state;
realtime::Queue<Command, 64> commands;
```

最终使用者只需要理解：

```text
Clock
Scheduler
set_scheduler
set_affinity
lock_memory

PeriodicTask
CycleInfo
Stats

Value
Queue
```

即可使用整个模块。

如果一个使用者需要理解：

```text
Runtime
Manager
Backend
Context
Environment
Thread hierarchy
Cpu manager
```

才能使用 realtime，那么模块已经设计过重。

如果为了减少类型数量又删除：

```text
Scheduler
CycleInfo
Value / Queue semantic distinction
```

等真正影响实时正确性的领域概念，那么模块又被过度简化。

因此最终原则是：

> **减少无价值抽象，不减少 realtime correctness 所依赖的语义；API 保持短而直接，关键并发、调度和时序 contract 必须在文档中完整定义。**
