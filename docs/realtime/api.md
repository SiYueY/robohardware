# Realtime Interface Design

状态：已冻结

阶段：Realtime API Design

最后更新：2026-09-12

## 1. 目的

本文逐项冻结 `realtime` 的公开 Interface，包括类型和函数，
也包括调用者必须理解的所有权、顺序、错误、时间、并发和 realtime-safety 契约。

项目章程、V1 需求规格和 Architecture Design v2 是本文的上位约束。本文不设计
任务运行时、transport 或设备业务逻辑。

## 2. 决策顺序

1. execution ownership；
2. monotonic time 和 deadline 表达；
3. periodic scheduling 和 missed-period 语义；
4. 当前线程配置；
5. 进程内存锁定；
6. 测量与统计；
7. `Queue<T, Capacity>`；
8. `Buffer<T>`；
9. realtime-safety 契约与验证；
10. public header layout。

后续决策不得推翻已冻结的上游语义；确有必要时必须显式重新评审。

## 3. Execution Ownership

状态：已冻结

### 3.1 Caller-owned execution

Realtime library 不拥有执行流。调用者负责：

- 创建线程；
- 管理线程生命周期；
- 控制运行状态；
- 编写周期循环；
- 决定业务逻辑执行顺序。

Realtime 只提供：

- 当前线程配置；
- 周期时间管理；
- 超期检测；
- 时间测量；
- RT/NRT 数据交换原语。

### 3.2 明确排除

V1 不提供：

- `PeriodicTask`、`Task` 或 `Thread`；
- executor 或 scheduler runtime；
- callback 驱动执行模型；
- 内部线程或 hidden worker thread；
- 用户任务注册、启停或生命周期管理。

### 3.3 Periodic scheduling seam

`PeriodicSchedule` 表示周期时间机制而不是任务实体。它负责：

- 基于 monotonic clock 推进周期；
- 计算 absolute deadline；
- sleep/wait；
- 检测 missed period；
- 执行调用者显式选择的恢复语义。

它不调用用户函数，不拥有运行标志，不创建、启动或停止线程。

概念使用模型：

```cpp
const auto affinity_error = set_current_thread_affinity(cpu_index);
const auto scheduling_error =
    set_current_thread_scheduling(SchedulingPolicy::Fifo, priority);

PeriodicSchedule schedule;
const auto schedule_error = schedule.configure(first_release, period, policy);
if (affinity_error || scheduling_error || schedule_error) {
  // Caller-owned configuration error policy.
  return;
}

while (running) {
  const auto result = schedule.wait_next();

  // Caller-owned realtime driver logic.
}
```

其中 `running`、driver logic 和退出处理均属于调用者。代码表达已冻结的 execution
ownership 和 schedule 使用关系。

## 4. Monotonic Time Model

状态：已冻结

### 4.1 Public Interface

V1 使用固定的 Linux `CLOCK_MONOTONIC` clock domain，并提供一个具体、
chrono-compatible 的 `Clock`，而不是可继承或可注入的 clock abstraction：

```cpp
namespace realtime {

struct Clock final {
  using rep = std::int64_t;
  using period = std::nano;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<Clock>;

  static constexpr bool is_steady = true;

  static time_point now() noexcept;
};

using Duration = Clock::duration;
using TimePoint = Clock::time_point;

}  // namespace realtime
```

`Clock` 的 Interface 契约：

- `now()` 读取 `CLOCK_MONOTONIC`；
- 返回值单调不减，但不保证相邻调用严格递增；
- 不受 wall-clock 跳变影响；
- 不计入系统 suspend 时间；
- epoch 未指定，仅允许在同一次 boot 的同一 clock domain 内比较；
- `now()` 不阻塞、不分配动态内存且不抛异常；
- 在受支持 Linux 环境中，固定有效的 `CLOCK_MONOTONIC` 读取没有可恢复失败路径；
  如果 implementation 无法履行这一 Value Query 契约，则按第 12.1 节处理为致命的
  implementation-controlled invariant failure，不返回伪造时间；
- public Interface 不暴露 `clockid_t` 或 `timespec`。

### 4.2 Deadline 表达

V1 不增加 `Deadline` wrapper。absolute deadline 直接使用 `TimePoint`，时间间隔
使用 `Duration`。二者类型不同，可以避免把 duration 误传为 absolute time。

### 4.3 固定 clock domain 的原因

- `PeriodicSchedule` 的读取和 absolute sleep 必须使用同一 clock domain；
- C++17 不保证 `std::chrono::steady_clock` 与 Linux `CLOCK_MONOTONIC` 共享 epoch，
  因而不能把其 `time_point` 可移植地直接作为 `clock_nanosleep` 的 absolute time；
- Linux `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)` 直接表达绝对等待；
- `CLOCK_REALTIME` 会引入 wall-clock 调整语义；
- `CLOCK_BOOTTIME` 包含 suspend，不符合实时控制周期的运行时间语义；
- `CLOCK_MONOTONIC_RAW` 不作为 V1 absolute sleep contract；
- 可注入 clock 会扩大公开 seam，而 V1 只有一个生产 clock domain。

### 4.4 明确不提供

V1 不提供：

- wall-clock、calendar time 或时间格式化；
- runtime clock selection；
- virtual clock、mock clock 或继承式 `IClock`；
- `CLOCK_REALTIME`、`CLOCK_BOOTTIME` 或 `CLOCK_MONOTONIC_RAW` 选择；
- public `timespec` conversion；
- clock synchronization；
- clock resolution 查询，除非测量阶段证明其必须成为公共能力。

确定性单元测试应把时间计算写成可由显式 `TimePoint` 输入验证的纯逻辑。Linux
clock 读取和 absolute sleep 通过私有 syscall seam 测试，不为测试扩大公开
Interface。

## 5. Periodic Scheduling Semantics

状态：已冻结

本节定义周期行为；对应的最终公开类型和结果结构在第 12.4 节冻结。

### 5.1 Anchored schedule

`PeriodicSchedule` 由调用者显式配置以下参数：

- `first_release`：第一个计划释放时间，类型为 `TimePoint`；
- `period`：严格大于零的周期，类型为 `Duration`；
- missed-period policy：无默认值，必须显式选择。

周期时间网格固定为：

```text
release(n) = first_release + n * period
```

构造和配置均不隐式调用 `Clock::now()`。正常推进和 missed-period 恢复均不得把实际
唤醒时间作为新的隐式基准，因此不会因执行时间或 signal interruption 累积 phase
drift。

### 5.2 `wait_next()` 行为

每次调用针对当前计划释放时间：

1. 如果目标在未来，使用 `CLOCK_MONOTONIC` absolute sleep 等待；
2. `EINTR` 使用同一个 absolute target 继续等待，不重新开始相对周期；
3. 如果目标已经到达或过去，不执行额外 sleep；
4. 返回当前目标、实际观察到的唤醒时间、lateness 和 missed release 数量；
5. 根据显式 policy 计算下一目标。

unexpected system error 必须作为值返回，且不得推进 schedule。V1 不提供周期等待
cancellation；调用者最迟在当前目标到达并返回后重新检查自己的运行状态。

