# Realtime Implementation Design

状态：已冻结

阶段：Realtime Implementation Design

最后更新：2026-09-12

## 1. 目的

本文定义 `realtime` V1 的 Implementation，包括源码组织、内部依赖、
Linux/POSIX seam、checked arithmetic、并发算法、对象存储、memory ordering 和验证
证据。

本文不修改已经冻结的 Realtime Interface。任何 Implementation 选择如果无法满足
`api.md`，必须返回 Interface Design 重新评审，不得通过增加公开类型、测试
hook、配置项或弱化契约来规避。

## 2. 上位约束

Implementation 必须同时满足：

- `../project-charter.md` 的项目定位和非目标；
- `../v1-requirements.md` 的 Realtime 范围与三级验证模型；
- `../architecture.md` 的 component-first、独立构建和 public/private seam；
- `api.md` 已冻结的类型、行为、错误、并发和 realtime-safety Interface。

V1 只使用 C++17 standard library 与 Linux/POSIX 系统 Interface。不得因实现或测试方便
引入公共 backend、runtime、executor、统一 platform layer 或其他 production component
依赖。

## 3. Implementation Goals

### 3.1 Correctness first

- 所有时间计算在 signed overflow 发生前检查；
- 所有状态变更在完整计算成功后一次提交；
- Queue 和 Buffer 在 C++ memory model 下不存在符合 Interface 前置条件时的 data race；
- full、empty、尚无 publication、非法配置和系统失败严格保持已冻结语义；
- debug/release、static/shared build 不改变可观察行为。

### 3.2 Bounded hot paths

RT-callable 路径必须具有可枚举的控制流。除 `wait_next()` 对同一 absolute target 的
显式等待和 `EINTR` 重试外，不允许隐藏 retry loop、blocking lock、allocation、日志或
callback。

### 3.3 Private variability

Linux syscall 替换只存在于 test build 的私有 link seam。生产库在链接时固定使用
Linux implementation；运行时不保存函数表，不执行 backend 选择，也不向 Public
Interface 暴露注入点。

### 3.4 Evidence before claims

算法说明、测试、编译期检查、二进制审计和目标平台结果共同构成 realtime-safety
证据。在相应 gate 通过前，Implementation 不使用 `lock-free`、`wait-free` 或
`realtime-safe` 作为组件级声明。

## 4. Decision Order

Implementation 按以下顺序评审并冻结：

1. production source layout 与 private OS seam；
2. 时间 representation conversion 与 checked arithmetic；
3. `Clock` 和 absolute sleep；
4. `PeriodicSchedule` 状态推进及错误提交；
5. current-thread scheduling、affinity 和 process memory locking；
6. `TimingStatistics` accumulator 与 saturation；
7. `Queue<T, Capacity>` storage、indices 和 memory ordering；
8. `Buffer<T>` slot ownership、publication 和 memory ordering；
9. build/install、compiler、architecture 和 binary audit；
10. Level 1、Level 2、PREEMPT_RT 与硬件验证计划。

后续决策不得推翻已经冻结的上游 Implementation 约束。Queue 和 Buffer 在算法证明及
最小验证 prototype 完成前不得直接进入 production implementation。

## 5. Production Source Layout

状态：已冻结

```text
realtime/
├── CMakeLists.txt
├── README.md
├── CHANGELOG.md
├── cmake/
├── docs/
│   ├── interface.md
│   └── implementation.md
├── include/realtime/
│   ├── buffer.hpp
│   ├── clock.hpp
│   ├── current_thread.hpp
│   ├── periodic_schedule.hpp
│   ├── process_memory.hpp
│   ├── queue.hpp
│   └── timing_statistics.hpp
├── src/
│   ├── clock.cpp
│   ├── current_thread.cpp
│   ├── monotonic_time.hpp
│   ├── monotonic_time_linux.cpp
│   ├── periodic_schedule.cpp
│   ├── process_memory.cpp
│   ├── schedule_arithmetic.hpp
│   ├── statistics_arithmetic.hpp
│   └── timing_statistics.cpp
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── consumer/
│   └── support/
├── benchmarks/
└── examples/
```

规则：

- `include/` 内容完全服从已冻结 Public Header Layout；
- `src/*.hpp` 是具有明确职责的 private implementation headers，不安装；
- private header 不使用 `detail`、`internal`、`common` 或 `types` 等模糊目录/名称；
- public tests 和 examples 只包含安装式 public headers；
- 只有纯 arithmetic 或 OS adapter contract 等无法由 Public Interface 精确定位失败的
  私有逻辑，才允许直接测试 private header；这类测试不能替代 Interface 测试；
- 只在产生实际实现时创建文件和目录，本布局不是空文件清单。

最终文件数量可以减少。例如 checked arithmetic 足够局部时，应保留在对应 `.cpp`
而不是创建浅 helper。不得为了与布局图一致而拆文件。

## 6. Internal Dependency Graph

状态：已冻结

```text
Clock
  └── monotonic_time_linux

PeriodicSchedule
  ├── Clock / monotonic_time_linux
  └── schedule_arithmetic

current-thread configuration
  └── Linux pthread scheduling / affinity

process memory locking
  └── Linux mlockall

TimingStatistics
  └── statistics_arithmetic

Queue<T, Capacity>
  └── C++ object lifetime + atomics

Buffer<T>
  └── C++ object lifetime + atomics
```

不存在 Realtime 内部的通用 `core` 或 `platform` 层。两个调用点恰好共享同一
monotonic clock domain 和 syscall contract，因此 monotonic-time seam 是真实共享；
其他 Linux 操作保持在各自 Implementation 中。

## 7. Private Monotonic-time Seam

状态：已冻结

### 7.1 Compile/link-time substitution

V1 建立一组仅由 `clock.cpp` 和 `periodic_schedule.cpp` 使用的 private free functions，
职责限于：

- 读取 `CLOCK_MONOTONIC`；
- 使用 `CLOCK_MONOTONIC` 和 `TIMER_ABSTIME` 等待给定 native target；
- 返回 Linux/POSIX error number，不建立公共 error category。

生产 target 固定链接 `monotonic_time_linux.cpp`。确定性测试 target 编译同一份
`clock.cpp`、`periodic_schedule.cpp`，但链接 tests/support 中的 controlled adapter。
替换发生在 link time，不是运行时：

```text
production:
  Clock / PeriodicSchedule -> Linux monotonic-time adapter

deterministic test:
  Clock / PeriodicSchedule -> controlled monotonic-time adapter
```

### 7.2 Why this seam exists

生产与测试至少存在两个真实 adapter：Linux clock 与 deterministic controlled clock。
该 seam 可以通过 Public Interface 确定性验证：

- future target 的 absolute wait；
- target 已到达时不 sleep；
- 多次 `EINTR` 后仍使用原 absolute target；
- unexpected wait error 原样返回；
- wait error 时 schedule 不推进；
- 精确的 missed-release 边界和 overflow 路径。

若只使用真实 wall execution，上述测试会依赖 scheduler timing，既不确定也无法稳定
覆盖错误路径。

### 7.3 Production-path constraints

- private functions 不使用 virtual dispatch、`std::function` 或 owning polymorphism；
- production object 不保存 adapter pointer、reference 或 function table；
- 不存在运行时 setter、global mutable backend 或 environment switch；
- test adapter 及其控制 Interface 不编译进、不链接进、不安装进 production target；
- adapter 返回 native data 后，chrono conversion、checked arithmetic、policy 和状态
  提交仍由 production code 负责，避免 fake 重复被测逻辑；
- production integration tests 另外链接正式 target，验证真实 `CLOCK_MONOTONIC` 和
  absolute sleep 路径。

### 7.4 Rejected alternatives

- **Public clock abstraction**：扩大已经冻结的 Interface，且 V1 没有第二个 production
  clock domain；
- **Runtime injected function table**：把测试间接调用和可变状态带入 RT hot path；
- **只测纯 arithmetic**：无法验证 `EINTR`、system error 和不推进状态的完整行为；
- **只运行真实 sleep tests**：时间敏感且无法可靠制造所有错误；
- **preprocessor 替换 syscall**：容易让测试编译出与 production 不同的控制流。

## 8. Cross-cutting Implementation Rules

状态：已冻结

### 8.1 Native types remain private

`timespec`、`clockid_t`、`cpu_set_t`、pthread scheduling structs 和 mlock flags 只出现于
`.cpp` 或 private source-tree headers。Public Interface 继续只使用已冻结的 chrono
types、整数、枚举和 `std::error_code`。

### 8.2 Error preservation

- POSIX functions that return an error number directly 不读取 `errno`；
- functions documented through `errno` capture it immediately after failure；
- generic validation errors 使用 `std::make_error_code(std::errc::...)`；
- Linux failures 使用 `std::error_code(error_number, std::system_category())`；
- error mapping 不分配字符串、不记录日志、不执行 fallback。

### 8.3 State commit

有状态操作先在 local temporaries 中完成 validation、conversion 和 checked arithmetic，
最后一次提交完整状态。错误返回不得依赖回滚部分写入。

### 8.4 Assertions

`static_assert` 用于 template constraints 和 atomic capability。运行时 caller input 使用
已冻结返回语义。Debug assertion 只检查不可由调用者触发的 Implementation invariant，
不得替代 error handling。

## 9. Implementation Decision Status

本文列出的 Realtime V1 Implementation decisions 已全部冻结。进入 production coding 后，
如果实现证据表明任一选择无法满足 Public Interface 或目标平台约束，必须显式重新评审
对应章节；不得通过未记录的 fallback、弱化 guarantee 或 test-only production path 解决。

## 10. Time Representation And Checked Arithmetic

状态：已冻结

### 10.1 Canonical representation

Realtime 内部计算沿用 Public Interface 已冻结的 representation：

```text
Clock::rep       = std::int64_t
Clock::period    = std::nano
Duration         = signed int64 nanoseconds
TimePoint        = signed int64 nanoseconds in CLOCK_MONOTONIC domain
```

所有 schedule 和 statistics 核心计算先转换为上述整数纳秒 domain。Implementation 不
创建第二套 tick、floating-point seconds 或 platform-dependent duration。`timespec` 只
在 private monotonic-time adapter 附近短暂存在，不保存在 public result 或 schedule
state 中。

`TimePoint` 的负值是可表达值。虽然生产 `CLOCK_MONOTONIC` observation 非负，调用者
仍可配置负的 `first_release`，表示一个已经过去的网格起点。`configure()` 不因此拒绝
输入；只有真正需要转换为 native absolute sleep target 时，才要求目标可以表示为合法
`timespec`。

### 10.2 Checked arithmetic primitives

V1 使用少量 private、无状态、`noexcept` 的 checked operations，覆盖实际调用点：

- checked `TimePoint + Duration -> TimePoint`；
- checked `TimePoint - TimePoint -> Duration`；
- checked nonnegative `Duration + Duration -> Duration`；
- checked nonnegative difference/absolute difference；
- checked counter addition；
- saturating total addition 只用于后续 statistics accumulator。

每个 checked operation 先比较 `std::numeric_limits<std::int64_t>` 的界限，再执行算术，
不得先产生 signed overflow。V1 不依赖 `__int128`、compiler overflow builtin、floating
point 或 exception。operation 通过 private `bool + out parameter` 表达成功，并保证
失败时不修改 out value。

只实现存在真实调用点的 operation，不建立 generic safe-integer class、operator
overload 集合或公共 arithmetic library。

### 10.3 Native `timespec` conversion

`timespec -> TimePoint` 必须：

1. 验证 `tv_sec >= 0`；
2. 验证 `0 <= tv_nsec < 1'000'000'000`；
3. 在乘以每秒纳秒数和加上 `tv_nsec` 前分别检查 `int64_t` 范围；
4. 只在全部步骤成功后产生 `TimePoint`。

生产 `clock_gettime(CLOCK_MONOTONIC, ...)` 返回不合法或无法映射的值，表示
`Clock::now()` 无法履行 Value Query Interface，进入已经冻结的 fatal invariant failure
路径。不得截断、饱和或返回伪造时间。

`TimePoint -> timespec` 只用于 future absolute sleep target，必须：

1. 要求 nanosecond count 非负；
2. 使用整数除法得到 seconds 和 `[0, 999'999'999]` nanoseconds；
3. 验证 seconds 可由目标平台 `timespec::tv_sec` 表示；
4. 只在全部检查成功后写入 native value。

转换失败时 `wait_next()` 返回 `std::errc::value_too_large`，并保持 schedule state
不变。由于 Implementation 在调用 sleep 前已经读取当前 monotonic time，合法的 future
target 在生产环境中必然非负；负的 past target 不进入 native conversion，也不调用
sleep。

### 10.4 Schedule arithmetic

对一次 configured `wait_next()`，Implementation 按以下顺序计算：

1. snapshot 当前 `scheduled_time`、`period` 和 policy 到 local values；
2. 读取 `now`；
3. 当 `scheduled_time > now` 时转换并 absolute-sleep；`EINTR` 重用同一个 native
   target，成功后重新读取 `wake_time`；否则以本次 `now` 作为 `wake_time`；