### 5.3 Observable timing

一次成功等待提供以下已冻结字段：

- `scheduled_time`：本次等待针对的计划释放时间；
- `wake_time`：等待返回后读取的 monotonic time；
- `period`：生成本次 observation 时已配置的 schedule period snapshot；
- `lateness = max(wake_time - scheduled_time, Duration::zero())`；
- `missed_releases`：在本次目标之后、`wake_time` 之前或恰好等于它的额外释放点
  数量。

因此，小于一个完整周期的正 lateness 不等于跳过一个 release；二者必须分别报告。

### 5.4 Missed-period policies

V1 只提供两个显式 policy：

#### `CatchUp`

- 每次成功等待后只推进一个 period；
- 保持原始时间网格；
- 当调用者落后多个周期时，后续调用立即返回，直到追上时间网格；
- 不声称调用者能够安全或有意义地补做业务工作。

#### `SkipMissed`

- 保持原始时间网格；
- 跳过已经到达或过去的后续 release；
- 下一目标是严格晚于本次 `wake_time` 的第一个原始网格点；
- 本次结果报告跳过数量，调用者决定诊断或降级策略。

V1 不提供自动 `Rebase` policy。把 `wake_time + period` 作为下一目标会丢失原始
phase 并掩盖持续过载；确需重新定相时，由调用者在无并发访问时显式重新调用
`configure()`。

### 5.5 明确不负责

`PeriodicSchedule` 不负责：

- deadline enforcement 或终止超时业务逻辑；
- 自动改变线程优先级或 affinity；
- 自动降频、告警或故障恢复；
- 执行时间统计的全局收集；
- signal policy 或进程退出；
- 多 schedule 同步和 distributed clock synchronization。

## 6. Current-thread Configuration

状态：已冻结

本节冻结职责、公开名称、失败语义和单 CPU affinity 范围。

### 6.1 Current thread only

V1 只提供以下 policy 和两个独立配置操作：

```cpp
enum class SchedulingPolicy {
  Normal,
  Fifo,
  RoundRobin,
};

[[nodiscard]] std::error_code set_current_thread_scheduling(
    SchedulingPolicy policy,
    int priority) noexcept;

[[nodiscard]] std::error_code set_current_thread_affinity(
    unsigned int cpu_index) noexcept;
```

Interface 不接受 `pthread_t`、TID、native handle 或 library-owned `Thread`。调用者
必须在目标线程内部执行配置，从而避免跨线程生命周期、权限和并发语义。

### 6.2 Scheduling policy

V1 冻结以下枚举语义：

- `Normal`：Linux `SCHED_OTHER`，priority 必须为 `0`；
- `Fifo`：Linux `SCHED_FIFO`；
- `RoundRobin`：Linux `SCHED_RR`。

`Fifo` 和 `RoundRobin` 的 priority 必须落在目标系统对该 policy 报告的范围内。
implementation 在调用设置操作前验证 policy/priority 组合，并保留 Linux 返回的
权限或系统错误。

V1 不为 Linux priority 建立归一化的 `0.0..1.0` 或 `Low/Medium/High` 映射；调用者
使用明确的原生数值语义。

### 6.3 CPU affinity

V1 的公开操作只支持把当前线程固定到一个 CPU：

- `cpu_index` 是 Linux logical CPU index；
- 成功表示调用返回时线程 affinity 只包含该 CPU；
- implementation 先验证 index 能由内部 CPU-set representation 表达，无法表达时
  返回 `std::errc::invalid_argument`；
- 不预先通过 sysfs 查询 CPU 是否存在、在线或允许，避免重复且存在竞争的检查；
- CPU 不存在、离线或不在当前 cpuset/cgroup 许可范围内时，保留
  `pthread_setaffinity_np()` 返回的 system error；
- 设置成功可能使当前线程在调用返回前迁移到目标 CPU。

多 CPU affinity mask 暂不进入 V1。机器人硬件驱动的首要场景是把一个 RT driver
thread 固定到一个 CPU；为通用 mask 提供容器、动态大小 CPU set 或公开
`cpu_set_t` 会扩大 Interface。出现明确的多 CPU 使用需求后再增加独立操作。

### 6.4 Independent operations

V1 不提供聚合 `configure_current_thread(...)`。scheduling 和 affinity 是两个
独立 system operation，任一步都可能失败，无法提供可靠的跨调用事务或通用回滚。

调用者显式决定顺序并逐项处理错误。推荐在进入周期循环前先设置 affinity，再设置
realtime scheduling policy；进程内存锁定使用后续独立 Interface。

### 6.5 Error and realtime-safety contract

- 成功返回空的 `std::error_code`；
- 非法 policy 枚举值、非法 policy/priority 组合或无法表达的 CPU index 返回
  `std::errc::invalid_argument`；
- priority range 查询失败以及 scheduling、权限、resource limit、cpuset 和 kernel
  错误保留为 system-category error；
- 失败不触发隐藏 fallback，例如从 `Fifo` 降级为 `Normal`；
- library 不记录或恢复调用前的线程配置；
- 两个配置操作均不声明为 realtime-safe，不得在周期热路径调用；
- 函数不抛异常，不创建线程，不进行 library-owned 动态内存分配。

### 6.6 V1 exclusions

V1 不提供：

- 配置其他线程；
- thread creation attributes；
- 统一 `ThreadConfig` 和自动回滚；
- 多 CPU affinity mask；
- `SCHED_DEADLINE`、`SCHED_BATCH` 或 `SCHED_IDLE`；
- nice value、cgroup/cpuset 管理；
- CPU isolation、IRQ affinity 或 governor 配置；
- thread name、signal mask 或 stack size 配置；
- 自动权限提升或 resource-limit 修改。

## 7. Process Memory Locking

状态：已冻结

### 7.1 Process-wide operation

V1 冻结以下唯一操作：

```cpp
[[nodiscard]] std::error_code lock_process_memory() noexcept;
```

该操作固定请求 Linux `mlockall(MCL_CURRENT | MCL_FUTURE)`：

- `MCL_CURRENT` 锁定调用时已经映射的进程页面；
- `MCL_FUTURE` 使之后建立的 mappings 也进入锁定语义；
- 不使用 `MCL_ONFAULT`，因为首次访问 page fault 与避免实时路径缺页延迟的目标
  冲突；
- 不公开 Linux flags，不提供弱化或任意组合模式。

成功只证明 kernel 接受了本次 process address-space lock 请求，不证明调用者未来
执行路径不会发生 page fault。

### 7.2 Ownership

memory locking 是 process-wide 状态，不属于调用线程或某个 C++ 对象。因此：

- 不提供 `MemoryLock` RAII class；
- library 不记录引用计数或“是否由我锁定”的进程内状态；
- 应用负责协调唯一的 lock 时机；
- 重复调用直接服从 Linux 行为，不由 library 提供额外嵌套语义。

V1 不提供 `unlock_process_memory()`。`munlockall()` 会解除整个进程的所有 memory
locks，无法只撤销某个对象或调用者建立的状态；把它包装为局部资源释放会表达错误
所有权。进程退出或 `execve()` 的系统行为不由 library 再包装。