4. checked 计算 `lateness = wake_time - scheduled_time`；
5. 计算 `missed_releases = lateness / period`；
6. checked 计算 policy 对应的 next release；
7. 只有全部 observation 和 next-state 计算成功后，提交 next release 并返回成功结果。

生产 clock 保证 `wake_time >= scheduled_time`：future target 至少等待至 target，past 或
equal target 不等待。因此 lateness 为非负。若两个可表示 `TimePoint` 的差仍无法由
`Duration` 表示，返回 `std::errc::value_too_large`，不提交状态。

`missed_releases` 的除法安全条件为 `lateness >= 0`、`period > 0`。其商不大于
`INT64_MAX`，可以无损转换为 `std::uint64_t`，不需要 saturating count。

`CatchUp` 使用 checked：

```text
next_release = scheduled_time + period
```

`SkipMissed` 不计算 `(missed_releases + 1) * period`。它使用：

```text
remainder = lateness % period
advance_from_wake = period - remainder
next_release = wake_time + advance_from_wake
```

当 `remainder == 0` 时，`advance_from_wake == period`，因此 next release 严格晚于
`wake_time`；其他情况推进到原 schedule grid 的下一个点。该形式避免 count increment
和 duration multiplication，同时保持 anchored schedule。最终 addition 仍必须 checked。

### 10.5 Absolute sleep details

production adapter 调用：

```text
clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, nullptr)
```

`clock_nanosleep()` 直接返回 positive error number，Implementation 不从 `errno` 获取其
错误。返回 `EINTR` 时使用原封不动的 `target` 再次调用；`remain` 对 absolute sleep
没有用途。其他非零返回转换为 system-category `std::error_code`。

虽然 Linux 对已经到达的 absolute target 会立即返回，Implementation 仍遵守 Public
Interface：在已知 target 不晚于当前时间时不调用 sleep。这一分支由 controlled adapter
记录调用次数进行确定性验证。

### 10.6 Rejected alternatives

- **Unchecked chrono arithmetic**：signed representation overflow 会产生 undefined
  behavior，不能在事后检测；
- **`__int128` 中间值**：不是 C++17 standard library 能力，会扩大 compiler 约束；
- **Compiler overflow builtins**：可以作为未来经验证的私有优化，但不作为 V1 正确性
  前提；
- **Reject negative `first_release`**：没有 Interface 依据，也会拒绝可正确表示的 past
  schedule grid；
- **Store normalized `timespec` in `PeriodicSchedule`**：让 native representation 泄漏
  到状态逻辑，并增加双 representation 一致性负担；
- **用 missed count 乘 period 推进 `SkipMissed`**：产生不必要的 increment 和
  multiplication overflow surface；
- **Saturate schedule time**：会静默改变 phase 和 deadline，违反明确错误语义。

### 10.7 Required tests

至少覆盖：

- 每个 checked operation 的 zero、正负边界和失败时 out value 不变；
- `timespec` nanosecond 下界、上界、非法值及 seconds conversion 极限；
- negative/equal/future `first_release`；
- lateness 为 `0`、`period - 1ns`、`period` 和多个 period；
- `CatchUp` 与 `SkipMissed` 的 next release；
- `SkipMissed` 结果严格晚于 wake time 且仍位于原网格；
- observation subtraction 和 next release overflow 时 schedule 不推进；
- 多次 `EINTR` 始终使用同一 native target；
- past target 不调用 sleep；
- 非 `EINTR` sleep error 原样映射且不推进状态。

## 11. Clock And Absolute Sleep Implementation

状态：已冻结

### 11.1 Private adapter contract

private `src/monotonic_time.hpp` 声明两个非公开 free functions，概念 Interface 为：

```cpp
namespace realtime::monotonic_time {

[[nodiscard]] int read(timespec& value) noexcept;
[[nodiscard]] int sleep_until(const timespec& target) noexcept;

}  // namespace realtime::monotonic_time
```

该 header 位于 `src/`、不安装，native type 不进入 Public Interface。两个函数统一采用
`0` 表示成功、positive error number 表示失败；失败时不得返回 `-1`，调用者也不读取
`errno`。

production `monotonic_time_linux.cpp` 的职责严格限定为：

- `read()` 调用一次 `clock_gettime(CLOCK_MONOTONIC, ...)`；失败后立即捕获 `errno` 并
  返回 positive error number；
- `sleep_until()` 调用一次
  `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ..., nullptr)`，直接返回其结果；
- 不执行 conversion、retry、policy、logging 或 state mutation。

`EINTR` retry 属于 `PeriodicSchedule` Implementation，而不是 adapter。这样 controlled
adapter 的每次 sleep invocation 都可观察，能够证明 schedule 重用了同一 target。

第 5 节原先使用 `monotonic_time_linux.hpp` 的布局在本轮细化为：由
`monotonic_time.hpp` 声明 adapter contract，由 `monotonic_time_linux.cpp` 提供 production
adapter。该调整不增加文件数量，也避免让 controlled adapter 实现带有错误的 Linux
身份。

### 11.2 `Clock::now()`

`Clock::now()` 每次调用：

1. 在 stack 上建立未公开的 native time value；
2. 调用 private `monotonic_time::read()`；
3. 按第 10.3 节 checked-convert 为 `TimePoint`；
4. 成功时直接返回转换结果。

Implementation 不缓存时间，不保存 last value，不使用 global atomic，也不把相邻相同
读数人为增加一个 nanosecond。单调不减语义由固定 `CLOCK_MONOTONIC` domain 提供；
library 不建立第二个全局 ordering mechanism。

### 11.3 Fatal invariant failure

如果 `monotonic_time::read()` 失败，或返回的 native value 不合法/无法由 `TimePoint`
表示，`Clock::now()` 调用 private、`[[noreturn]] noexcept` 的 failure function。V1 冻结
该 function 最终调用：

```cpp
std::abort();
```

选择 `std::abort()` 的原因：

- Public Interface 已定义该情况为不可恢复的 implementation-controlled invariant
  failure；
- 它不会把伪造时间带入 schedule，也不会把错误隐藏为 fallback；
- 不执行 C++ automatic、thread 或 static object destructor，也不调用 `atexit` handlers；
- 相比 `std::terminate()`，不受可替换 terminate handler 影响；
- 相比 `_Exit()`，`SIGABRT` 保留标准的异常终止和 core-dump 诊断路径。

failure function 不格式化消息、不访问 logger、不分配内存，也不提供 runtime-configured
handler。失败后不再提供 realtime latency 或程序继续执行保证。

### 11.4 `PeriodicSchedule` sleep path

`wait_next()` 只在 snapshot target 严格晚于第一次 `Clock::now()` observation 时进入
sleep path：

1. checked-convert target 为一个 normalized native value；
2. 调用 `monotonic_time::sleep_until(target)`；
3. 返回 `EINTR` 时不读取 remaining duration、不修改 target、不推进 schedule，直接用
   同一 native target 再次调用；
4. 返回其他错误时构造失败 `PeriodicWaitResult`，不再读取 wake time，也不推进 state；
5. 返回成功后调用 `Clock::now()` 得到 `wake_time`，继续第 10.4 节的 observation 和
   next-state checked calculation。

对于第一次 observation 已经证明 target 小于或等于 now 的情况，`wake_time` 就是该
observation，不调用 sleep，也不进行第二次 clock read。

V1 不提供 cancellation。持续到来的 signal 可以导致多次 `EINTR`；这是 Public
Interface 已声明的唯一允许的内部 retry loop，循环始终等待同一个 absolute target。

### 11.5 Build constraints

- production source 通过 target-private `_GNU_SOURCE` feature-test definition 请求所需
  GNU affinity 和 POSIX clock declarations；该 macro 不传播给 consumer；
- 调用 libc wrapper，不直接调用 Linux raw syscall，从而允许受支持系统使用 vDSO 等
  libc/kernel 优化；
- production target 只链接 Linux adapter；controlled adapter 只链接 deterministic
  test subject；
- private adapter symbols 最终必须使用 hidden visibility，不成为 shared-library ABI；
- static 和 shared build 必须执行相同控制流，不使用 weak-symbol override。

具体 export macro 和 target visibility 设置在 build/ABI decision 中统一冻结，不能为
本 seam 单独建立另一套机制。

### 11.6 Required tests

Deterministic Public Interface tests 至少验证：

- `Clock::now()` 对合法 native time 的精确纳秒转换；
- 两次相同或递增 clock reading 不被 library 修改；
- future wait 只传递 normalized absolute target；
- past/equal target 的 sleep call count 为零；
- 任意数量 `EINTR` 后 target value 保持一致；
- unexpected sleep error 被保留，且不发生额外 clock read 或 state advance；
- successful sleep 后只读取一次 wake time；
- read failure、非法 nanoseconds 和 conversion overflow 通过 child-process death test
  观察 `SIGABRT`。

production integration tests 至少验证：

- 连续 `Clock::now()` observations 单调不减；
- real absolute wait 不早于 target 返回；
- signal interruption 不造成 relative-time restart drift；
- 测试不对 wake latency 设置与硬件无关的严格上界。

Death test 允许在普通测试进程中使用 `fork()` 隔离异常终止；它不是 production
Interface，也不在 realtime execution 环境运行。

### 11.7 Rejected alternatives

- **`std::terminate()`**：terminate handler 可由进程替换，会把未知用户行为引入该
  failure path；
- **返回 zero/last time**：伪造成功并可能破坏 schedule ordering；
- **在 library 内记录错误后继续**：引入 I/O、allocation 或 global diagnostic state；
- **在 adapter 内处理 `EINTR`**：测试无法从 schedule seam 证明 target reuse；
- **每次 `EINTR` 后重新计算 relative duration**：扩大 drift 和 timeout 重启风险；
- **强制 clock value 严格递增**：制造不属于 `CLOCK_MONOTONIC` 的语义，并需要共享
  atomic state；
- **直接 raw syscall**：绕开 libc 没有 Interface 收益，并可能失去平台优化。

## 12. `PeriodicSchedule` State And Commit

状态：已冻结

### 12.1 Object representation

V1 使用直接、固定大小的 private members，不使用 PImpl、heap allocation、optional
state 或 polymorphism：

```cpp
TimePoint next_release_{};
Duration period_{};
MissedPeriodPolicy policy_{MissedPeriodPolicy::CatchUp};
bool configured_{false};
```

`configured_` 是显式 lifecycle state，不借用 `period_ == 0` 作为隐式 sentinel。虽然
有效配置已经要求 positive period，独立 bool 仍让 unconfigured 状态、debug invariant
和未来维护保持直接，不把两个领域概念耦合为一个表示技巧。

unconfigured 对象中的其他字段只用于确定性初始化，不具有 schedule 语义。默认 policy
选择 `CatchUp` 只是避免 indeterminate storage；它不是 Public Interface 默认策略，调用者
仍必须在 `configure()` 中显式提供 policy。

### 12.2 `configure()`

`configure(first_release, period, policy)` 按以下顺序执行：

1. 检查 `period > Duration::zero()`；
2. 使用 exhaustive `switch` 接受且只接受 `CatchUp`、`SkipMissed`；
3. validation 全部成功后写入 `next_release_`、`period_`、`policy_`；
4. 最后写入 `configured_ = true`；
5. 返回空 `std::error_code`。

非法 period 或由 integer cast 构造的未知 policy 返回
`std::errc::invalid_argument`。失败路径不写任何 member，因此：

- 首次配置失败后保持 unconfigured；
- 重新配置失败后完整保留旧 schedule；
- 不需要 rollback；
- 不读取 clock，不进行 native conversion 或未来 release 预计算。

配置成功后的 member assignment 都是不会失败的 fixed-value assignment。Interface 已禁止
并发 `configure()`、`is_configured()` 或 `wait_next()`，因此这里不使用 atomic 或 lock
制造跨线程事务假象。

### 12.3 `is_configured()`

`is_configured()` 直接返回 `configured_`：

- 不推导或验证其他 members；
- 不执行 syscall、allocation 或 synchronization；
- 不修复 Implementation invariant；
- 只有在调用者满足无并发访问契约时才是 RT-callable。

### 12.4 `wait_next()` state machine

`wait_next()` 只有两个 lifecycle branches：

```text
unconfigured
  -> return operation_not_permitted
  -> remain unconfigured

configured
  -> snapshot current state
  -> observe/wait/calculate using locals
  -> failure: preserve current state
  -> success: commit next_release_ only
```

configured path 开始时复制以下 local snapshot：

```text
scheduled_time = next_release_
period         = period_
policy         = policy_
```

后续 clock observation、native conversion、sleep、lateness、missed count 和 next release
全部基于 snapshot/local values。`period_`、`policy_` 和 `configured_` 在 `wait_next()` 中
永不修改。

### 12.5 Failure construction

所有可恢复失败使用同一 private helper 构造 value-initialized `PeriodicWaitResult`，然后
只设置 `error`：

```text
unconfigured                  -> operation_not_permitted
native target 不可表示         -> value_too_large
lateness/next 不可表示          -> value_too_large
non-EINTR sleep failure        -> corresponding system error
```

这保证失败 result 的每个字段都有确定的 C++ value，同时继续遵守“timing fields 没有领域
意义”的 Public Interface。Implementation 不为不同错误建立部分有效字段组合。

任何失败都不写 `next_release_`。如果错误发生在 successful physical wait 之后，例如
next release overflow，schedule 仍保留原 target；后续调用会再次观察同一 target，直到
调用者重新配置或退出。这是“不推进”契约的直接结果，Implementation 不私自 rebase。

### 12.6 Successful commit

只有以下条件全部满足后才构造成功 result 并提交：

- clock/native values 合法；
- sleep 成功或无需 sleep；
- `wake_time >= scheduled_time`；
- lateness 可表示；
- missed count 可表示；
- policy 对应的 next release 可表示且严格满足其推进规则。

提交顺序为：

1. 由 local snapshot 构造完整的成功 `PeriodicWaitResult` local value；
2. `next_release_ = calculated_next_release`；
3. 返回已经完成构造的 result。

result 的 `scheduled_time` 和 `period` 来自本次 local snapshot，不在提交后重新读取 object
members。因此未来 `configure()` 或 `wait_next()` 不会改变已经返回 observation 的含义。

### 12.7 Clock invariant violation

在 configured path 中，如果：

- past/equal branch 的 local `wake_time` 小于 `scheduled_time`，与先前比较矛盾；或
- absolute sleep 返回成功后读取的 `wake_time` 小于 target；

则 monotonic clock/adapter 已违反生产 contract。该情况不是 caller configuration error，
也不是 arithmetic overflow，不能映射为 `value_too_large`。Implementation 使用第 11.3
节同一个 private fatal invariant function 调用 `std::abort()`。

不额外保存 last wake time，也不比较不同 `PeriodicSchedule` 或不同调用之间的 clock
observations；只检查完成当前 observation 所必需的局部不变量。

### 12.8 Policy calculation

`CatchUp`：

```text
calculated_next_release = checked_add(scheduled_time, period)
```

即使当前已经 missed 多个 releases，也只推进一个 period。

`SkipMissed` 使用第 10.4 节的 remainder form，计算严格晚于 `wake_time` 的第一个原网格
release。Implementation 在 configured state 中仍对 policy 使用 exhaustive `switch`；
default/unreachable value 表示内部 state corruption，进入 fatal invariant failure，不
静默采用任一 policy。

### 12.9 No additional lifecycle operations

V1 不实现 private/public `reset()`、`start()`、`stop()` 或 generation counter：

- default construction 已表达 unconfigured；
- `configure()` 同时表达首次配置和显式重新定相；
- caller 拥有运行与停止；
- 没有 Public Interface 可以把 configured 对象恢复为 unconfigured；
- sequence/freshness 不属于 schedule observation。

### 12.10 Required tests

通过 Public Interface 至少覆盖：

- default object 为 unconfigured；
- unconfigured wait 返回 `operation_not_permitted` 且重复调用结果稳定；
- 每一种非法 configure 输入均不改变首次或既有配置；
- 成功 reconfigure 从新的 `first_release`、period 和 policy 开始；
- 每次成功 wait 只推进一次 state commit；
- `CatchUp` 连续立即返回并逐 period 追赶；
- `SkipMissed` 一次推进到严格晚于 wake time 的原网格点；
- sleep error 或 controlled wake 导致的 lateness overflow 后，修复 adapter 条件再调用时
  仍针对原 scheduled target；
- native conversion overflow 或 next overflow 后重复调用仍不推进 target，调用者可通过
  成功 reconfigure 恢复；
- 返回 result 保留当次 period snapshot；
- controlled adapter 违反 wake ordering 时 child process 以 `SIGABRT` 终止。

unknown configured policy 属于无法通过合法 Public Interface 构造的 Implementation
corruption path，以 code review 和 default fatal branch 覆盖；不得为测试它而增加 Public
Interface hook 或通过未定义行为篡改 private storage。

### 12.11 Rejected alternatives

- **以 `period_ == 0` 表示 unconfigured**：节省一个 bool，但混淆 lifecycle 与配置值；
- **`std::optional<State>`**：增加 engagement/move 语义，却不增加可观察能力；
- **PImpl**：需要 allocation 或额外 ownership machinery，V1 没有 ABI 稳定承诺支持其
  成本；
- **每一步直接修改 members 并在失败时 rollback**：扩大错误路径和部分状态风险；
- **失败后自动 unconfigure**：破坏 strong failure guarantee；
- **successful sleep 后 next overflow 仍返回成功**：对象无法提交与 observation 对应的
  下一状态；
- **遇到 overflow 自动 rebase**：改变 anchored schedule 和调用者故障策略；
- **保存 last wake time 做全局 monotonicity enforcement**：增加状态且超出当前
  observation 所需不变量。

## 13. Current-thread And Memory Setup

状态：已冻结

### 13.1 Implementation placement

V1 使用两个独立 translation units：

```text
current_thread.cpp
  ├── set_current_thread_scheduling()
  └── set_current_thread_affinity()

process_memory.cpp
  └── lock_process_memory()
```

三个操作不共享状态、helper class 或 configuration object。它们直接实现已冻结的
setup-only Interface，不建立 `linux_setup`、`system_config` 或 platform manager。

### 13.2 Scheduling policy mapping

`set_current_thread_scheduling(policy, priority)` 首先以 exhaustive `switch` 映射：

```text
SchedulingPolicy::Normal       -> SCHED_OTHER
SchedulingPolicy::Fifo         -> SCHED_FIFO
SchedulingPolicy::RoundRobin   -> SCHED_RR
```

由 integer cast 产生的未知 enum value 返回 generic-category
`std::errc::invalid_argument`，不执行任何 scheduling syscall。

`Normal` 只接受 `priority == 0`。其他值直接返回 generic
`invalid_argument`。V1 不查询或接受 Linux 上其他 non-realtime policy，也不把 nice value
混入 priority。

`Fifo` 和 `RoundRobin` 分别调用：

```text
sched_get_priority_min(native_policy)
sched_get_priority_max(native_policy)
```

任一查询返回 `-1` 时立即捕获 `errno`，返回对应 system-category error，不继续配置。
priority 不在闭区间 `[minimum, maximum]` 时返回 generic `invalid_argument`，不调用
`pthread_setschedparam()`。

### 13.3 Applying current-thread scheduling

validation 成功后建立 value-initialized `sched_param`，只设置
`sched_priority = priority`，然后执行一次：

```text
pthread_setschedparam(pthread_self(), native_policy, &parameters)
```

`pthread_self()` 不保存到 object 或 global state。`pthread_setschedparam()` 返回 `0` 表示
成功，返回 nonzero error number 表示失败；Implementation 直接用该返回值建立
system-category `std::error_code`，不得读取 `errno`。

该 pthread operation 同时设置 policy 和对应 parameters。失败时 Linux/POSIX 保证目标
thread 的 policy 和 parameters 不改变，因此 library 不查询旧状态，也不实现 rollback。
成功后不执行 readback；调用返回值就是本次设置结果。

### 13.4 Single-CPU affinity representation

`set_current_thread_affinity(cpu_index)` 在 stack 上建立一个 value-initialized/fixed-size
`cpu_set_t`。在调用任何 `CPU_SET` macro 前必须验证：

```text
cpu_index < CPU_SETSIZE
```

无法由该 fixed native set 表示的 index 返回 generic-category
`std::errc::invalid_argument`。V1 不使用 `CPU_ALLOC` 或其他动态 CPU-set storage，因为
Public Interface 只承诺一个 CPU，且 setup function 已冻结为无 library-owned dynamic
allocation。

验证成功后：

```text
CPU_ZERO(&set)
CPU_SET(cpu_index, &set)
pthread_setaffinity_np(pthread_self(), sizeof(set), &set)
```

`pthread_setaffinity_np()` 的 nonzero return 是 error number，不读取 `errno`，直接映射为
system-category error。Implementation 不先调用 `sysconf()`、读取 `/sys` 或查询当前
cpuset：CPU 是否存在、在线并被 cpuset/cgroup 允许，最终由一次 kernel operation 决定。

V1 不尝试把 kernel 的 `EINVAL` 进一步猜测为“CPU 不存在”“CPU 离线”“超出 kernel
mask”或“被 cgroup 禁止”。这些条件在该 syscall seam 上没有稳定、无竞态的细分结果；
保留 system `EINVAL` 比推断错误诊断更准确。

成功后不执行 affinity readback。对于只包含一个 CPU 的 requested set，成功表示 kernel
接受该 current-thread affinity；如果当前不在该 CPU，Linux 可以在调用返回前迁移线程。

### 13.5 Process memory locking

`lock_process_memory()` 只执行：

```text
mlockall(MCL_CURRENT | MCL_FUTURE)
```

返回 `0` 时返回空 error。返回 `-1` 时立即捕获 `errno`，返回对应 system-category error。
Linux 保证失败时不改变 address-space locks，因此不需要 rollback。

Implementation 不保存“已锁定”flag、不读取 `RLIMIT_MEMLOCK`、不检查 capability、不重试、
不 fallback 到 `MCL_CURRENT`，也不提供 `munlockall()`。重复调用直接服从 kernel 语义。

成功只表示本次 `mlockall` 被接受。stack/heap prefault、lazy binding、后续 allocation、
page-fault measurement 和 `fork()` discipline 继续由调用者及验证程序承担，不能通过此
Implementation 推导出“实时路径不会 page fault”。

### 13.6 Error category rule

三个 setup operations 统一区分：

| 来源 | 表达 |
|---|---|
| library 在 syscall 前发现的非法 enum、priority 或不可表示 CPU index | generic-category `std::errc::invalid_argument` |
| `sched_get_priority_min/max()` 或 `mlockall()` 的 `-1`/`errno` | captured errno + `std::system_category()` |
| pthread function 直接返回的 nonzero error number | returned number + `std::system_category()` |

不得把 system `EINVAL` 重写成 generic `invalid_argument`。category 差异保留错误来源：前者
是 library 已验证的 caller input，后者是 OS 对当前环境或 native operation 的拒绝。

### 13.7 No private syscall adapter

V1 不为这些 setup operations 增加类似 monotonic-time 的 link seam：

- 每个 mutation 最终只有一次 production OS call；
- 没有 retry、转换状态机或多步提交需要 controlled execution；
- public input validation 可以确定性测试；
- 成功后的系统状态可以由测试使用独立 OS query 观察；
- 增加 adapter 会让测试主要验证 fake，而不是 Linux operation。

无法在普通 CI 权限下稳定制造的 permission/resource 结果，通过隔离进程和明确配置的
validation environment 验证，不为追求分支覆盖向 production code 加入 indirect call。

### 13.8 Feature-test and link requirements

`pthread_setaffinity_np()` 是 GNU extension。Realtime production target 使用 private
`_GNU_SOURCE` feature-test definition，使声明只影响 library translation units，不传播
给 consumer。该 definition 同时提供 glibc 中所需 POSIX clock declarations，因此 V1
不同时维护 `_GNU_SOURCE` 与 `_POSIX_C_SOURCE` 两组 target definitions。

第 11.5 节所述 POSIX feature-test definition 据此收敛为 `_GNU_SOURCE`。这是 Linux-only
component 的 Implementation build requirement，不改变 Public Interface。

CMake 使用 `find_package(Threads REQUIRED)` 和 `Threads::Threads` 表达 pthread build/link
requirement。static consumer 所需的最终 link requirement 必须由 exported target 正确
传递；具体 CMake visibility 与 package dependency 在 build/install decision 中验证。

### 13.9 Test strategy

所有改变线程或进程状态的测试都在隔离 child process 中运行，避免污染 test runner。

Scheduling tests：

- unknown policy、`Normal` nonzero priority、`Fifo/RoundRobin` 范围外 priority 返回
  generic `invalid_argument`，并用 `pthread_getschedparam()` 验证状态未变；
- `Normal, 0` 在 child 中成功设置并 readback；
- realtime policy 的成功路径只在具备明确 `CAP_SYS_NICE`/resource configuration 的
  validation environment 中作为 required test；普通 CI 不把权限拒绝误判为实现失败；
- system error 保持 system category，不允许 fallback 到 `Normal`。

Affinity tests：

- 由 `pthread_getaffinity_np()` 获取 child 当前允许集合，选择其中一个可表示 CPU，调用
  library 后验证 mask 恰好只包含该 CPU；
- `cpu_index >= CPU_SETSIZE` 返回 generic `invalid_argument` 且 affinity 不变；
- cpuset/cgroup 限制测试只在能控制该环境的 integration/validation job 中执行。

Memory-lock tests：

- 在 child 中调用，避免成功的 process-wide state 泄漏到其他 tests；
- 普通 CI smoke test 接受成功或保留的 permission/resource system error，但必须检查没有
  exception、fallback 或进程状态破坏；
- 配置了 `RLIMIT_MEMLOCK`/`CAP_IPC_LOCK` 的 validation job 必须验证 success、failure 和
  `/proc/<pid>/status` 的 `VmLck` observation；
- 不在调用成功的 child 中继续运行依赖 fork 的测试。