### 7.3 Error contract

- 成功返回空的 `std::error_code`；
- `mlockall()` 的权限、`RLIMIT_MEMLOCK`、可用内存和 kernel 错误保留为
  system-category error；
- 失败不自动修改 resource limit、不请求 capability，也不退化为只锁当前页面；
- library 不打印日志、不终止进程、不隐藏诊断；
- 调用不抛异常，但不声明为 realtime-safe。

### 7.4 Required caller preparation

调用者必须在进入实时循环前：

1. 建立并初始化所需 mappings、线程栈和 I/O resources；
2. 调用 `lock_process_memory()` 并处理失败；
3. 预分配并实际写入实时路径会使用的 stack、heap 和 copy-on-write pages；
4. 触发 lazy binding 或其他首次使用路径；
5. 通过 page-fault counters 和目标负载验证实时段没有意外缺页。

调用成功后继续分配内存可能因 locked-memory limit 失败；stack growth 也可能失败。
进入实时运行阶段后不得调用 `fork()`，因为后续 copy-on-write 会重新引入 page
fault 风险。

### 7.5 Stack-prefault decision

V1 不提供通用 `prefault_current_thread_stack(bytes)`：

- library 无法从字节数证明调用者剩余 stack 足够；
- stack exhaustion 可能产生 `SIGSEGV`，不能可靠表达为 `std::error_code`；
- helper 不能证明用户 driver 的完整 working set 已被触碰；
- caller-owned thread 模型意味着 stack size 和最坏调用深度仍由调用者控制。

可以在 validation 工具中提供特定测试程序，但不得把它描述成通用安全保证。若真实
驱动反复需要相同能力，必须基于明确前置条件重新设计，而不是直接封装 `alloca()`。

### 7.6 V1 exclusions

V1 不提供：

- per-range `mlock()` 或 `mlock2()` wrapper；
- `MCL_ONFAULT` policy；
- `munlockall()` wrapper；
- resource-limit 或 Linux capability 管理；
- swap、OOM、NUMA 或 huge-page 配置；
- 自动 stack/heap prefault；
- page-fault elimination 保证。

## 8. Measurement And Statistics

状态：已冻结

本节冻结测量定义、公开类型、累加语义和所有权。

### 8.1 Per-cycle observation

一次成功的 periodic wait 已提供：

- `scheduled_time`；
- `wake_time`；
- `period`；
- `lateness`；
- `missed_releases`。

调用者在本次 driver logic 完成后显式读取 `completion_time = Clock::now()`，并将它
与 wait observation 一起提交给统计设施。Realtime 不接收 callback，也不替调用者
决定哪段代码属于本周期工作。

### 8.2 Frozen metric definitions

对第 `n` 个成功周期定义：

```text
release_latency(n) = max(wake(n) - scheduled(n), 0)

execution_time(n) = completion(n) - wake(n)

wake_interval_error(n) =
    (wake(n) - wake(n-1)) - (scheduled(n) - scheduled(n-1))

jitter(n) = abs(wake_interval_error(n))

          = abs(release_latency(n) - release_latency(n-1))

overrun(n) = max(completion(n) - (scheduled(n) + period(n)), 0)
```

约束：

- `execution_time` 包含 `wait_next()` 记录 wake time 后，到调用者记录 completion
  time 前的 library return overhead 和 driver logic；
- 第一条 observation 没有前序样本，因此不产生 jitter sample；
- 使用相邻 `scheduled_time` 之差而不是固定一个 period 计算 jitter，使
  `SkipMissed` policy 下跳过 release 的行为仍有明确基准；
- `overrun > 0` 表示本周期工作在下一个原始 schedule release 后完成；
- `period(n)` 来自对应 `PeriodicWaitResult`，schedule 后续重新配置不改变已经提交的
  observation；
- 自定义小于 period 的业务 deadline 不属于 V1 统计语义；
- failed wait 不产生 timing observation。

最后一个等式来自成功 observation 的 `wake(n) >= scheduled(n)` 不变量。因此 V1
中的 jitter 明确定义为相邻 release-latency samples 的绝对差，同时保留
`wake_interval_error` 形式说明其 schedule-relative 含义。`jitter` 不作为未定义的
宣传术语使用。

### 8.3 Public Interface

V1 冻结以下 fixed-storage types：

```cpp
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
};
```

`TimingStatistics` 不可复制、不可移动，避免复制累加状态或模糊单线程所有权。
`TimingSnapshot` 是独立 value，必须满足 `Buffer<T>` 的 V1 payload constraints，因此
可以由 RT thread 通过 `Buffer<TimingSnapshot>` 发布给 NRT reader。

### 8.4 Observation acceptance

`try_observe(wait, completion_time)` 只在以下条件全部满足时接受 observation：

- `wait.error` 为空；
- `wait.period > Duration::zero()`；
- `wait.wake_time >= wait.scheduled_time`；
- `wait.lateness == wait.wake_time - wait.scheduled_time`；
- `completion_time >= wait.wake_time`；
- 本次 observation 所需的所有 `TimePoint` 和 `Duration` 运算均可表达。

成功返回 `true` 并提交完整统计更新。任一条件不满足时返回 `false`，accumulator 保持
调用前状态；拒绝不计入 saturation。`try_observe()` 不绑定或查询
`PeriodicSchedule`，只消费调用者提供的 self-contained observation。

第一次成功 observation 不产生 jitter sample。后续每个成功 observation 与前一个
成功 observation 共同产生一个 jitter sample。schedule 重新配置不会被
`TimingStatistics` 隐式检测：

- 如果调用者希望统计描述单次连续配置，应在重新配置后调用 `reset()`；
- 如果不 reset，跨配置的相邻 observations 仍按第 8.2 节公式产生 jitter。

### 8.5 Snapshot semantics

`snapshot()` 返回调用时 accumulator 的完整副本：

- `cycle_count` 是成功接受的 observation 数量；
- release latency 和 execution time 的 sample count 都等于 `cycle_count`；
- `jitter_sample_count` 是实际累计的 jitter sample 数量；
- `overrun_count` 只统计 `overrun > Duration::zero()` 的 observations；
- `maximum_overrun` 在没有 overrun 时为 zero；
- `missed_release_count` 是所有成功 observation 的 `missed_releases` 总和。

每个非空 `DurationSummary` 的 `minimum`、`maximum` 和 `mean` 分别表示对应 samples 的
最小值、最大值和算术平均值。mean 使用整数 nanoseconds，正数除法产生余数时向下
取整，不使用 floating-point。空 accumulator 的所有 count、duration 和 summary
fields 均为 zero，`saturated` 为 `false`。

summary 是否为空由其外部 sample count 判断：release latency 和 execution time 使用
`cycle_count`，jitter 使用 `jitter_sample_count`。V1 不在每个 `DurationSummary` 中
重复保存 count。

### 8.6 Saturation semantics

所有 counter 和内部 duration total 使用 checked、saturating arithmetic，不允许
signed 或 unsigned overflow：