### 13.10 Rejected alternatives

- **直接使用 `sched_setscheduler(0, ...)`**：`0` 表示 calling process/thread 的 Linux
  约定不如 `pthread_self()` 清楚，且 Public Interface 明确采用 current-thread 语义；
- **把 priority 映射为 Low/Medium/High**：丢失原生数值和平台范围；
- **动态 `CPU_ALLOC`**：增加 allocation 与 free 路径，只为超出 V1 单 CPU fixed-set
  范围的场景；
- **预查询 CPU online/cpuset 状态**：增加竞态和多套错误来源，最终仍需 kernel 验证；
- **affinity 成功后强制 readback**：增加一次 syscall，但不改善单 CPU set 的成功语义；
- **memory-lock RAII/refcount**：错误表达 process-wide ownership；
- **自动提高 rlimit/capability**：扩大权限与进程策略范围；
- **为每个 syscall 建 test adapter**：增加浅 seam 和 production indirection，没有足够
  行为复杂度回报。

## 14. `TimingStatistics` Accumulator

状态：已冻结

### 14.1 Private representation

V1 在 `TimingStatistics` object 内直接保存 fixed-size state，不使用 heap、PImpl、atomic
或 floating-point：

```cpp
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
```

这里是概念 member layout；最终可以使用 private nested aggregate 组织相同字段，但不得
改变 ownership、storage 或 arithmetic 语义。

`DurationAccumulator` 不重复保存 sample count：release-latency 和 execution-time 使用
`cycle_count_`，jitter 使用 `jitter_sample_count_`。count 为 zero 时对应 minimum、maximum
和 total 均保持 zero；不额外保存 `has_samples`。

`previous_release_latency_` 只在 `cycle_count_ > 0` 时有意义。它用于计算下一条 jitter，
不进入 snapshot。

### 14.2 Why totals use `std::uint64_t`

所有被接受的 duration sample 都是 `[0, INT64_MAX]` nanoseconds。total 使用
`std::uint64_t`，使多个 nonnegative samples 的累计范围大于单个 `Duration::rep`，同时
仍完全属于 C++17 standard integer arithmetic。

V1 不使用 signed total、floating point、`__int128` 或 incremental mean：

- signed total 更早饱和，并增加 signed-overflow 风险；
- floating point 破坏精确纳秒与确定性整数语义；
- `__int128` 扩大 compiler 约束；
- incremental mean 会在每次整数除法时累计 rounding bias，并使 min/max/count
  saturation 后的语义更复杂。

### 14.3 Observation validation and derivation

`try_observe(wait, completion_time)` 在修改任何 member 前，通过 local values 完成：

1. 要求 `wait.error` 为空；
2. 要求 `wait.period > Duration::zero()`；
3. 要求 `wait.wake_time >= wait.scheduled_time`；
4. checked 计算 release latency，并要求等于 `wait.lateness`；
5. 要求 `completion_time >= wait.wake_time`；
6. checked 计算 execution time；
7. checked 计算 `next_deadline = wait.scheduled_time + wait.period`；
8. completion 晚于 next deadline 时 checked 计算 overrun，否则 overrun 为 zero；
9. 如果已有前一 sample，以两个 nonnegative release latencies 的 `max - min` 计算 jitter。

任何 comparison、subtraction 或 addition 不满足 Interface invariant 或无法表示时返回
`false`，不修改任何 member，也不设置 `saturated_`。

V1 直接使用已验证的 `wait.missed_releases` 作为本次 missed-count increment。它是
`std::uint64_t` observation，不重复从 lateness 计算后再比较；`TimingStatistics` 验证
自包含 timing invariants，不重新实现完整 `PeriodicSchedule` policy。

### 14.4 Commit phase

所有 metrics 成功派生后，后续 accumulator update 不再具有拒绝路径：

1. release-latency 与 execution-time min/max/total；
2. 如果存在前一 sample，jitter min/max/total 和 `jitter_sample_count_`；
3. positive overrun 的 count 与 maximum；
4. missed-release total；
5. `cycle_count_`；
6. `previous_release_latency_`；
7. saturation latch。

由于 commit phase 只包含不会失败的 fixed-value comparisons、assignments 和 saturating
adds，不需要复制整个 accumulator 再交换，也不需要 rollback。`try_observe()` 在开始
commit 后必然返回 `true`；触发 saturation 仍是成功接受 observation。

### 14.5 Min/max initialization

对于 release latency 和 execution time：

- `cycle_count_ == 0` 时，minimum 和 maximum 都设置为第一条 sample；
- 后续分别执行普通 min/max comparison。

对于 jitter：

- 第一条成功 observation 只保存 previous release latency，不产生 jitter；
- `jitter_sample_count_ == 0` 时，第一条 jitter 同时初始化 minimum 和 maximum；
- 后续更新 min/max。

overrun 只在 `overrun > 0` 时增加 count 并更新 maximum。没有 positive overrun 时
`maximum_overrun_` 保持 zero。

即使任一 total 或 count 已饱和，minimum 和 maximum 仍继续根据后续成功 observations
更新，previous release latency 也继续更新，从而使后续 jitter sample 的局部定义保持
正确。

### 14.6 Saturating addition

private `statistics_arithmetic.hpp` 只提供实际需要的 pure operation，例如：

```text
saturating_add(uint64 current, uint64 increment)
  -> { value, saturated }
```

算法在加法前检查：

```text
increment > UINT64_MAX - current
```

成立时返回 `{UINT64_MAX, true}`，否则返回精确 sum 和 `false`。不得依赖 unsigned wrap
后检测。每个 count increment 使用相同 operation，increment 为 `1` 或
`wait.missed_releases`；duration sample 在已经证明 nonnegative 后无损转换为
`std::uint64_t` 再累计。

本次任一 addition 返回 saturated，或 `saturated_` 原本已为 true，最终 latch 都保持
true。不同 metric 不增加 public per-field saturation flags；调用者看到全局
`TimingSnapshot::saturated` 后，必须把所有 count/mean 的精确性视为不再统一保证。

### 14.7 Mean calculation

`snapshot()` 对非空 metric 使用：

```text
mean_nanoseconds = total_nanoseconds / sample_count
```

整数除法余数向下舍弃。空 metric 的 mean、minimum 和 maximum 均为 zero。

在未 saturation 的合法 state 中，mean 必然位于 `[minimum, maximum]` 且不大于
`INT64_MAX`，可以 checked-convert 为 `Duration::rep`。如果 private state 违反该不变量，
进入 fatal invariant failure，而不是返回截断 mean。发生 saturation 后仍使用饱和 total
和 count 生成确定性 snapshot value，但该 mean 不再承诺精确；转换仍必须 checked。

`snapshot()` 只读取当前 members 并构造一个独立 `TimingSnapshot`，不改变 accumulator，
不缓存 mean，也不执行 synchronization。

### 14.8 `reset()`

`reset()` 把所有 fixed accumulator fields 恢复为与默认构造相同的 zero state：

- counts 和 totals 为 zero；
- duration fields 为 zero；
- previous release latency 不再有意义并置 zero；
- `saturated_` 清为 false。

Implementation 可以通过 private aggregate value-initialization 一次赋值完成，但不得
deallocate/reconstruct storage、调用 global collector 或保留跨 reset 的 previous sample。
reset 后第一条成功 observation 不产生 jitter。

### 14.9 Realtime-safety and concurrency

- `try_observe()`、`snapshot()`、`reset()` 都只执行固定数量 arithmetic/assignment；
- 不使用 heap、lock、atomic、syscall、logging 或 callback；
- 没有与 sample count 成比例的 loop；
- accumulator 仍由一个线程独占访问，不通过 atomics 提供 snapshot 并发安全；
- 跨线程发布必须复制 `TimingSnapshot` 到调用者拥有的 `Buffer<TimingSnapshot>`。

public header 对 `DurationSummary` 和 `TimingSnapshot` 使用与 `Buffer<T>` payload contract
一致的 compile-time assertions，确保目标 standard library/compiler 上它们可以作为
Buffer payload。失败表示平台不满足已冻结 Interface，不 fallback 到其他复制策略。

### 14.10 Test strategy

Public Interface tests 至少覆盖：

- 默认与 reset 后的全 zero snapshot；
- 单 sample 的 min/max/mean、无 jitter、无 positive overrun；
- 多 sample 的 minimum、maximum 和向下取整 mean；
- jitter 等于相邻 release latency 的绝对差；
- schedule period 改变时 overrun 使用每条 observation 自带 period；
- positive/zero overrun 的 count 和 maximum；
- missed release 累计；
- 每一种 invalid observation 返回 `false` 且 snapshot 完全不变；
- reset 清除 saturation latch 和 previous sample，下一条 observation 不产生 jitter；
- `TimingSnapshot` 可通过 `Buffer<TimingSnapshot>` 往返复制。

无法通过现实循环把 `uint64_t` state 推到极限，因此 private pure arithmetic tests 直接
覆盖 zero、exact maximum、maximum-minus-one 和 overflow increments。Public tests 验证
normal update wiring；code review 验证每个 total/count 都调用同一 saturating operation。
不得通过缩小 production counter width、test-only macro 或篡改 private object storage
制造 saturation。

### 14.11 Rejected alternatives

- **每个 metric 保存独立 count**：为 release/execution 重复 `cycle_count_`，增加一致性
  invariant；
- **用 zero duration 作为“没有 sample”sentinel**：zero 是合法 observation；
- **复制整个 state 后 update/swap**：validation 完成后没有失败路径，只增加 stack 和
  copy cost；
- **incremental mean**：整数 rounding bias 会随 samples 累积；
- **floating-point total/mean**：失去精确纳秒和固定整数行为；
- **per-field public saturation flags**：扩大 snapshot，而 V1 已冻结单一 latch；
- **saturation 时拒绝 observation**：把 accumulator capacity 变成 Try Action failure，
  与已冻结语义冲突；
- **让 `snapshot()` 使用 lock/atomic**：伪装未承诺的跨线程共享模型；
- **test-only 小 counter type**：测试的不是 production representation。

## 15. `Queue<T, Capacity>` SPSC Ring

状态：已冻结

### 15.1 Algorithm choice

V1 使用 single-producer/single-consumer one-empty-slot ring：

```text
physical slots: Capacity + 1
usable slots:   Capacity
write index:    next slot producer may construct
read index:     next slot consumer may copy and retire

empty: write == read
full:  next(write) == read
```

`Capacity` 仍是 Public Interface 承诺的真实可用元素数量。额外 physical slot 只是区分
full/empty 的 Implementation storage，不减少 usable capacity，也不进入 Interface。

V1 不要求 Capacity 是 power of two。ring index 显式在 `[0, Capacity]` 中循环，不使用
bit mask 或 monotonically wrapping counter。

### 15.2 Inline storage without `Capacity + 1` overflow

为避免在 template array extent 中计算可能溢出的 `Capacity + 1`，object representation
概念上使用：

```cpp
using Slot = std::aligned_storage_t<sizeof(T), alignof(T)>;

std::array<Slot, Capacity> slots_;
Slot extra_slot_;

std::atomic<std::size_t> write_index_{0};
std::atomic<std::size_t> read_index_{0};
```

index `0..Capacity-1` 映射到 `slots_`，index `Capacity` 映射到 `extra_slot_`。辅助函数：

```text
next(index) = index == Capacity ? 0 : index + 1
```

当 `index < Capacity` 时，`index + 1 <= Capacity`，因此不会发生 unsigned overflow；
`index == Capacity` 时直接返回 zero。该表示：

- 不增加 power-of-two 或 `Capacity <= SIZE_MAX/2` 约束；
- 不把 `Capacity - 1` 暴露为实际容量；
- 不 default-construct `T`；
- 不 value-initialize 或清零 raw slot bytes，Queue construction 不随 Capacity 扫描 storage；
- 全部 storage 位于 Queue object 内；
- 对 `Capacity == 1` 仍具有两个 physical slots 和一个真实 usable element。

C++ implementation 对不可实现的巨大 object size 可以正常给出编译诊断；Queue 不为了
理论最大 array 建立另一种 runtime allocation strategy。

### 15.3 Object lifetime

Queue 构造只初始化 raw slots 和两个 atomic indices，不开始任何 `T` lifetime。

producer 对一个已证明 free 的 slot 使用 global placement new copy-construct：

```text
::new (slot_address) T(value)
```

placement new 返回的 pointer 指向新 object。consumer 从 raw slot address 恢复 pointer 时
使用 `std::launder`，只在 acquire 已证明该 slot 包含 published live object 后解引用。

consumer 按以下顺序退休元素：

1. copy-assign live slot object 到 caller-provided `value`；
2. 使用 `std::destroy_at()` 结束 slot 中 `T` 的 lifetime；
3. release-publish 新 `read_index_`，允许 producer 重用该 slot。

V1 已要求 `T` trivially destructible，因而 destroy 不执行用户代码，但显式结束 lifetime
使 slot ownership 和下一次 placement construction 保持清楚。Queue 析构时可能仍有
unconsumed live elements；对 trivially destructible `T`，storage release 可以结束这些
lifetimes，不扫描 ring，也不执行与 occupancy/Capacity 成比例的 loop。

`T` 不需要 default constructor。所有 placement construction 和 caller-output assignment
分别由已经冻结的 nothrow copy-construction/copy-assignment constraints 保证。

### 15.4 Atomic capability

indices 使用 `std::atomic<std::size_t>`，public header 执行：

```cpp
static_assert(std::atomic<std::size_t>::is_always_lock_free);
```

如果目标 standard library/architecture 不能在 compile time 保证该 atomic always
lock-free，Queue template 拒绝实例化；不得链接 `libatomic` fallback、改用 mutex 或降低
Realtime claim。

两个 indices 从 zero 开始，并始终只包含 `[0, Capacity]`。producer 是
`write_index_` 的唯一 writer，consumer 是 `read_index_` 的唯一 writer；双方只读取对方
发布的 index。

### 15.5 `try_push()`

概念步骤和 memory order：

```text
write = write_index_.load(relaxed)
next_write = next(write)
read = read_index_.load(acquire)

if next_write == read:
    return false

copy-construct T in slot(write)
write_index_.store(next_write, release)
return true
```

producer 对自身唯一写入的 index 使用 relaxed load。对 `read_index_` 的 acquire load：

- full 时只产生正常 Boolean Status；
- 有空间时证明目标 slot 已由 consumer 退休；
- 如果 slot 在先前 lap 中包含元素，匹配 consumer retirement release，确保 consumer
  copy/destruction happens-before producer reuse。

成功 push 的 linearization/publication point 是 final release store。构造完成前 consumer
不能观察到新 write index。full 返回前不开始 `T` lifetime，也不修改 Queue 或 input。

### 15.6 `try_pop()`

概念步骤和 memory order：

```text
read = read_index_.load(relaxed)
write = write_index_.load(acquire)

if read == write:
    return false

copy-assign slot(read) to output
destroy T in slot(read)
read_index_.store(next(read), release)
return true
```

consumer 对自身唯一写入的 index 使用 relaxed load。对 `write_index_` 的 acquire load：

- empty 时只产生正常 Boolean Status；
- 有元素时匹配 producer publication release；
- producer 在 release 前完成的 payload construction happens-before consumer copy。

成功 pop 的 retirement point 是 final release store；producer 在 acquire-observe retirement
之前不能重用该 slot。empty 返回不修改 output，也不访问任何 slot 中的 `T`。

### 15.7 Full/empty and wrap-around proof

ring 始终保留一个 physical empty slot。合法状态下，从 `read` 沿 `next()` 到 `write` 的
live slots 数量位于 `[0, Capacity]`：

- producer 只在 `next(write) != read` 时增加一个 live slot；
- consumer 只在 `read != write` 时减少一个 live slot；
- 两个角色各自只能推进自己拥有的 index；
- 因此 producer 不会越过 consumer，consumer 也不会越过 producer。

index wrap 每经过 `Capacity + 1` 次推进就从 `Capacity` 回到 zero，与 `size_t` 数值
overflow 无关。即使 acquire load 观察到较旧的对端 index，也最多导致一次保守的
full/empty false result；在 SPSC 不可越过 invariant 和 atomic coherence 下，不会把正在
由另一端拥有的 slot 误判为可访问。

该表示避免 monotonically wrapping counter 对非 power-of-two Capacity 的 slot mapping
问题：如果 counter 在 `2^N` 处自然 wrap，`counter % Capacity` 只有在 Capacity 整除
`2^N` 时才保持原 conceptual sequence。V1 不接受这种隐含限制。

### 15.8 Bidirectional happens-before

对每个成功传输：

```text
producer payload construction
  sequenced-before
write_index release store
  synchronizes-with
consumer write_index acquire load
  sequenced-before
consumer payload copy

consumer payload copy + lifetime end
  sequenced-before
read_index release store
  synchronizes-with
producer read_index acquire load
  sequenced-before
producer reuses the same slot
```

第一条链发布 payload，第二条链保护 storage reuse。只有同一 index atomic 上读取对应
release 或其后续 coherent value 时建立相应 synchronizes-with；算法不依赖 relaxed
operation 建立跨线程 ordering。

failed push/pop 不成功传输新 payload。其 acquire load 可能观察历史 index publication，
但调用者不得把 `false` 当作任意外部数据的发布协议。

### 15.9 Cache layout decision

V1 不预先添加 `alignas(64)`、手写 padding 或
`std::hardware_destructive_interference_size`：

- cache-line size 是目标相关属性；
- padding 会显著改变每个 template specialization 的 object size/layout；
- Queue correctness 不依赖 cache placement；
- 尚无 benchmark 证明固定 padding 在目标 x86_64/aarch64 workload 上有净收益。

benchmark 必须分别测量 producer/consumer throughput、tail latency 和 payload size。只有
证据显示 false sharing 是主要瓶颈后，才重新评审 index/cache layout；优化不得改变
Interface 或 memory-order proof。

### 15.10 Complexity and RT path

每次 operation 固定执行：

- 两次 atomic load；
- 成功时一次 atomic store；
- 最多一次 `T` copy construction 或 copy assignment；
- 常数次 comparison/branch/index calculation；
- pop 成功时一个 trivial destruction。

没有 CAS loop、retry、allocation、lock、syscall、sleep、yield 或 backoff。复杂度为
`O(1)`；payload cost 仍与 `sizeof(T)` 及目标 copy code 有关。

### 15.11 Test and proof plan

Public Interface deterministic tests：

- Capacity 1、2、非 power-of-two 3 及较大普通容量；
- 空 pop、填入恰好 Capacity、额外 push 失败；
- FIFO ordering、交替 push/pop 和多次 ring wrap；
- full/empty 失败不修改 Queue/input/output；
- non-default-constructible trivially copyable payload；
- over-aligned payload；
- payload size/alignment checks，以及 `alignof(Queue<T, Capacity>) >= alignof(T)`；
- compile-fail cases：Capacity zero、非法 payload、non-lock-free target gate（可用独立
  compile-probe 验证，不伪造 production atomic trait）。

Concurrent stress tests：

- producer 发布递增 sequence 和 checksum，consumer 验证无丢失、重复、重排或 torn
  payload；
- 随机化 producer/consumer pacing，反复经过 ring wrap；
- 分别覆盖 Capacity 1、非 power-of-two 和典型 command capacity；
- 在 x86_64、aarch64 上运行长时间 stress；
- ThreadSanitizer 作为 data-race detector，结果不是 memory-model proof。

正式实现前建立最小 throwaway/model prototype，使用相同 state transition 和 memory
orders 验证算法可编译并通过 stress。最终证据还必须包含逐操作 C++ memory-model review
和生成代码/atomic linkage audit；prototype 不作为 production source 复制入口。

### 15.12 Rejected alternatives

- **`std::array<T, Capacity>`**：要求 default-constructible `T`，违反 Interface；
- **runtime allocation**：违反 fixed-capacity inline storage；
- **只分配 Capacity slots 并牺牲一个 slot**：实际容量变成 `Capacity - 1`；
- **monotonic counter `% Capacity`**：counter wrap 后对任意 Capacity 不保持 slot sequence；
- **限制为 power-of-two Capacity**：Interface 没有该约束，也降低通用性；
- **每 slot sequence atomic**：避免 extra payload slot，但增加 `Capacity` 个 atomics、
  storage 和证明复杂度，SPSC V1 不需要；
- **shared occupancy atomic/CAS**：让双方写同一热点 atomic，并引入 RMW/CAS retry；
- **`memcpy` payload**：Interface 冻结的是 C++ copy construction/assignment，不需要把
  object lifetime 正确性建立在额外 trivially-copyable 推导上；
- **`memory_order_seq_cst` everywhere**：不能替代 ownership proof，并增加无证据的全局
  ordering；
- **预先 cache-line padding**：性能收益尚未测量，且扩大 object layout。

## 16. `Buffer<T>` publication protocol

状态：已替换

> 本节原 four-slot load/store protocol 已被并发审查否决：writer 可在 reader 宣告读取
> 前选择同一 pair，并随后覆写 reader 正在复制的 slot。以下 §16.1–§16.6 不再是 V1
> 实现规格，仅保留为被否决设计的记录。V1 采用下列三槽 atomic-exchange protocol。
> 相应的 historical four-slot state-model test 不参与 V1 test graph；三槽算法当前以
> production specialization stress 与 memory-model review 验证。

### 16.0 Replacement: three-slot exchange publication

三个 inline raw slots 分别由 writer、reader 和 published state 持有。writer 在自己的
slot 构造 payload 后，以一次 `atomic<uint32_t>::exchange` 发布并取得旧 published slot；
reader 只在观察到新的 publication sequence 时 exchange 自己的已读 slot，并取得完整
publication 的 slot。reader 没有新 sequence 时直接复制它已拥有的 slot。

因此 exchange 的返回 slot 已由对方 relinquish：writer 从不写 reader 当前 slot，reader
从不读 writer 当前 slot。publication state 使用 valid bit、slot index 和 writer-owned
sequence；sequence 只用于 reader 判断是否有新 publication，不进入 public API。

write/read 各进行固定数量的 library-level atomic 操作和一次 payload copy，不含
library retry loop、allocation、mutex 或 syscall。`exchange` 在部分架构可由 LL/SC retry
实现；本模块不再将 Buffer 宣传为无底层 retry、wait-free 或具有硬件级执行时间上界。
`is_always_lock_free` 仍是必要平台条件。

### 16.1 Algorithm choice

V1 使用 Simpson-style four-slot single-writer/single-reader publication mechanism。四个
payload slots 组织为两个 pair，每个 pair 包含两个 slot：

```text
pair 0: slot 0, slot 1
pair 1: slot 0, slot 1

reader announcement  -> reader 当前选择的 pair
published pair        -> writer 最后发布的 pair
published slot[pair]  -> 每个 pair 最后发布的 slot
```

writer 从 reader 当前声明的 pair 之外选择目标 pair，再从该 pair 当前 published slot
之外选择目标 slot。reader 先取得 published pair，声明自己将读取该 pair，再取得其中的
published slot。两个正交选择层共同保证 writer 不修改 reader 正在复制的 payload slot。

该算法满足已经冻结的 Interface：

- writer 不等待读取，允许覆盖未被 reader 观察的 publication；
- reader 不等待写入，且每次成功读取一个完整 publication；
- write/read 都只执行固定数量的 control operations 和一次 payload copy；
- reader 可以返回与并发 write 竞争前已完整发布的值，不承诺调用返回时刻的最新值；
- Buffer 初始 empty，第一次 publication 后可重复读取同一个或更新的完整值。

V1 不使用常见的 triple-buffer atomic-exchange 方案。`is_always_lock_free` 只说明 atomic
operation 不使用锁，并不证明 read-modify-write 在目标指令集上没有 LL/SC retry loop。
four-slot 方案只需要 atomic loads/stores，提供更直接的固定控制步骤证据。

### 16.2 Object representation

概念 representation 为：

```cpp
using Slot = std::aligned_storage_t<sizeof(T), alignof(T)>;

std::array<std::array<Slot, 2>, 2> slots_;

std::atomic<std::uint32_t> reading_pair_{0};
std::atomic<std::uint32_t> published_pair_state_{0};
std::atomic<std::uint32_t> published_slot_0_{0};
std::atomic<std::uint32_t> published_slot_1_{0};
```

`published_pair_state_` 在一个 atomic value 中编码：

- 是否存在有效 publication；
- 最后 publication 所在 pair，值为 zero 或 one。

V1 不使用独立 `has_value_` atomic，避免 reader 需要组合两个不能作为同一次
publication observation 读取的状态。具体 bit encoding 是 private Implementation，但
必须保留一个明确 valid bit，且非法 bit pattern 只可能表示 Implementation invariant
violation。

初始状态是：

```text
reading pair:       0
published pair:     invalid
published slot[0]:  0
published slot[1]:  0
```

因此第一次 write 选择 pair 1、slot 1。四个 raw slots 均不 value-initialize、不清零，也
不开始 `T` lifetime。Buffer construction 的 payload-storage cost 不随 `sizeof(T)` 执行
字节扫描。

控制字段的精确排列和是否使用 atomic array 属于 private layout。V1 不预先加入 cache-line
padding；正确性不得依赖 padding。

### 16.3 Atomic capability and ordering

所有 control atomics 使用固定宽度 `std::atomic<std::uint32_t>`。public template 执行：

```cpp
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
```

pair/slot 值只允许 zero 或 one。V1 对 four-slot control operations 统一使用
`std::memory_order_seq_cst`，而不是在首版中引入依赖精细 fence 推理的弱化 ordering：

- 所有 reader/writer control operations 进入同一个 sequentially-consistent total order；
- published-slot store/load 同时提供 payload publication 的 release/acquire ordering；
- reader 的 pair announcement 与 writer 的 pair selection 具有明确全局先后关系；
- correctness proof 不依赖 compiler 或 architecture 特有的 dependency ordering。

`seq_cst` 是 V1 correctness choice，不是性能声明。不得在缺少新的完整 proof、stress 和
目标生成代码验证时把个别 operation 降为 acquire/release 或 relaxed。