- 任一 counter 或 total 将溢出时饱和在其可表达最大值；
- `saturated` 变为 `true`，并保持为 `true` 直到 `reset()`；
- structurally valid observation 即使触发 saturation，`try_observe()` 仍返回 `true`；
- minimum 和 maximum 继续根据后续成功 observations 更新；
- saturation 后 counter 是 lower bound，受影响 metric 的 mean 不再保证精确；
- 调用者必须在依赖精确 count 或 mean 前检查 `saturated`。

单个 observation 的时间运算无法表达属于输入拒绝，而不是 accumulator saturation，
必须返回 `false` 且不修改任何状态。

### 8.7 Ownership and concurrency

- accumulator 由一个线程独占写入；
- `try_observe`、`snapshot` 和 `reset` 不保证并发调用安全；
- 不使用 mutex 或 atomic 伪装共享统计对象；
- 不分配动态内存，不抛异常；
- 操作必须具有有界执行时间，目标是允许在 RT thread 调用；
- RT/NRT 间发布统计 snapshot 时，由调用者使用 `Buffer<TimingSnapshot>`；
- library 不建立全局 registry、singleton 或自动 collector。

### 8.8 Explicit collection

概念使用模型：

```cpp
TimingStatistics statistics;

while (running) {
  const auto wait = schedule.wait_next();
  if (!wait) {
    // Caller-owned error policy.
    continue;
  }

  run_driver_cycle();

  if (!statistics.try_observe(wait, Clock::now())) {
    // Caller-owned rejected-observation policy.
  }
}
```

代码表达调用者显式选择测量结束点和统计所有权。

### 8.9 V1 exclusions

V1 不提供：

- stopwatch、scope timer 或 callback wrapper；
- percentile、histogram、standard deviation 或 quantile estimator；
- tracing、logging 或 metrics exporter；
- process-wide 或 cross-thread aggregation；
- 自动滑动窗口；
- CPU execution-time clock；
- deadline enforcement；
- 跨机器测量或 clock synchronization。

validation 工具可以提供更丰富的离线分析，但不得扩大 production Interface。

## 9. `Queue<T, Capacity>`

状态：已冻结

### 9.1 Public surface

V1 冻结以下最小 Interface：

```cpp
template <typename T, std::size_t Capacity>
class Queue final {
 public:
  using value_type = T;

  Queue() noexcept;
  ~Queue() noexcept;

  Queue(const Queue&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(Queue&&) = delete;

  [[nodiscard]] bool try_push(const T& value) noexcept;
  [[nodiscard]] bool try_pop(T& value) noexcept;

  [[nodiscard]] static constexpr std::size_t capacity() noexcept {
    return Capacity;
  }
};
```

`Capacity` 是真实可用容量：Queue 必须能够同时保存恰好 `Capacity` 个尚未消费的
元素。它不包含 implementation 为区分 full/empty 可能保留的内部 slot，也不得因
内部 ring 表示而把实际容量降为 `Capacity - 1`。`Capacity` 必须大于零。

### 9.2 Producer and consumer roles

- 整个并发使用期间恰好一个 producer 调用 `try_push()`；
- 整个并发使用期间恰好一个 consumer 调用 `try_pop()`；
- producer 和 consumer 可以是不同线程；
- library 不通过 thread ID 绑定或运行时检查角色；
- role transfer 只允许在调用者已经建立外部同步、且 Queue 没有并发操作时发生；
- 构造和析构时不得存在并发访问；
- Queue 本身不可复制、不可移动，避免复制并发状态或改变对象身份。

违反 SPSC 或生命周期契约属于调用者错误，并可能产生 data race 或 undefined
behavior。

### 9.3 Push and pop semantics

`try_push(value)`：

- 有空间时复制一个 `T` 到 FIFO 尾部并返回 `true`；
- full 时立即返回 `false`；
- 失败不得修改 Queue 或 `value`；
- 成功只发布 `T` 的副本，不转移 `value` 或其间接资源的所有权。

`try_pop(value)`：

- 有元素时把 FIFO 头部复制赋值给 `value`，移除该元素并返回 `true`；
- empty 时立即返回 `false`；
- 失败不得修改 `value` 或 Queue；
- 成功元素严格遵循成功 push 的 FIFO 顺序。

两个操作都不等待另一端，不重试到成功，也不调用 yield、sleep 或 backoff。full 和
empty 是正常流控结果，不通过 `std::error_code` 或 exception 表达。

### 9.4 Payload type constraints

V1 冻结以下要求：

```cpp
static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>);
static_assert(std::is_trivially_copy_constructible_v<T>);
static_assert(std::is_trivially_copy_assignable_v<T>);
static_assert(std::is_trivially_destructible_v<T>);
static_assert(std::is_nothrow_copy_constructible_v<T>);
static_assert(std::is_nothrow_copy_assignable_v<T>);
```

这些约束允许 implementation 使用对象内的未初始化固定 slot storage。`T` 明确
不需要 default-constructible；同时把用户代码、动态生命周期和异常排除在 Queue
操作之外。

类型约束不能证明 payload 的语义所有权。若 `T` 含 pointer、handle 或 view，Queue
只复制这些值，不延长 pointee 或外部资源生命周期。实时驱动应优先使用完全 inline
owned、固定大小的 payload。

### 9.5 Memory-model contract

对每个成功传输的元素：

- producer-to-consumer：producer 在发布该元素前 sequenced-before 的写入，通过
  publication release 和 consumer 的匹配 acquire 建立 happens-before，因此对
  consumer 成功取得该元素后的操作可见；
- consumer-to-producer：consumer 完成 payload 复制后，通过 slot-retirement
  release 和 producer 的匹配 acquire 建立 happens-before；producer 只有观察到该
  状态后才能再次写入同一个 slot；
- `try_push(false)` 和 `try_pop(false)` 不建立跨线程数据发布关系；
- FIFO 可见性只适用于同一个 Queue 实例和符合 SPSC 契约的调用。

具体 ring layout、index representation、padding 和 cache-line placement 属于
implementation，不进入公开 Interface。

### 9.6 Realtime-safety contract

- Queue 使用 fixed-capacity inline storage，构造及操作均不动态分配；
- `try_push()` 和 `try_pop()` 只执行有界数量的 payload 操作和 atomic operation；
- library 不使用 mutex、condition variable、syscall、sleep 或 hidden retry loop；
- implementation 使用的 atomic 类型必须在支持平台上证明 always lock-free，否则
  编译或平台验证必须拒绝该实现；
- 单次操作复杂度为 `O(1)`，但实际成本仍包含 `sizeof(T)` 对应的复制成本；
- library 只保证自身路径，不保证调用者访问的间接资源具有确定性。

在算法审查、目标平台 atomic 验证和并发测试完成前，项目不把 Queue 宣传为
lock-free、wait-free 或 realtime-safe。最终声明必须逐项对应上述行为契约。

### 9.7 Deliberate omissions

V1 不提供：

- blocking `push()` 或 `pop()`；
- overwrite-oldest policy；
- multi-producer 或 multi-consumer；
- runtime capacity；
- `size()`、`empty()` 或 `full()` 瞬时观察；
- iterator、peek、batch operation；
- `emplace()`、rvalue overload 或 `std::optional<T>` 返回；
- allocator；
- Queue 内部统计计数器。

失败计数、重试、丢弃和告警策略由调用者显式实现。

## 10. `Buffer<T>`

状态：已冻结

### 10.1 Public surface

V1 冻结以下最小 Interface：

```cpp
template <typename T>
class Buffer final {
 public:
  using value_type = T;

  Buffer() noexcept;
  ~Buffer() noexcept;

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&&) = delete;
  Buffer& operator=(Buffer&&) = delete;

  void write(const T& value) noexcept;
  [[nodiscard]] bool try_read(T& value) noexcept;
};
```

Buffer 默认处于 empty/uninitialized 状态，不 default-construct `T`，也不提供隐式
初始值 constructor。调用者可以在并发访问开始前显式 `write(initial_value)`。

### 10.2 Writer and reader roles

- 整个并发使用期间恰好一个 writer 调用 `write()`；
- 整个并发使用期间恰好一个 reader 调用 `try_read()`；
- writer 和 reader 可以是不同线程；
- library 不记录 thread ID，也不在运行期检查角色；
- writer/reader 角色必须在并发使用开始前确定，并在 Buffer 的整个并发使用期保持
  固定；V1 不支持运行时角色转换；
- 构造和析构时不得存在并发访问；
- Buffer 不可复制、不可移动。

违反单写者/单读者契约属于调用者错误，并可能产生 data race 或 undefined
behavior。

### 10.3 Write semantics

`write(value)`：

- 复制并发布一个完整的新值；
- 始终完成，不因 reader 状态返回失败；
- 不等待读取，不重试，不调用 yield、sleep 或 backoff；
- 可以覆盖尚未被 reader 观察的旧 publication；
- 返回后调用者仍拥有 `value`，Buffer 只保存其副本。

连续 write 允许替换尚未读取的旧 publication，不承诺 reader 观察每个中间值。
需要顺序和逐项交付时必须使用 Queue。

### 10.4 Read semantics

`try_read(value)`：

- 尚未观察到任何成功 write 时返回 `false`，且不得修改 `value`；
- 有已发布值时返回 `true`，并把一个完整一致的 snapshot 复制给 `value`；
- `true` 只表示存在可读值，不表示本次读到了新版本；
- 没有新 write 时可以重复读取同一个当前值，每次均返回 `true`；
- 返回某一次完整 publication 的 snapshot，不保证为调用返回时刻的最新值；
- 与 write 并发时，新 publication 可以在本次或后续读取中观察；
- 不返回 torn、部分更新或由多个 publication 混合的 `T`。

`latest-value` 表示 Buffer 可以合并更新并丢弃旧值，不表示对调用返回时刻提供全局
freshness 或跨线程 wall-clock 顺序保证。

### 10.5 Payload constraints

Buffer 对 `T` 采用与 Queue 相同的约束：non-const、non-volatile、trivially and
nothrow copy-constructible、trivially and nothrow copy-assignable，以及 trivially
destructible。`T` 不需要 default-constructible。

若 `T` 包含 pointer、handle 或 view，Buffer 只复制其值，不管理间接资源生命周期。
推荐使用完全 inline owned、固定大小的 state、status 或 telemetry payload。

### 10.6 Memory-model contract

对 reader 实际观察到的 publication：

- writer 在 publication 前 sequenced-before 的写入，通过 release/acquire 建立
  happens-before，对成功 read 后的 reader 可见；
- reader 完成 snapshot 复制后，必须通过反向 release/acquire 关系释放对应 storage，
  writer 才能重用；
- writer 不得修改 reader 正在复制的 storage；
- 未被 reader 观察的中间 publication 可以被后续 write 替换；
- `try_read(false)` 不建立 payload publication 的 happens-before 关系。

内部采用 double buffer、triple buffer、index exchange 或其他算法不属于公开
Interface。implementation 必须通过算法审查证明上述 slot ownership 和同步关系。

### 10.7 Realtime-safety contract

- 固定 storage 全部包含在 Buffer 对象自身；
- 构造、write 和 read 均不动态分配；
- 单次操作只执行有界数量的 payload copy 和 atomic operation；
- 不使用 mutex、condition variable、syscall 或 library-level hidden retry loop；
- implementation 使用的 atomic 类型必须在支持平台 always lock-free；
- Buffer 的 atomic exchange 在某些 CPU 上可由带 retry 的硬件原语实现；RT-callable
  只承诺固定的 library-level API 步骤，不承诺底层指令无 retry；
- 单次操作复杂度为 `O(1)`，实际成本包含 `sizeof(T)` 的复制成本；
- library 不保证 payload 指向的间接资源具有确定性。

与 Queue 一样，在算法和平台证据完成前不宣传 lock-free、wait-free 或
realtime-safe，只声明已经验证的具体行为。

### 10.8 Deliberate omissions

V1 不提供：

- `has_value()`、`is_new()` 或 `version()`；
- blocking wait 或 update notification；
- multi-writer 或 multi-reader；
- in-place mutation、reference、pointer 或 view 返回；
- `emplace()`、rvalue overload 或 `std::optional<T>` 返回；
- timestamp 自动附加；
- history、queueing 或丢失更新计数；
- runtime-selected storage strategy。

freshness、sequence number 和 timestamp 是 payload 或调用者协议的一部分，不由
Buffer 隐式添加。

### 10.9 Queue and Buffer roles

| 原语 | 数据语义 | 典型用途 |
|---|---|---|
| `Queue<T, Capacity>` | 有界事件流，保持成功写入顺序 | command、event、request |
| `Buffer<T>` | 状态 snapshot，允许丢弃旧值 | state、sensor、feedback |

## 11. Realtime-safety Claims And Evidence

状态：已冻结

### 11.1 Operation-level classification

V1 不把整个 realtime component 统一宣称为 realtime-safe。每个公开操作必须单独
标记为以下类别之一。

#### RT-callable

满足相应前置条件和平台验证后，允许在 RT hot path 调用：

- `Clock::now()`；
- `PeriodicSchedule::wait_next()`；
- `Queue::try_push()` 和 `Queue::try_pop()`；
- `Buffer::write()` 和 `Buffer::try_read()`；
- timing statistics 的 `try_observe()`、`snapshot()` 和 `reset()`。

`PeriodicSchedule::wait_next()` 是显式等待操作：它有意阻塞到 absolute target，且
可能因 `EINTR` 针对同一 target 重试。RT-callable 不表示 non-blocking 或 wait-free。

`TimingStatistics::reset()` 的 RT-callable 承诺仅指把固定 accumulator 恢复为空统计
状态。它不释放内存、不重建 storage、不访问全局 collector，也不触发输出。

#### Setup-only

只能在进入 RT hot path 前调用：

- `set_current_thread_scheduling()`；
- `set_current_thread_affinity()`；
- `lock_process_memory()`；
- 对象构造、销毁和角色建立；
- `PeriodicSchedule::configure()`，包括首次配置和重新定相。

#### Non-RT

不属于实时环境建立步骤，也不得在 RT hot path 调用的普通功能，例如未来可能增加的
报告格式化、文件输出或诊断工具 Interface。Non-RT 操作可以有与其文档一致的分配、
锁或 I/O 行为，但不得被 RT-callable 路径隐式调用。