source algorithm 不使用 compare-exchange、fetch operation 或其他 atomic RMW，也不包含
retry loop。C++ 标准仍不承诺每个平台上的 atomic load/store 指令延迟上界；supported
x86_64/aarch64 toolchain 必须检查最终生成代码和链接依赖。若实现被降级为 library lock、
runtime fallback 或隐藏 retry，则该平台不得获得 RT-callable 声明。

### 16.4 `write()` transition

conceptual steps：

```text
reading = reading_pair.load(seq_cst)
target_pair = 1 - reading

current_slot = published_slot[target_pair].load(seq_cst)
target_slot = 1 - current_slot

copy-construct T in slots[target_pair][target_slot]
published_slot[target_pair].store(target_slot, seq_cst)
published_pair_state.store(valid | target_pair, seq_cst)
```

writer 是唯一执行 `write()` 的角色。在 control state 合法且遵守 SW/SR contract 时，
`write()` 没有失败分支：

1. pair selection 避开 reader 在 writer load 之前已经声明的 pair；
2. 如果 reader 在 writer 选择 pair 之后切换到该 pair，slot selection 避开 reader 能够
   选择的旧 published slot；
3. payload construction 完成后才发布 pair-local slot；
4. final pair-state store 使新 publication 成为之后 reader 可以选择的 publication。

final `published_pair_state_` store 完成本次 write transition。即使其 pair bit 与上一次
相同，也必须执行该 atomic store；Implementation 不省略值未变化的 control publication。

write 的 linearization point 是 payload construction 后、新 slot 第一次能被符合协议的
reader 选择的 control store：如果 target pair 已经是公开 pair，则是 pair-local
published-slot store；如果 target pair 尚未公开，则是 final published-pair store。两者都
位于 `write()` invocation 与 return 之间。不能把 final store 一概声明为 linearization
point，因为连续 write 可能复用同一个公开 pair，concurrent reader 可以在 final store 前
通过更新后的 pair-local slot 观察新 publication。

### 16.5 `try_read()` transition

conceptual steps：

```text
state = published_pair_state.load(seq_cst)

if state is invalid:
    return false

pair = decode_pair(state)
reading_pair.store(pair, seq_cst)
slot = published_slot[pair].load(seq_cst)

copy-assign slots[pair][slot] to output
return true
```

initial-invalid load 是 `try_read(false)` 的 decision point。该路径不访问 raw payload
storage，也不修改 caller output。

successful read 必须在取得 pair 后始终执行 reader announcement，包括 atomic 中已经是
相同 pair 的情况；不得把“值没有变化”当作省略 store 的理由。announcement 必须位于
pair selection 和 slot selection 之间。对该经典 protocol 的验证显示，省略看似冗余的
control write 会破坏原 ownership reasoning。

reader 随后的 published-slot load 选择一个已经完整 constructed 的 payload：

- 若它在并发 writer 的 pair-local slot store 之前，reader 取得旧 slot；
- 若它观察到新 slot store，则该 store 的 release side 与 load 的 acquire side 发布新
  payload；
- 两种情况都只读取某一次完整 publication。

成功 read 的 observation point 由其读取到的 published pair/slot control state 决定，而
不是函数返回时刻。第一次成功 publication 后 valid bit 不再恢复为 false，因此没有新
write 时重复调用仍返回 `true` 并可读取同一 publication。

### 16.6 Slot ownership argument

four-slot ownership 分为两个层次：

1. **pair exclusion**：reader announcement 已先于 writer selection 时，writer 选择另一
   pair，因而不会触及 reader 所在 pair 的任意 slot；
2. **slot exclusion**：writer 已在 reader announcement 前选择同一 pair 时，writer 只写
   该 pair 当时 published slot 的另一 slot。reader 在 announcement 后读取 pair-local
   publication state，只会取得旧的已发布 slot，或 writer 完成 construction 后发布的新
   slot。

在统一 seq_cst order 中，reader announcement 和 writer reading-pair load 总有明确先后；
pair-local published-slot load/store 也有明确先后。由此每个 concurrent access 落入上述
两种 exclusion 情况之一，不存在 writer 修改 reader 正在 copy 的 slot。

payload publication 的 happens-before chain 为：

```text
writer payload construction
  sequenced-before
published-slot seq_cst store
  synchronizes-with a reader load that observes that store
  sequenced-before
reader payload copy
```

如果 reader 选择旧 slot，则该 slot 已由更早的相应 publication store 发布。reader pair
announcement 是 storage ownership protocol 的一部分；writer 的 reading-pair load
观察其 seq_cst 顺序并避开相应 pair。四槽算法不得被简化成只依赖 final published-pair
atomic 的 double-buffer protocol。

该 argument 必须在 production implementation review 中展开为逐 event proof，并用
small-state interleaving model 复核。文档论证、stress test 或 `seq_cst` 任一项单独都不
构成完整正确性证明。

### 16.7 Payload object lifetime

Buffer constructor 不在任何 raw slot 中构造 `T`。writer 只在 ownership proof 判定为
不可被 reader 访问的 target slot 中使用 global placement new：

```text
::new (slot_address) T(value)
```

target slot 可能从未包含 `T`，也可能包含更早的同类型 publication。后一种情况下，在
完全重合的 storage 中 placement-construct 同一 non-const `T` 会结束旧 object lifetime
并建立新 lifetime。冻结的 trivially-destructible constraint 保证不需要先执行用户
destructor；copy construction 是 nothrow。

reader 只有在 control protocol 证明 selected slot 包含 live published object 后，才从
raw address 经 `std::launder` 恢复 `T*` 并 copy-assign 到 output。reader 不结束 payload
lifetime；slot 由未来拥有它的 writer replacement，或最终 Buffer storage release 结束。

Buffer destructor 不扫描四个 slots。`T` trivially destructible，因而可以直接释放包含
零到四个 live objects 的 storage。`T` 不需要 default constructor，也不需要额外 per-slot
initialized flags。

### 16.8 Complexity and RT path

每次 `write()` 固定执行：

- 两次 atomic load；
- 两次 atomic store；
- 一次 `T` copy construction；
- 常数次 bit/index calculation。

empty `try_read()` 执行一次 atomic load。successful `try_read()` 固定执行：

- 两次 atomic load；
- 一次 atomic store；
- 一次 `T` copy assignment；
- 常数次 decode/index calculation。

没有 CAS、atomic exchange、retry、allocation、lock、syscall、sleep、yield 或 backoff。
复杂度为 `O(1)`，但 payload copy cost 仍随 `sizeof(T)` 和目标生成代码变化。四个 inline
payload slots 的 object-size cost 是本算法的显式代价，不由 runtime strategy 隐藏。

在 atomic code-generation audit、memory-model review 和目标平台验证完成前，项目只声明
上述 fixed source-level steps，不把 Buffer 宣传为 wait-free、lock-free 或 realtime-safe。

### 16.9 Cache layout decision

V1 不为 control atomics 或 payload pairs 预先加入固定 cache-line alignment/padding：

- correctness 只依赖 ownership 和 atomic ordering；
- Buffer object size 已包含四份 `T`，无证据 padding 会进一步扩大 footprint；
- reader/writer control fields 的 false sharing 影响必须用目标 payload 和周期频率测量；
- `std::hardware_destructive_interference_size` 的可用性和稳定 ABI 含义不足以自动成为
  C++17 public template layout policy。

实现必须保持 `alignof(Buffer<T>) >= alignof(T)`。如 benchmark 证明 layout 是主要瓶颈，
后续优化必须保持四槽 protocol 与 Public Interface 不变，并重新执行 ABI、assembly 和
latency 验证。

### 16.10 Test and proof plan

Public Interface deterministic tests：

- 初始 read 返回 false 且不修改 output；
- 第一次 write/read，以及无新 write 的重复 read；
- 无并发时多次 write 后读取最后 publication；
- 驱动全部 pair/slot 交替路径和大量 state wrap；
- reader 慢于 writer时允许跳过中间值，但结果始终来自某次完整 publication；
- non-default-constructible、trivially-copyable 和 over-aligned payload；
- `alignof(Buffer<T>) >= alignof(T)` 与 object-size/storage checks；
- illegal payload 和 non-lock-free atomic gate 的 compile probes。

Concurrent verification：

- payload 携带 sequence、反码和 checksum，检测 torn 或混合 publication；
- writer outruns reader、reader outruns writer、随机 pacing 和长期高频 stress；
- 用 test-only small-state model 枚举 reader/writer control-step interleavings；
- 特别覆盖重复写入相同 pair bit和重复 announcement，不允许省略 control store；
- x86_64/aarch64 ThreadSanitizer 与目标原生 stress；
- 检查生成汇编、atomic linkage 和不存在隐藏 runtime fallback。

production template 不加入 test hook、virtual clock 或 public seam 来暂停内部 atomic
steps。model 是验证相同 protocol 的独立证据，不能代替 production code review；stress
和 sanitizer 也不能代替 C++ memory-model proof。

### 16.11 Rejected alternatives

- **two-slot/double buffer**：writer 可以在 stalled reader 完成前绕回并覆盖其 slot；
- **three slots plus atomic exchange**：语义可实现，但 RMW 可能使用 LL/SC retry loop，
  bounded-operation evidence 弱于只含 load/store 的 four-slot protocol；
- **seqlock/version retry**：reader 可能在持续 writer activity 下无界重试；
- **`std::atomic<T>`**：对任意已接受 payload size/alignment 不保证 always lock-free，并
  把 payload Interface 限制隐式交给 implementation；
- **per-slot reader flags/CAS ownership**：增加 atomic state、RMW contention 和证明复杂度；
- **mutex/shared mutex**：违反无隐藏锁和 non-waiting contract；
- **弱化 memory ordering 的 four-slot variant**：V1 没有足够价值承担更复杂 proof；
- **省略 unchanged control store**：会改变经典 protocol 的 ordering/ownership premise；
- **返回 pointer/reference/view**：无法在返回后维持 slot ownership，违反已冻结 copy-out
  Interface；
- **`memcpy` payload**：Interface 冻结的是 C++ copy construction/assignment，不需要把
  lifetime reasoning 转换为 byte-copy protocol；
- **自动 sequence/version**：freshness metadata 属于 payload 或调用者协议，不属于 Buffer。

## 17. Platform Support And Atomic Claim Gate

状态：已冻结

### 17.1 Support is a toolchain tuple

Realtime 的支持对象不是单独一个 CPU architecture 或 compiler 名称，而是至少包含：

```text
architecture
compiler and version
C++ standard-library implementation and version
libc / Linux userspace ABI
kernel family
compile and link flags
build type
```

例如“支持 aarch64”不能自动推出任意 aarch64 compiler、`-mcpu`、standard library 和
kernel 组合都具有相同 atomic code generation 或 latency behavior。文档和 validation
report 必须记录完整 tuple，不得把一个验证结果扩展到整个 architecture family。

### 17.2 V1 compatibility floor

V1 冻结以下最低 baseline：

| 项目 | 最低版本或范围 |
|---|---|
| Language | ISO C++17，关闭 GNU language extensions |
| CMake | 3.20 |
| GCC | 10 |
| Clang | 12 |
| Standard library | libstdc++ 10 |
| Linux userspace | glibc 2.31 |
| Linux runtime validation floor | Linux 5.15 或带有等价 vendor backports 的受维护 distribution kernel |
| Architectures | little-endian x86_64 LP64、little-endian aarch64 LP64 |

这是一项维护范围决策，不表示更旧 toolchain 必然错误，也不表示版本号自身能够证明
realtime behavior。选择该 floor 是为了覆盖仍常见的 industrial/embedded toolchain，
同时限制单一维护者需要持续验证的组合数量。

Clang V1 validation 固定搭配 libstdc++，不把 Clang frontend support 自动扩展为 libc++
support。musl、libc++ 和 vendor-modified toolchain 可以进行实验性构建，但在进入独立 CI
tuple 前不属于 declared V1 support。

V1 明确不支持：

- x86 ILP32/x32、ARM32、aarch64 ILP32 或 big-endian target；
- Windows、macOS、bare metal 或 MCU RTOS；
- GCC 以外的编译器仅因兼容 `__GNUC__` macros 而被视为 GCC；
- Clang 以外的编译器仅因兼容 Clang/GCC command-line flags 而获得支持；
- 未记录 compiler flags 的 prebuilt validation claim。

library 不在 runtime 查询 kernel version、CPU model 或 compiler identity。版本 floor 通过
build configuration 和 CI 管理，部署环境兼容性通过 report 管理，不把 policy 编码成
runtime branch。

### 17.3 Baseline target flags

portable validation 使用明确且保守的 architecture baseline：

```text
x86_64:  generic x86-64 ISA baseline
aarch64: Armv8-A baseline
```

CI、release package 和 benchmark 不使用 `-march=native` 或 `-mcpu=native` 生成可对外泛化
的结果。部署项目可以选择更具体的 CPU flags，但该 binary 形成新的 validation tuple，
不能继承 baseline binary audit。

production target：

- 声明 `cxx_std_17` 并设置 `CXX_EXTENSIONS OFF`；
- 不向 consumer 传播 architecture optimization flags；
- 不要求 `-fno-exceptions` 或 `-fno-rtti`；
- 不传播 `-Werror`、sanitizer、coverage、LTO 或 PGO flags；
- 不链接或公开 `libatomic` dependency。

V1 的 RT code-generation evidence 先覆盖普通 `-O2` non-LTO build。Debug、`-O0`、LTO、
PGO 和 sanitizer build 可以用于功能或诊断，但不继承 `-O2` binary 的 hot-path claim。

### 17.4 Hard build gates

component standalone CMake configure 必须执行 compile-only probes。以下条件不满足时直接
失败，不产生“可构建但悄悄降低保证”的 binary：

1. Linux target；
2. compiler ID/version 满足已冻结 baseline；
3. target architecture 和 data model 属于 V1 范围；
4. `sizeof(std::size_t)` 与目标 LP64 model 一致；
5. `std::atomic<std::size_t>::is_always_lock_free` 为 true；
6. `std::atomic<std::uint32_t>::is_always_lock_free` 为 true；
7. required C++17 headers、object-lifetime facilities 和 chrono representation 可用。

这些 probes 只编译、不执行，因此 cross-compilation 不依赖 host 上运行 target binary。
Queue/Buffer public headers 仍分别保留 atomic `static_assert`，防止安装后 consumer 使用不同
toolchain/target 绕过 component configure-time result。

CMake gate 不通过时必须输出失败的具体 prerequisite，不自动：

- 添加 `-latomic`；
- 改用 mutex；
- 降低 memory order；
- 禁用 Queue/Buffer 后继续构建一个缩减版 realtime library；
- 把 unsupported architecture 猜测映射成相近 target。

standard-library vendor、libc 和 runtime kernel floor 属于 validated support matrix，而不是
public header 中的 preprocessor ban。这样不会把标准 C++ headers 污染为特定 distribution
检测逻辑；未经矩阵验证的组合仍明确是 unsupported，而不是 silently supported。

### 17.5 Three distinct claim levels

V1 必须区分以下三个层级：

#### Build-supported

一个 tuple 只有在以下结果持续通过后才可列为 build-supported：

- standalone configure/build/install；
- unit tests；
- installed consumer `find_package()` test；
- public-header self-containment test；
- GCC/Clang warnings review。

build-supported 只表示功能和 packaging 受到维护，不是 RT-callable evidence。

#### RT-evidence-qualified

一个 build-supported tuple 只有进一步完成以下验证，才可以承载对应 operation 的
RT-callable claim：

- Queue/Buffer atomic trait compile gates；
- memory-model 和 object-lifetime review；
- production specialization concurrency stress；
- isolated object/disassembly audit；
- undefined-symbol 和 dynamic dependency audit；
- allocation、locking、syscall path audit；
- native target execution，而不只是 cross-compile 或 emulation。

该资格仍只证明 library-side contract，不提供系统 latency upper bound。

#### PREEMPT_RT-validated

RT-evidence-qualified tuple 在指定 PREEMPT_RT kernel、CPU、firmware/BIOS settings 和 load
profile 上完成可重复测量后，才附加 PREEMPT_RT validation report。报告是环境相关证据，
不是对所有同 architecture hardware 的性能承诺。

三个层级不得通过一个笼统的 `SUPPORTED` macro、README badge 或 CMake option 合并。

### 17.6 Atomic binary audit

`is_always_lock_free` 是必要条件，不是完整的 bounded-execution proof。每个 release tuple
必须建立 isolated audit translation units，分别显式实例化并调用：

- `Queue<T, Capacity>::try_push()`；
- `Queue<T, Capacity>::try_pop()`；
- `Buffer<T>::write()`；
- `Buffer<T>::try_read()`。

audit 至少使用 small scalar payload、典型 command/state payload 和 over-aligned payload，
覆盖 Queue Capacity 1、非 power-of-two 和普通容量。对 object file 与最终 executable：

1. 检查 atomic control path 不调用 `__atomic_*`、`__sync_*` 或 toolchain lock helper；
2. 检查 Queue/Buffer control path 没有 compiler-introduced retry loop；
3. 检查 realtime component shared object 没有 `libatomic` dynamic dependency；
4. 检查 atomic operation 数量和 ordering 与冻结算法一致；
5. 保存 compiler version、flags、target triple、disassembly 和 symbol report 作为 release
   artifact。

audit 不硬编码“x86_64 必须出现某条 opcode”作为源码测试，因为 compiler release 可以用
不同但等价的 instruction sequence。review 关注禁止行为、control flow 和 memory-order
语义；architecture-specific expected patterns 记录在对应 validation report。

### 17.7 Payload specialization boundary

Queue/Buffer template 的 atomic control algorithm 可以按 platform tuple 验证，但任意 `T`
的 copy code generation 不能由少量 project tests 穷举。特别是 trivially-copyable payload
copy 可能被 compiler 展开，也可能降低为 `memcpy` 或其他 target helper。

因此 RT-callable claim 分为两部分：

- component 证明 control path 满足固定操作、atomic 和 ownership contract；
- consumer 对实际 `T` specialization 负责确认 copy cost、间接资源和最终 code generation
  满足其 deadline/RT policy。

project-provided validation payload 只形成代表性证据，不将 claim 扩展到任意大小、对齐和
内容的 `T`。若 specialization 引入 external copy helper，部署审计必须确认它已经完成
lazy binding/pre-touch，并且其实现没有 allocation、locking 或其他不允许行为；否则该
specialization 不得称为 RT-callable。

V1 不通过限制一个武断的 `sizeof(T)` 上限来伪装解决该问题，也不手写 byte-copy loop 来
阻止 compiler optimization。payload cost 继续是 Public Interface 明示的 caller
responsibility。

### 17.8 Native architecture validation

x86_64 和 aarch64 都必须执行 native tests。QEMU/user-mode emulation 可以用于扩大功能
coverage，但不能验证：

- atomic instruction lowering 的真实执行特性；
- cache/coherency contention；
- scheduler latency；
- page faults 和 memory locking；
- PREEMPT_RT behavior。

首次 V1 release 前，GCC 和 Clang 至少各有一个 x86_64 与一个 aarch64 supported tuple。
minimum-version compatibility row 和当前 maintained compiler row 可以分开；公开 support
table 必须列出实际验证组合，不能仅写 `GCC >= 10`、`Clang >= 12` 后假设所有中间及更新
版本都自动获得 RT evidence。

### 17.9 CI and release matrix

每个 change 的普通 CI 至少包含：

- GCC 和 Clang build/test；
- Debug 与 `-O2` Release；
- standalone component 和 installed consumer；
- x86_64 native tests；
- aarch64 compile coverage。

release gate 额外包含：

- x86_64/aarch64 native stress；
- ThreadSanitizer（诊断证据，不用于 latency）；
- AddressSanitizer/UndefinedBehaviorSanitizer 的非实时功能测试；
- atomic object/disassembly/symbol audit；
- PREEMPT_RT measurement tuple；
- validation manifest 汇总 toolchain、kernel、CPU、flags 和结果。

资源不足导致某个 native tuple 未执行时，release notes 必须把它标为 unvalidated；不得用
上一 release 结果或 cross-compile 成功替代本 release evidence。

### 17.10 Claim invalidation

以下任一变化都使既有 binary-level evidence 失效，至少需要重新执行 atomic/code-generation
audit：

- compiler major version 或 standard-library implementation 变化；
- target triple、ABI、architecture flags 或 optimization level 变化；
- 启用或关闭 LTO/PGO/sanitizer；
- Queue/Buffer payload specialization 或 algorithm/memory order 变化；
- atomic type、object layout 或 relevant compile definition 变化。

kernel patch、CPU firmware、BIOS settings、CPU model 或 load profile 变化不一定使 C++
memory-model proof 失效，但会使 PREEMPT_RT measurement claim 失效，必须形成新的环境报告。

### 17.11 Rejected alternatives

- **只检查 compiler version**：不能证明 standard library、target flags 或 atomic lowering；
- **只使用 `is_always_lock_free`**：不能排除 helper call、retrying instruction sequence 或
  payload copy behavior；
- **runtime 自动 fallback**：使同一 Public Interface 在不同机器上隐藏地改变 RT contract；
- **自动链接 `libatomic`**：以 build success 掩盖原子操作不满足 gate；
- **把所有能编译的平台称为 supported**：混淆 source portability 与维护承诺；
- **QEMU 代替 native aarch64**：不能提供真实 atomic、cache、scheduler 或 latency 证据；
- **统一 `ROBO_REALTIME_SAFE` macro**：把 operation、specialization 和 deployment tuple 的
  条件错误压缩成全局布尔值；
- **固定 opcode snapshot 作为唯一测试**：对等价 compiler code generation 过度脆弱；
- **要求整个项目 `-fno-exceptions`**：Public Interface 的 no-throw contract 不需要改变
  consumer 全局语言模式；
- **把 sanitizer 结果当 proof**：sanitizer 只能发现部分执行中的问题，不能证明所有
  interleaving 或 latency bound。

## 18. Test And Validation Implementation

状态：已冻结

### 18.1 Evidence layers

Realtime V1 的验证不是单一 test suite，而是五类互补证据：

| 层级 | 主要目标 | 不能单独证明 |
|---|---|---|
| Deterministic unit | 输入、状态转换、算术、错误语义 | Linux 实际行为、并发正确性、latency |
| Linux integration | production adapter 和真实 kernel contract | 所有错误交错、实时上界 |
| Concurrency stress/model | Queue/Buffer 数据完整性和交错 | C++ memory model 的形式正确性、latency bound |
| Binary/sanitizer audit | 隐藏调用、data race、UB 线索 | 所有执行路径正确、deadline 保证 |
| PREEMPT_RT measurement | 指定环境中的 latency/jitter/page-fault 结果 | 其他硬件或系统的统一性能承诺 |

任何一层通过都不得替代其他层。memory-model 与 object-lifetime review 是独立的设计证据，
不因 stress 或 ThreadSanitizer 通过而省略。

### 18.2 Test dependency policy

V1 tests 只依赖：

- C++17 standard library；
- Linux/POSIX test process facilities；
- CMake/CTest；
- toolchain 自带的 sanitizer、symbol 和 disassembly tools。

不通过 `FetchContent`、git submodule 或 package manager 下载 GoogleTest、Catch2、benchmark
framework 或 mock framework。component-local test support 只提供最小 assertion、退出状态和
诊断位置，不发展为通用测试框架，也不安装或导出。

每个 test executable 应聚焦一个 public type 或一个明确 OS contract。失败通过非零 process
status 报告；需要表达环境不满足的 optional integration case 使用 CTest 明确 skip result，
不得把 skip 打印成 pass。

### 18.3 CMake and CTest controls

standalone component 使用标准 `BUILD_TESTING` 控制测试构建。额外的长时间或测量程序使用
component-scoped options：

```cmake
REALTIME_ENABLE_STRESS_TESTS   # default OFF
REALTIME_ENABLE_PRIVILEGED_TESTS # default OFF
REALTIME_BUILD_BENCHMARKS     # default OFF
```

普通 configure/build/test 不要求 root repository、网络、root privilege 或实时调度权限。
tests 按 CTest label 分类：

```text
unit
integration
model
stress
privileged
consumer
```

`ctest` 默认集合只包含 deterministic unit 和不会改变 runner 全局环境的 integration tests。
stress 必须显式启用；privileged validation 必须由独立 job 显式选择，不能因本机恰好具有
capability 而在普通 CI 隐式运行。

benchmarks 不是 pass/fail unit tests，不默认注册到 CTest。它们输出 measurement artifacts，
由 validation workflow 检查运行有效性和报告完整性。

### 18.4 Deterministic unit executables

按职责使用以下 executable names；只有出现对应 production source 时才创建：

```text
realtime_clock_test
realtime_periodic_schedule_test
realtime_current_thread_test
realtime_process_memory_test
realtime_timing_statistics_test
realtime_queue_test
realtime_buffer_test
```

纯 private arithmetic 可由所属 public-type test target 额外编译 private source/header，避免
为每个 helper 建立小 executable。Public Interface behavior 仍必须从安装式 header 验证。

`realtime_clock_test` 和 `realtime_periodic_schedule_test` 使用第 7 节冻结的 private link-time
monotonic adapter：

- production `clock.cpp`/`periodic_schedule.cpp` 与 test adapter 组合；
- scripted `now()`、absolute sleep、`EINTR` 和 fatal invariant；
- 不在 production object 中加入 runtime function table、virtual dispatch 或 test macro；
- 另有 Linux integration executable 链接正式 production target。

Queue、Buffer 和 TimingStatistics tests 使用显式 payload/observation values，不读取 private
object bytes，不通过 friend/test hook 观察 internal indices 或 slots。

### 18.5 Linux integration executables

Linux integration 必须链接正式安装/构建 target 和 production adapter，至少验证：