V1 production Interface 当前不要求提供 Non-RT 操作。保留该分类是为了防止未来把
“不是 RT-callable”的所有功能错误归入 Setup-only。

### 11.2 Library-side guarantee

一个操作只有在文档明确标记后才能称为 RT-callable。该标记至少保证 library 自身
执行路径：

- 不抛异常；
- 不进行 dynamic allocation 或 deallocation；
- 不获取 mutex、condition variable 或其他可能睡眠的用户态锁；
- 不打印日志、不格式化字符串、不调用用户 callback；
- 不执行未声明的 syscall、sleep、yield 或 backoff；
- 除显式 wait 和 `EINTR` 处理外，不包含无界 retry loop；
- 使用的 atomic operation 已在支持平台证明 always lock-free；
- 计算步骤有界，payload copy 成本由公开的 `T` 约束限定。

### 11.3 What is not guaranteed

RT-callable 不保证：

- 调用者一定满足 deadline；
- kernel、scheduler、firmware 或 hardware latency 上界；
- CPU cache miss、TLB miss、interrupt 或 preemption 不发生；
- 调用者代码、间接资源或外部 library 不分配、不加锁；
- 尚未预触碰的页面不会产生 page fault；
- 不同 kernel、CPU 和系统负载具有相同延迟；
- `lock-free` 自动等于系统级 realtime-safe。

### 11.4 Required evidence

每个 RT-callable 声明必须至少有：

1. Interface 前置条件和失败语义；
2. implementation 路径审查；
3. compile-time payload 和 atomic capability checks；
4. 检查最终链接产物不依赖隐藏的 atomic runtime fallback；
5. deterministic unit tests 和边界测试；
6. Queue/Buffer 的 C++ memory-model 论证、wrap-around 测试和长时间并发压力测试；
7. sanitizer 或并发检测工具结果，并明确它们不是正确性证明；
8. allocation、locking 和 syscall audit；
9. x86_64 与 aarch64 目标环境验证；
10. PREEMPT_RT 上可重复的 latency、jitter、execution-time 和 page-fault 报告。

没有对应证据时，只能描述具体已知行为，不得使用 lock-free、wait-free 或
realtime-safe 标签。

### 11.5 Documentation rule

每个公开函数的文档必须包含 `Realtime safety` 段落，明确写出：

- `RT-callable`、`Setup-only` 或 `Non-RT`；
- 是否等待以及等待什么；
- 是否执行 syscall；
- 是否依赖 payload operation；
- 所需线程角色和外部同步；
- 未满足前置条件时的结果。

营销材料和 README 不得扩大该逐操作契约。

## 12. Error And Construction Semantics

状态：已冻结

### 12.1 No exceptions

状态：已冻结

`realtime` 的全部 V1 public Interface 都不抛异常，而不只限于
RT-callable 操作：

- public functions 和 constructors 在可表达处标记 `noexcept`；
- 正常但暂时无法完成的动作通过 Boolean Status 返回；
- 可预期、可恢复且需要原因的失败通过 `std::error_code` 或领域 Observation 返回；
- library 不因 configuration 或 system failure 抛出 exception；
- library 不把可恢复失败转换为日志、fallback 或进程终止；
- library 不捕获并隐藏 caller failure，也不通过 callback、registry 或 thread tracking
  建立另一套异常或错误机制。

这比“仅 RT hot path 不抛异常”更容易理解和验证，也避免同一个类型因调用阶段不同
具有两套错误模型。

#### Fatal invariant failure

Value Query 和 Command 没有可恢复失败路径。如果 implementation 因不受支持的运行
环境或自身缺陷而无法履行成功契约，就不得返回伪造值或假装命令完成。V1 允许这种
implementation-controlled invariant failure fail-fast。

典型例子是使用固定有效 clock id 和有效输出地址读取 `CLOCK_MONOTONIC` 仍被 kernel
拒绝。该情况不是调用者可通过 `Clock::now()` 参数修复的运行时错误，也不应污染每次
时间读取的返回类型。

fail-fast 的具体机制属于 Implementation，不形成可配置的公共 error policy。该规则：

- 只适用于无法继续满足公开成功契约的致命不变量破坏；
- 不得用于替代 configuration、permission、resource 或其他已定义的错误返回；
- 不为失败后的 RT latency 或程序继续执行提供保证。

### 12.2 Return-semantics classification

状态：已冻结

V1 不让每个函数任意选择返回形式，也不为表面统一而引入 generic `Result<T>`。
每个公开操作必须按返回语义归入以下五种模式之一。

#### Value Query

操作返回一个值，并在满足公开前置条件时必然完成：

```cpp
T operation(...) noexcept;
```

返回值就是完整结果，不需要 error、engagement state 或其他包装。V1 示例包括：

- `Clock::now()`；
- `Queue::capacity()`。

#### Command

操作执行一个命令，在满足公开前置条件时成功完成即返回，不产生额外结果：

```cpp
void operation(...) noexcept;
```

`void` 不表示调用者忽略了结果，而表示 Interface 已经保证该操作没有运行时失败路径。
如果操作可能失败并需要调用者处理，就不得归入 Command。V1 示例包括：

- `Buffer::write()`；
- `TimingStatistics::reset()`。

#### Boolean Status

操作返回一个不需要额外诊断信息的布尔结果：

```cpp
[[nodiscard]] bool operation(...) noexcept;
```

Boolean Status 包含两种不同命名语义。

Try Action 使用 `try_` 前缀，表示尝试执行一次动作；因当前数据或对象状态而无法完成
属于正常结果。`true` 表示动作完成，`false` 表示未完成，且不得产生部分更新。V1
示例包括：

- `Queue::try_push()`：Queue full 时返回 `false`；
- `Queue::try_pop()`：Queue empty 时返回 `false`；
- `Buffer::try_read()`：尚无 publication 时返回 `false`；
- `TimingStatistics::try_observe()`：输入 observation 无法被接受时返回 `false`，且
  accumulator 保持不变。

Boolean Query 使用 `is_` 或 `has_` 等 predicate 名称，表示查询一个简单状态。V1
示例是 `PeriodicSchedule::is_configured()`。

Boolean Status 描述的是返回形式及其无需诊断的语义，不意味着所有操作都在查询对象
状态，也不要求所有返回 `bool` 的操作使用 `try_` 前缀。

#### Error-reporting

操作建立或改变某种状态，但可能因参数、OS、权限或系统资源等原因失败，调用者需要
获得失败原因：

```cpp
[[nodiscard]] std::error_code operation(...) noexcept;
```

成功返回空的 `std::error_code`。V1 示例包括：

- `PeriodicSchedule::configure()`；
- `set_current_thread_scheduling()`；
- `set_current_thread_affinity()`；
- `lock_process_memory()`。

`std::error_code` 尽量保留 system 或 generic category。只有 Linux errno 和
`std::errc` 无法准确表达稳定的 Realtime 领域错误时，才建立 component-local error
category。

#### Observation

操作产生调用者需要整体解释的一组领域事实，因此返回固定的领域结果结构：