- `Clock::now()` 对同 boot monotonic observation 单调不减；
- absolute periodic wait 不早于目标成功返回，`EINTR` 不改变 original target；
- 小于一个 period 的 lateness 与 missed release 分离；
- scheduling、affinity 和 memory-lock errors 保留为 `std::error_code`；
- affinity success 使用独立 OS query 观察，并从当前允许 mask 选择 CPU，不假设 CPU 0；
- process-wide memory-lock operation 在 child process 内执行；
- installed consumer 从 package config 包含、链接和运行最小程序。

真实 sleep test 只断言 clock/order/error semantics，不设置与 CI hardware 无关的严格最大
latency。需要改变 thread/process state 的 test 使用短生命周期 child process 隔离；child
退出即恢复环境，不在共享 test runner 中尝试手写回滚。

`SCHED_FIFO`/`SCHED_RR` success、受限 cpuset 和 memory-lock success/failure 属于 privileged
validation job。普通 CI 必须覆盖参数 validation 和环境允许观察到的系统结果，但权限不足
不能被误判为 production bug，也不能被吞掉为无诊断 pass。

### 18.6 Queue and Buffer stress executables

Queue/Buffer 各自使用独立 stress executable，直接实例化 production public template。
测试协调线程可以使用 ordinary atomics、thread join 和 watchdog；这些 test-only 机制不进入
被测对象或 production target。

Queue producer 发布严格递增 sequence、payload pattern 和 checksum。consumer 验证：

- 没有成功 publication 丢失、重复或重排；
- 没有 torn/mixed payload；
- full 只造成 producer 明确重试或计数，不静默改变成功序列；
- Capacity 1、非 power-of-two 和典型容量都反复 wrap。

Buffer writer 发布递增 sequence、反码和 checksum。reader 验证：

- 每次成功 read 对应某一个完整 publication；
- observed sequence 单调不减，允许跳过；
- reader outruns writer 时重复读取合法；
- writer outruns reader 时不要求观察中间 publication；
- 四个 pair/slot 路径在长期运行中被覆盖。

stress 的工作量以 operation count 为主，而不是只依赖 wall duration。每次 run 记录固定或
显式传入的 seed、capacity、payload shape、operation count 和 toolchain tuple。随机 pacing
只影响调度，不改变 correctness oracle。失败必须打印可复现实参；CI 不通过自动 rerun 把
首次失败转换成 pass。

watchdog 只负责让 broken test 有界终止，不参与通过判定。测试完成协议必须保证 producer
的终止 publication 不会因 Buffer latest-value semantics 被错误当作必须逐项交付的 event。

### 18.7 Small-state model

Queue ring transition 和 Buffer four-slot protocol 分别建立 test-only small-state model：

- 枚举 producer/writer 与 consumer/reader control steps 的合法 interleavings；
- 检查 slot ownership、live-object state、FIFO 或 complete-publication invariant；
- 覆盖 index/pair/slot wrap 与 unchanged atomic stores；
- model state 和 transition 名称逐项映射 Implementation 文档。

model 不复制 production template 后修改 visibility，也不以 model pass 声称 compiler 对
non-atomic payload access 的 C++ memory-model 行为已经证明。它用于发现 state-machine
argument 漏洞，并与独立文字 proof、production stress 和 sanitizer 形成交叉证据。

### 18.8 Sanitizer matrix

Sanitizer build 相互独立：

| 配置 | 范围 | 说明 |
|---|---|---|
| AddressSanitizer + UndefinedBehaviorSanitizer | unit、Linux integration、有限 stress | 检查 lifetime、越界、UB；不是 RT measurement |
| ThreadSanitizer | Queue/Buffer stress 和并发 test support | 检查执行到的 data race；不是 memory-model proof |
| 无 sanitizer `-O2` | 完整功能、stress、binary audit | 承载 release code-generation evidence |

不把 ASan/UBSan 与 TSan 合并为一个 binary。sanitizer runtime 会分配、加锁、拦截 syscall
并显著改变 scheduling，因此 sanitizer build 永远不用于 latency、allocation-free 或
PREEMPT_RT claim。

death tests、privileged scheduling 和 `mlockall(MCL_FUTURE)` 可以与 sanitizer runtime
冲突；这类组合必须按明确 test matrix 排除，而不是为了全绿改变 production semantics。
排除项写入 CTest labels/CI config 和 validation manifest。

### 18.9 Binary and dependency audit executables

第 17.6 节的 isolated audit translation units 是 release test artifacts，不安装。每个被审计
operation 使用可识别的 noinline harness boundary，防止整个调用被 constant-fold/remove，
同时不改变 operation 内部优化。

audit workflow 至少保存：

- compiler/linker 完整版本和 target triple；
- compile/link command 与 optimization flags；
- object symbol/relocation report；
- shared-library dynamic dependencies；
- disassembly；
- 对允许/禁止 call、loop、atomic lowering 的人工或脚本化结论。

脚本可以检查已知 forbidden symbols，但脚本通过不等于 review 完成。compiler 可能使用新
helper 名称或生成无符号的 retry sequence，因此每个新 compiler major version 都触发人工
复核并更新已记录 pattern。

### 18.10 PREEMPT_RT measurement executable

建立 component-local benchmark executable `realtime_periodic_latency`。它只组合已经冻结的
Public Interface，不引入 production
runtime：

1. caller 创建并拥有 measurement thread；
2. 在 hot loop 前设置 affinity/scheduling、锁定内存并预触碰工作集；
3. 显式配置 `PeriodicSchedule`；
4. warm-up 后运行固定 cycle count；
5. hot loop 内不输出、不格式化、不增长 container、不执行未声明工作；
6. 结束后在 Non-RT phase 写出 raw samples 和 summary。

measurement 至少记录：

- release latency；
- wake interval error 和已冻结定义的 absolute jitter；
- execution time；
- overrun count/maximum；
- missed releases；
- hot interval 前后 minor/major page-fault counters；
- wait/configuration errors 和 dropped measurement samples。

如需 percentile/histogram，benchmark 在 Non-RT phase 对预分配并预触碰的 raw samples 做
离线分析；不扩大 `TimingStatistics` Public Interface。连续 `Clock::now()` 的 measurement
overhead 单独报告，不从每个 sample 中隐式扣除。

### 18.11 Measurement validity rules

一次 run 只有满足以下条件才可标记为 valid PREEMPT_RT measurement：

- kernel 明确报告 PREEMPT_RT capability/configuration；
- requested realtime scheduling、affinity 和 memory lock 全部成功；
- warm-up、working-set pre-touch 和 lazy binding preparation 已完成；
- hot interval 没有 major page fault，也没有未解释的 minor page fault；
- sample storage 没有 overflow/drop；
- run 达到记录的 cycle count，且未被 signal/watchdog 提前终止；
- 所有环境 metadata 和 raw artifacts 完整。

不满足条件的 run 仍可保留用于诊断，但必须标记 invalid，不得与有效结果合并。V1 不设置
跨硬件统一 max-latency pass threshold。回归门槛只能针对同一受控 host/profile 单独建立，
并与 library 的普适 Interface contract 分离。

### 18.12 Validation report schema

每份 PREEMPT_RT report 至少记录：

```text
project commit/version and dirty state
report schema version
compiler, standard library, libc, linker and flags
target triple and executable hash
kernel release/config and PREEMPT_RT evidence
CPU model, topology, SMT, governor and frequency policy
firmware/BIOS and relevant kernel command line
isolated CPU, affinity, IRQ/load placement
scheduling policy/priority and memory-lock result
period, first-release value/phase, missed-period policy
warm-up, cycle count, payload/workload and background load
all metric summaries, page faults and errors
raw sample artifact path/hash
start time, duration and operator/environment notes
```

machine-readable metadata/raw data 和 human-readable Markdown summary 同时生成。schema 属于
validation tooling contract，不是 production C++ API；发生不兼容变化时增加 schema version，
不建立公共 `Report` 类型。

报告生成发生在 RT hot loop 之后。不得在周期线程中执行 JSON/CSV formatting、filesystem
I/O、logging 或动态增长 sample storage。

### 18.13 CI and release execution matrix

每次普通 change：

- GCC/Clang deterministic unit tests；
- production Linux integration smoke tests；
- standalone build/install/consumer tests；
- public-header self-containment；
- Debug 与 `-O2` Release；
- aarch64 cross-compile coverage。

定期或 release candidate：

- x86_64/aarch64 native Queue/Buffer long stress；
- ASan+UBSan 和独立 TSan jobs；
- supported tuples 的 binary/dependency audit；
- privileged setup integration；
- PREEMPT_RT measurement 与 report generation；
- static/shared install consumer matrix。

release manifest 对每个 required job 记录 pass、fail、skip 或 not-run。只有 pass 支持相应
claim；skip/not-run 不等于 pass。单一核心开发者可以降低运行频率，但不能降低 release
evidence 的语义或隐藏缺失结果。

### 18.14 Current deferred environment validation

截至 2026-09-12，项目暂时没有可用的 aarch64 native、PREEMPT_RT 或真实硬件验证环境。
以下项目因此标记为 `not-run`，不作为当前 x86_64 软件实现与 Level 1/2 验证的阻塞条件：

- aarch64 native build、atomic code-generation audit 与 Queue/Buffer stress；
- PREEMPT_RT latency/jitter/page-fault measurement 与 report generation；
- 真实 serial/CAN 设备和机器人驱动的 Level 3 hardware validation。

该延期不改变 V1 Interface、实现约束或未来 required-job 定义。它只限制当前可作出的
claim：不得把 realtime component 标记为 aarch64-validated、PREEMPT_RT-validated 或
hardware-validated。环境可用后，必须按本节既有 matrix 运行并把结果写入 release manifest，
不得以本记录替代实际 pass。

### 18.15 Completion criteria

Realtime Implementation 可以进入 production coding，当且仅当：

1. 本文所有 Implementation decisions 已冻结；
2. public API、private seam 和 source layout 无未解释矛盾；
3. Queue/Buffer proof obligations 已转化为明确 tests/reviews；
4. baseline toolchain 可取得并能建立 standalone CI；
5. PREEMPT_RT benchmark/report 流程已定义，即使目标 hardware run 尚待执行。

Realtime V1 只有在 Level 1、Linux integration、并发验证、binary audit 和至少一个有效
PREEMPT_RT report 完成后，才能满足项目 V1 的 Realtime success condition。硬件报告未完成
不阻止先实现，但必须阻止 `PREEMPT_RT-validated` 标记。

### 18.16 Rejected alternatives

- **一个巨型 test executable**：隔离、权限、sanitizer 和失败定位边界不清；
- **依赖 wall-clock sleep 的全部测试**：无法确定性覆盖 EINTR、overflow 和 missed policy；
- **把 private state 暴露给 tests**：让 tests 绑定 representation 并扩大 Interface；
- **追求 100% line coverage 作为正确性目标**：不能表达并发 proof 或真实 OS contract；
- **flaky test 自动重跑后视为通过**：隐藏并发或 timing regression；
- **普通 CI 强制 realtime privilege**：不可移植且会混淆环境失败和代码失败；
- **在 hot loop 输出 samples**：引入 formatting、I/O、allocation 和不可控 latency；
- **只保存 summary**：失去复核 outlier、sample loss 和离线统计的原始证据；
- **设置跨硬件统一 latency threshold**：将环境结果错误提升为库保证；
- **sanitizer run 作为 RT evidence**：instrumentation 改变了被声明的执行路径；
- **benchmark framework 成为 production/test dependency**：扩大依赖且不改善核心测量语义。

## 19. References

- Linux `clock_gettime(2)`：<https://man7.org/linux/man-pages/man2/clock_gettime.2.html>
- Linux `clock_nanosleep(2)`：<https://man7.org/linux/man-pages/man2/clock_nanosleep.2.html>
- C++ working draft `[support.start.term]`：<https://eel.is/c++draft/support.start.term>
- Linux `pthread_setschedparam(3)`：<https://man7.org/linux/man-pages/man3/pthread_setschedparam.3.html>
- Linux `sched_get_priority_min(2)`：<https://man7.org/linux/man-pages/man2/sched_get_priority_min.2.html>
- Linux `pthread_setaffinity_np(3)`：<https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html>
- Linux `mlockall(2)`：<https://man7.org/linux/man-pages/man2/mlockall.2.html>
- C++ working draft `[basic.life]`：<https://eel.is/c++draft/basic.life>
- C++ working draft `[atomics.order]`：<https://eel.is/c++draft/atomics.order>
- C++ working draft `[intro.races]`：<https://eel.is/c++draft/intro.races>
- C++ working draft `[ptr.launder]`：<https://eel.is/c++draft/ptr.launder>
- John Rushby, *Model Checking Simpson's Four-Slot Fully Asynchronous Communication
  Mechanism*：<https://www.csl.sri.com/users/rushby/papers/4slot.pdf>
- GCC 10 release notes：<https://gcc.gnu.org/gcc-10/changes.html>
- Clang 12 command guide：<https://releases.llvm.org/12.0.0/tools/clang/docs/CommandGuide/clang.html>
- CMake 3.20 release notes：<https://cmake.org/cmake/help/v3.20/release/3.20.html>
- Linux kernel release categories and longterm releases：<https://www.kernel.org/category/releases.html>