```cpp
[[nodiscard]] OperationResult operation(...) noexcept;
```

结果结构可以同时携带成功 observation 和该操作特有的失败信息，但不得退化成公共
generic `Result<T>`。V1 示例包括：

- `PeriodicSchedule::wait_next()` 返回 `PeriodicWaitResult`；
- `TimingStatistics::snapshot()` 返回统计 snapshot 类型。

Observation 不是 `value + error` 的 generic wrapper，而是具体领域模型。结果可以
包含该领域事件的 error field，但不要求所有 Observation 都可失败；例如统计 snapshot
不需要 error state。

每个操作只归入一个模式。分类由调用者如何解释结果决定，而不是仅由 C++ 返回类型
决定。

| 公开操作 | 返回语义 |
|---|---|
| `Clock::now()` | Value Query |
| `Queue::capacity()` | Value Query |
| `Buffer::write()` | Command |
| `TimingStatistics::reset()` | Command |
| `Queue::try_push()` | Boolean Status / Try Action |
| `Queue::try_pop()` | Boolean Status / Try Action |
| `Buffer::try_read()` | Boolean Status / Try Action |
| `TimingStatistics::try_observe()` | Boolean Status / Try Action |
| `PeriodicSchedule::is_configured()` | Boolean Status / Boolean Query |
| `PeriodicSchedule::configure()` | Error-reporting |
| `set_current_thread_scheduling()` | Error-reporting |
| `set_current_thread_affinity()` | Error-reporting |
| `lock_process_memory()` | Error-reporting |
| `PeriodicSchedule::wait_next()` | Observation |
| `TimingStatistics::snapshot()` | Observation |

compile-time template 约束不属于运行时返回语义，继续通过 `static_assert` 拒绝。

### 12.3 PeriodicSchedule lifecycle and configuration

状态：已冻结

`PeriodicSchedule` 是调用者拥有的显式状态对象，不是构造完成后必然可以立即等待的
值对象。V1 提供不会失败的默认构造和独立配置操作：

```cpp
class PeriodicSchedule final {
 public:
  PeriodicSchedule() noexcept = default;

  PeriodicSchedule(const PeriodicSchedule&) = delete;
  PeriodicSchedule& operator=(const PeriodicSchedule&) = delete;
  PeriodicSchedule(PeriodicSchedule&&) = delete;
  PeriodicSchedule& operator=(PeriodicSchedule&&) = delete;

  [[nodiscard]] std::error_code configure(
      TimePoint first_release,
      Duration period,
      MissedPeriodPolicy policy) noexcept;

  [[nodiscard]] bool is_configured() const noexcept;
  [[nodiscard]] PeriodicWaitResult wait_next() noexcept;
};
```

默认构造后的对象处于明确的 unconfigured 状态。该状态是完整、有效且可查询的
lifecycle 状态，不是部分初始化或不可使用的对象：

- `is_configured()` 返回 `false`；
- `wait_next()` 返回 `std::errc::operation_not_permitted`，不等待且不修改对象；
- 成功调用 `configure()` 后进入 configured 状态；
- configured 对象可以在无并发访问时再次调用 `configure()`，成功后以新的时间网格
  重新定相；
- `configure()` 和 `is_configured()` 不隐式读取 `Clock::now()`。

`configure()` 按以下规则验证输入并返回错误：

- `period <= Duration::zero()` 返回 `std::errc::invalid_argument`；
- `policy` 不是公开枚举值时返回 `std::errc::invalid_argument`。

配置只验证并保存输入，不预先计算无界的未来 release 序列，因此不存在“内部状态
无法初始化”这一额外失败模式。

配置具有 strong failure guarantee：implementation 必须在修改可观察状态前完成输入
验证；失败时对象保持调用前状态。因此，首次配置失败后仍为 unconfigured，重新配置
失败后仍保留此前完整 schedule。

由于周期数量没有上限，`configure()` 无法证明未来任意
`first_release + n * period` 都不会溢出。后续推进发生不可表达的时间运算时，
`wait_next()` 返回 `std::errc::value_too_large`，不推进当前 schedule；调用者可以
重新配置或退出。

对象不可复制、不可移动，避免隐式转移推进状态或并发身份。构造、`configure()` 和
销毁属于 Setup-only；`is_configured()` 与 `wait_next()` 在满足无并发配置等前置条件
时属于 RT-callable。V1 不提供静态 factory、`std::optional<PeriodicSchedule>`、
generic `Result<T>` 或 out-parameter construction。

`configure()` 不 clamp period、不改变 `first_release`，也不 fallback 为其他 policy。
上述错误可由 `std::errc` 表达，因此这些情况不需要 component-local error category。

### 12.4 Periodic wait result

状态：已冻结

V1 冻结以下 policy 和 result Interface：

```cpp
enum class MissedPeriodPolicy {
  CatchUp,
  SkipMissed,
};

struct PeriodicWaitResult final {
  std::error_code error{};

  TimePoint scheduled_time{};
  TimePoint wake_time{};
  Duration period{};
  Duration lateness{};
  std::uint64_t missed_releases{0};

  [[nodiscard]] explicit operator bool() const noexcept {
    return !error;
  }
};
```

`wait_next()` 需要同时返回完整 timing observation 和该操作特有的 system 或状态
错误，因此使用专用固定结构，而不是 generic `Result<T>`。

成功结果满足：

- `error` 为空，显式 `bool` conversion 为 `true`；
- 所有 timing fields 有效；
- `wake_time >= scheduled_time`；
- `period > Duration::zero()`，且是生成本次 observation 时的 schedule period
  snapshot；
- `lateness = wake_time - scheduled_time`；
- `missed_releases` 是当前 `scheduled_time` 之后、在 `wake_time` 之前或恰好等于它的
  额外 release 数量；
- schedule 根据已配置 policy 成功推进。

后续重新配置 schedule 不改变已经返回的 `PeriodicWaitResult::period`。

失败结果采用 all-or-nothing validity：

- `error` 非空，显式 `bool` conversion 为 `false`；
- 所有 timing fields 均不具有领域意义，调用者不得使用；
- implementation 仍然确定性初始化所有字段，但初始化值不构成 Interface 语义；
- schedule 不推进；
- 调用者可以根据 error retry、重新配置或退出。

V1 不保留“失败时只有 `scheduled_time` 有效”之类的部分 observation 规则，避免调用者
根据具体错误记忆不同字段有效性组合。

错误映射为：

| 条件 | `error` |
|---|---|
| schedule 尚未配置 | `std::errc::operation_not_permitted` |
| 计算后续 release 时发生不可表达的时间运算 | `std::errc::value_too_large` |
| unexpected Linux wait failure | 对应的 system-category error |
| `EINTR` | 不返回；使用相同 absolute target 继续等待 |

schedule 只在完整计算并验证下一目标后提交状态：

```text
CatchUp:
    next = scheduled_time + period

SkipMissed:
    next = scheduled_time + (missed_releases + 1) * period
```

任一步骤发生溢出时返回失败结果，并保持当前 schedule 不变。

`missed_releases` 使用固定宽度的 `std::uint64_t`，不使用表达对象或容器大小的
`std::size_t`。它是与 policy 无关的 observation：

- 在 `CatchUp` 下，它表示当前目标之后仍已到达、后续调用需要追赶的 release 数量；
- 在 `SkipMissed` 下，它同时表示本次推进跳过的 release 数量。

result 不保存 missed-period policy，也不增加 sequence number、`was_skipped`、
optional observation 或字段访问器包装。`period` 是解释 overrun 所需的 observation
上下文；恢复 policy 不参与统计计算，因此不随结果重复返回。

result 是 fixed-storage value type，不分配动态内存。字段读取和显式 `bool` conversion
不抛异常，也不执行 syscall。

### 12.5 Programming contracts

状态：已冻结

#### Compile-time constraints

以下条件必须通过 `static_assert` 拒绝：

- `Queue<T, Capacity>` 的 `Capacity == 0`；
- Queue 或 Buffer payload 不满足已冻结的类型约束；
- 目标平台所需 atomic type 不是 always lock-free。

implementation 不得把这些情况降级为容量变化、锁实现或运行时错误。

#### Checked runtime input

能够低成本且确定性验证的输入必须通过已冻结的返回语义报告失败，包括：

- 非法 period 或 policy；
- 非法 scheduling policy/priority；
- 无法由内部 CPU-set representation 表达的 CPU index；
- `TimingStatistics` 无法接受的 `PeriodicWaitResult`；
- 不可表达的时间运算。

这些情况不得使用 assertion、undefined behavior、clamp 或 fallback 代替规定的失败
返回。implementation 必须在提交可观察状态前完成验证；失败不得留下部分状态。

#### Caller concurrency and lifetime contracts

无法低成本检测的条件保持为调用者必须满足的前置条件，包括：

- Queue 的 SPSC producer/consumer 角色；
- Buffer 的 single-writer/single-reader 角色；
- 对象构造或销毁时不存在并发访问；
- `TimingStatistics` 和 `PeriodicSchedule` 不被并发调用；
- 已声明的 role transfer、move 和外部同步限制。

违反这些约束并形成 C++ data race 时，行为是 undefined behavior。library 不通过
thread ID、mutex、registry 或动态状态追踪检测这些前置条件。

#### Contract and diagnostic limits

- 在 RT hot path 调用 Setup-only 操作不一定产生 C++ undefined behavior，但该调用
  不享有 RT-callable 保证；
- 失败 `PeriodicWaitResult` 的 timing fields 是确定性初始化的 C++ values，但没有
  领域意义；
- `[[nodiscard]]` 只请求 compiler diagnostic，不构成运行时检查；
- debug assertion 可以检查 Implementation 内部不变量，但不得替代 Interface 规定的
  错误返回；
- 有效输入在 debug 和 release builds 中必须具有相同可观察语义；
- 所有时间和统计运算必须在提交状态前检查，失败不得留下部分状态。

## 13. Public Header Layout

状态：已冻结

### 13.1 Public identity

Realtime 保持 Architecture Design v2 冻结的品牌公共身份：

```text
namespace:  realtime
include:    <realtime/...>
target:     realtime::realtime
package:    realtime
```

`realtime` 是可独立构建、安装、测试和迁移的 component。上述公共身份不表示它依赖
repository 中的其他 component，也不要求消费者通过 repository 根项目构建。

### 13.2 Installed headers

V1 安装以下 public headers：

```text
include/realtime/
├── buffer.hpp
├── clock.hpp
├── current_thread.hpp
├── periodic_schedule.hpp
├── process_memory.hpp
├── queue.hpp
└── timing_statistics.hpp
```

各 header 的公共职责为：

| Header | Public Interface |
|---|---|
| `buffer.hpp` | `Buffer<T>` |
| `clock.hpp` | `Clock`、`Duration`、`TimePoint` |
| `current_thread.hpp` | `SchedulingPolicy`、`set_current_thread_scheduling()`、`set_current_thread_affinity()` |
| `periodic_schedule.hpp` | `MissedPeriodPolicy`、`PeriodicWaitResult`、`PeriodicSchedule` |
| `process_memory.hpp` | `lock_process_memory()` |
| `queue.hpp` | `Queue<T, Capacity>` |
| `timing_statistics.hpp` | `DurationSummary`、`TimingSnapshot`、`TimingStatistics` |

每个 public header 必须独立可编译，直接包含自身声明和定义所需的标准库及本 component
headers，不依赖 include 顺序或传递包含。消费者只需包含直接使用的 public header。

### 13.3 Header dependencies

允许的 public-header 依赖为：

```text
periodic_schedule.hpp  -> clock.hpp
timing_statistics.hpp  -> periodic_schedule.hpp
```

`queue.hpp` 和 `buffer.hpp` 是独立的 template headers，不依赖其他 Realtime public
header。`current_thread.hpp` 和 `process_memory.hpp` 只依赖所需的 C++ standard library
Interface；Linux native types、constants 和 UAPI headers 不进入其公开函数签名。

`timing_statistics.hpp` 依赖 `periodic_schedule.hpp`，因为
`TimingStatistics::try_observe()` 的公开签名直接使用 `PeriodicWaitResult`。V1 不为打断
这一真实 Interface 依赖而拆出泛化的 observation 或公共 `types.hpp`。

### 13.4 Template implementation boundary

`Queue<T, Capacity>` 和 `Buffer<T>` 的完整模板定义保留在各自 public header 中。两个
header 必须自包含；V1 不创建或安装 `detail/`、`internal/`、`common/` 或 `types/`
目录，也不要求消费者包含实现 header。

消费者能够看到模板 Implementation 不表示其中所有实体都属于受支持的 Public
Interface。受支持 Interface 仅包括本文冻结的公开名称和行为契约。若未来出现必须随
模板安装的非契约辅助实现，必须重新评审其必要性和兼容性边界；非模板辅助代码则应
保留在 `src/` 内，不安装、不进入 public include path，并使用明确的领域名称。

### 13.5 No convenience or umbrella header

V1 不提供：

```cpp
#include <realtime.hpp>
#include <realtime/realtime.hpp>
#include <hardware.hpp>
```

Realtime 的 API 规模不足以证明 convenience header 的维护成本。是否增加 component
级 convenience header 必须依据真实 consumer 使用证据另行评审；跨 component 的
umbrella header 仍由 Architecture Design v2 明确排除。

## 14. References

- Linux `clock_gettime(2)`：<https://www.man7.org/linux/man-pages/man2/clock_gettime.2.html>
- Linux `clock_nanosleep(2)`：<https://www.man7.org/linux/man-pages/man2/clock_nanosleep.2.html>
- POSIX `clock_nanosleep()`：<https://pubs.opengroup.org/onlinepubs/009696899/functions/clock_nanosleep.html>
- Linux `pthread_setschedparam(3)`：<https://man7.org/linux/man-pages/man3/pthread_setschedparam.3.html>
- Linux scheduling priority range：<https://www.man7.org/linux/man-pages/man2/sched_get_priority_min.2.html>
- Linux `pthread_setaffinity_np(3)`：<https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html>
- Linux `mlockall(2)`：<https://www.man7.org/linux/man-pages/man2/mlockall.2.html>
