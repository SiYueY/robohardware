# robo-hardware V1 架构与 API 重构设计

状态：设计基线候选
阶段：Architecture / API Rebaseline
适用分支：`next`
语言标准：C++17
目标平台：Linux / Embedded Linux / PREEMPT_RT

---

## 1. 背景与目标

robo-hardware 当前包含或计划包含以下低层模块：

* `realtime`：确定性执行基础与 RT/NRT 数据交换原语；
* `serial`：UART / RS-485 transport；
* `spi`：Linux spidev transport；
* `can`：SocketCAN RAW transport；
* `canopen`：未来建立在 CAN transport 之上的协议模块。

此前项目采用 component-first 架构：

* 每个模块独立构建；
* 每个模块独立安装；
* 每个模块维护独立 CMake package；
* 模块之间不共享公共生产 API；
* 可恢复失败主要通过 `std::error_code`、`bool` 或领域专用结果结构表达。

随着实际实现推进，这种结构产生了明显问题：

1. `spi` 等模块自身实现规模较小，而独立 package、安装、consumer test、CMake config 等基础设施占比过高；
2. Realtime、Serial、SPI 和未来 CAN 的返回语义逐渐分化；
3. `std::error_code`、`bool`、领域结果结构混合存在，增加 API 学习成本；
4. RT hot path 缺少统一、固定大小、无异常的错误返回模型；
5. repository 结构更多体现“多个独立发行项目”，而不是实际开发、构建和交付方式。

本次重构将项目重新定义为：

> **一个统一维护、构建、安装和发布的 Linux C++17 低层硬件基础库工程，其中包含多个职责独立、分别生成动态库的功能模块。**

本次重构不建立 HAL、Driver Framework、统一设备继承体系或公共运行时对象。

主要目标：

1. 统一 repository 目录结构；
2. 统一 root build / install / package；
3. 保留多个独立动态库；
4. 保留各领域独立 C++ namespace；
5. 建立 `hardware` 公共基础 namespace；
6. 引入统一的 `hardware::Result<T, E>`；
7. 所有需要错误原因的可失败 API 统一使用 Result；
8. 各模块错误使用简单、固定大小的 `enum class Error`；
9. 明确每个可失败 API 的 failure semantics；
10. 保持 RT hot path 可验证、无异常、无隐藏资源；
11. 在 V1 / 1.0 前完成公共 API 基线重构。

---

## 2. 项目身份与公共命名

### 2.1 `robohardware` 的定位

`robohardware` 只作为：

* GitHub repository 名称；
* repository 根目录名称；
* 项目开发组织名称；

存在。

它不进入消费者侧公共 API。

不得新增：

```text
robohardware::
robohardware::serial
robohardware::spi
robohardware::can
robohardware::realtime

librobohardware_serial.so
librobohardware_spi.so

find_package(robohardware)
```

公共身份由 `hardware` 和各模块自己的领域名称构成。

---

### 2.2 公共基础身份

跨模块共享基础 API 使用：

```cpp
namespace hardware {}
```

公共 include：

```cpp
#include <hardware/result.hpp>
```

统一 CMake package：

```cmake
find_package(hardware CONFIG REQUIRED)
```

`hardware` 表示整个库族的共享基础 API 和统一 package discovery identity。

`hardware` 不对应独立动态库。

不存在：

```text
libhardware.so
hardware::hardware
```

---

### 2.3 模块身份

各运行库保持自己的公共身份：

| 模块       | C++ namespace | Include          | CMake target         | 动态库              |
| -------- | ------------- | ---------------- | -------------------- | ---------------- |
| Realtime | `realtime::`  | `<realtime/...>` | `realtime::realtime` | `librealtime.so` |
| Serial   | `serial::`    | `<serial/...>`   | `serial::serial`     | `libserial.so`   |
| SPI      | `spi::`       | `<spi/...>`      | `spi::spi`           | `libspi.so`      |
| CAN      | `can::`       | `<can/...>`      | `can::can`           | `libcan.so`      |
| CANopen  | `canopen::`   | `<canopen/...>`  | `canopen::canopen`   | `libcanopen.so`  |

消费者示例：

```cpp
#include <hardware/result.hpp>
#include <can/frame.hpp>
#include <realtime/queue.hpp>
```

```cmake
find_package(hardware CONFIG REQUIRED)

target_link_libraries(
    driver
    PRIVATE
        can::can
        realtime::realtime
)
```

---

## 3. 总体模块架构

逻辑结构：

```text
hardware
`-- shared public API vocabulary
    `-- Result<T, E>

realtime
|-- deterministic execution
`-- RT/NRT data exchange primitives

serial
`-- UART / RS-485 transport

spi
`-- Linux spidev transport

can
`-- SocketCAN RAW transport

canopen
`-- future protocol module
```

V1 runtime dependency：

```text
realtime    no project runtime dependency
serial      no project runtime dependency
spi         no project runtime dependency
can         no project runtime dependency
```

所有模块只在编译期共享：

```text
<hardware/result.hpp>
```

该依赖为 header-only，不形成动态库依赖。

未来允许：

```text
canopen -> can
```

禁止因统一工程结构而引入：

```text
serial -> realtime
spi    -> realtime
can    -> realtime
```

等非必要依赖。

---

## 4. Repository 目录结构

目标结构：

```text
robohardware/
├── CMakeLists.txt
├── LICENSE
├── README.md
│
├── cmake/
│   └── hardwareConfig.cmake.in
│
├── include/
│   ├── hardware/
│   │   └── result.hpp
│   │
│   ├── realtime/
│   │   ├── buffer.hpp
│   │   ├── clock.hpp
│   │   ├── current_thread.hpp
│   │   ├── error.hpp
│   │   ├── periodic_schedule.hpp
│   │   ├── process_memory.hpp
│   │   ├── queue.hpp
│   │   └── timing_statistics.hpp
│   │
│   ├── serial/
│   │   ├── configuration.hpp
│   │   ├── error.hpp
│   │   ├── port.hpp
│   │   └── timeout.hpp
│   │
│   ├── spi/
│   │   ├── config.hpp
│   │   ├── device.hpp
│   │   └── error.hpp
│   │
│   ├── can/
│   │   ├── error.hpp
│   │   ├── frame.hpp
│   │   └── ...
│   │
│   └── canopen/
│       └── ...
│
├── src/
│   ├── realtime/
│   ├── serial/
│   ├── spi/
│   └── can/
│
├── tests/
│   ├── hardware/
│   ├── realtime/
│   ├── serial/
│   ├── spi/
│   ├── can/
│   └── integration/
│
├── benchmarks/
│   └── realtime/
│
├── examples/
│   ├── serial/
│   ├── spi/
│   └── can/
│
└── docs/
    ├── project-charter.md
    ├── v1-requirements.md
    ├── architecture.md
    ├── api-conventions.md
    │
    ├── realtime/
    ├── serial/
    ├── spi/
    └── can/
```

没有实际内容的目录不得为了结构对称提前创建。

---

## 5. Public / Private 代码边界

所有 public headers 统一位于：

```text
include/<namespace>/
```

例如：

```cpp
#include <hardware/result.hpp>
#include <serial/port.hpp>
#include <spi/device.hpp>
#include <can/frame.hpp>
#include <realtime/queue.hpp>
```

private implementation 位于：

```text
src/<module>/
```

例如：

```text
src/serial/tty_adapter.hpp
src/serial/tty_adapter_linux.cpp

src/spi/spidev_adapter.hpp
src/spi/spidev_adapter_linux.cpp
```

普通 implementation helper 不进入 public include tree。

默认不建立：

```text
include/*/detail/
include/*/internal/
include/hardware/utils/
```

---

# 6. 统一 API 返回语义

本项目采用四种明确的返回形式。

## 6.1 Value

操作不存在调用者可恢复的失败路径，并产生值：

```cpp
T operation(...) noexcept;
```

例如：

```cpp
realtime::Clock::now();

realtime::Queue<T, Capacity>::capacity();

realtime::TimingStatistics::snapshot();
```

---

## 6.2 Command

操作不存在可恢复失败路径，也没有返回值：

```cpp
void operation(...) noexcept;
```

例如：

```cpp
Buffer::write(...);

TimingStatistics::reset();
```

---

## 6.3 Boolean Status

`false` 是正常控制流状态，并不代表错误，也不需要错误原因：

```cpp
[[nodiscard]]
bool operation(...) noexcept;
```

例如：

```cpp
Queue::try_push();
Queue::try_pop();

Buffer::try_read();

PeriodicSchedule::is_configured();
```

以下状态不是 Error：

```text
Queue full
Queue empty
Buffer 尚未有 publication
```

不得为了表面统一而使用 Result。

---

## 6.4 Result

任何同时满足以下条件的 operation：

```text
可能发生可恢复失败
+
调用者需要知道失败原因
```

必须统一使用：

```cpp
hardware::Result<T, E>
```

如果没有成功 payload：

```cpp
hardware::Result<void, E>
```

因此：

```text
无失败、有值             -> T
无失败、无值             -> void
正常 flow-control         -> bool
可失败、有成功 payload    -> Result<T, E>
可失败、无成功 payload    -> Result<void, E>
```

公共 API 不再直接使用：

```cpp
std::error_code
```

也不新增：

```text
Status
OperationResult
ErrorResult
Expected
```

等平行通用错误模型。

---

# 7. `hardware::Result`

## 7.1 定位

`hardware::Result<T, E>` 是所有模块共享的 operation-result vocabulary。

它只负责表达：

```text
成功
或者
失败
```

它不负责：

```text
日志
错误格式化
重试
恢复策略
设备状态机
诊断字符串
```

---

## 7.2 状态模型

定义：

```cpp
namespace hardware {

template <typename T, typename E>
class [[nodiscard]] Result final;

template <typename E>
class [[nodiscard]] Result<void, E> final;

}
```

任意有效 Result 始终且仅处于：

```text
Value(T)

XOR

Error(E)
```

不得存在：

```text
empty
invalid
uninitialized
valueless_by_exception
```

Result 不提供默认构造。

---

## 7.3 Success

成功：

```cpp
Result<T, E>::success(value);
```

无 success payload：

```cpp
Result<void, E>::success();
```

Success 表示该 operation 定义的成功后置条件已经成立。

---

## 7.4 Failure

失败：

```cpp
Result<T, E>::failure(error);
```

Failure 表示该 operation 没有产生成功结果。

Result 自身不定义 rollback 语义。

具体 API 必须说明失败后的：

```text
对象状态
已发生的外部副作用
是否允许 retry
```

---

## 7.5 Public API

V1 Result 保持最小接口：

```cpp
template <typename T, typename E>
class [[nodiscard]] Result final {
 public:
    using value_type = T;
    using error_type = E;

    template <typename U = T,
              std::enable_if_t<std::is_nothrow_copy_constructible_v<U>, int> = 0>
    static Result success(const T& value) noexcept;
    static Result success(T&& value) noexcept;

    template <typename G = E,
              std::enable_if_t<std::is_nothrow_copy_constructible_v<G>, int> = 0>
    static Result failure(const E& error) noexcept;
    static Result failure(E&& error) noexcept;

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    Result(Result&& other) noexcept;
    Result& operator=(Result&&) = delete;

    ~Result() noexcept;

    [[nodiscard]]
    bool has_value() const noexcept;

    [[nodiscard]]
    explicit operator bool() const noexcept;

    [[nodiscard]]
    T& value() & noexcept;

    [[nodiscard]]
    const T& value() const& noexcept;

    [[nodiscard]]
    T&& value() && noexcept;

    [[nodiscard]]
    E& error() & noexcept;

    [[nodiscard]]
    const E& error() const& noexcept;

    [[nodiscard]]
    E&& error() && noexcept;
};
```

`Result<void, E>`：

```cpp
template <typename E>
class [[nodiscard]] Result<void, E> final {
 public:
    using value_type = void;
    using error_type = E;

    static Result success() noexcept;

    template <typename G = E,
              std::enable_if_t<std::is_nothrow_copy_constructible_v<G>, int> = 0>
    static Result failure(const E& error) noexcept;
    static Result failure(E&& error) noexcept;

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    Result(Result&& other) noexcept;
    Result& operator=(Result&&) = delete;

    ~Result() noexcept;

    [[nodiscard]]
    bool has_value() const noexcept;

    [[nodiscard]]
    explicit operator bool() const noexcept;

    [[nodiscard]]
    E& error() & noexcept;

    [[nodiscard]]
    const E& error() const& noexcept;

    [[nodiscard]]
    E&& error() && noexcept;
};
```

V1 不提供：

```text
value_or
and_then
or_else
transform
transform_error
emplace
swap
operator*
operator->
copy assignment
move assignment
```

Result 是 operation return type，不是完整 `std::expected` clone。

---

# 8. Result 类型约束

Result V1 采用 move-only 设计。

必须支持：

```cpp
hardware::Result<MoveOnlyType, Error>
```

不能要求 `T` 或 `E` copyable。

基础要求：

```text
T/E 为 object type
T/E 非 array
T/E nothrow move constructible
T/E nothrow destructible
```

复制工厂是条件参与的 API。仅当对应类型可 nothrow copy construct 时，才通过 C++17
SFINAE 提供：

```cpp
success(const T&)
failure(const E&)
```

否则该 overload 不参与 overload resolution。右值 factory 和 Result move constructor 依赖
上述 nothrow-move 类级约束，因此始终为 `noexcept`。

Result move construction 必须：

```cpp
noexcept
```

V1 不提供 assignment，从而避免：

```text
Value -> Error
Error -> Value
```

状态切换的额外生命周期复杂度。

---

# 9. Result Accessor Contract

调用：

```cpp
result.value();
```

要求：

```text
result.has_value() == true
```

调用：

```cpp
result.error();
```

要求：

```text
result.has_value() == false
```

违反该前置条件属于 programmer error。

Debug build 可以使用：

```cpp
assert(...)
```

进行辅助检测。

Release build 不通过 exception 或 fallback 改变语义。

---

# 10. Result 的实时属性

Result storage 完全 inline。

Result implementation 本身不得：

* 动态分配；
* mutex；
* condition variable；
* syscall；
* sleep；
* yield；
* logging；
* 字符串格式化；
* hidden retry；
* exception。

所有状态访问均应为固定步骤。

但：

> `hardware::Result<T, E>` 本身不自动拥有 realtime-safe 属性。

RT 属性由以下因素共同决定：

```text
operation
T
E
implementation path
platform evidence
```

---

# 11. Error 模型

## 11.1 Error 属于领域模块

Result 是共享机制。

Error 属于具体领域：

```cpp
realtime::Error
serial::Error
spi::Error
can::Error
```

不得建立：

```cpp
hardware::Error
```

并把所有模块错误放进同一枚举。

---

## 11.2 Error 使用简单枚举

Error 直接定义为：

```cpp
namespace spi {

enum class Error : std::uint8_t {
    InvalidArgument,
    // Other Linux errno values use their complete semantic names.
    InappropriateIoControlOperation,
    InputOutputError,
    // Device-local errors retain concise names.
    AlreadyOpen,
    NotOpen,
    ConfigurationMismatch,
};

}
```

具体枚举成员由各模块自己的 API Design 冻结。

不得额外增加：

```text
ErrorCode
native_code
category
message
```

等层级。

调用：

```cpp
if (!result) {
    switch (result.error()) {
        case spi::Error::InappropriateIoControlOperation:
            ...
            break;

        case spi::Error::InputOutputError:
            ...
            break;
    }
}
```

即可。

---

## 11.3 Error 不表示 Success

Error 不包含：

```text
None
Success
Ok
```

成功/失败已经由 Result state 表达。

因此：

```cpp
hardware::Result<void, spi::Error>::success()
```

表示成功。

而：

```cpp
spi::Error
```

中的每个枚举值都代表真实错误。

---

# 12. Linux errno 的处理

Linux errno、pthread return code、ioctl failure 等属于 implementation detail。

例如：

```text
EACCES
EPERM
ENOENT
ENODEV
EBUSY
ENOTTY
EIO
```

implementation 将其映射为稳定领域错误：

```text
EACCES / EPERM
    -> PermissionDenied

ENOENT / ENODEV
    -> DeviceNotFound

EBUSY
    -> Busy

ENOTTY / EOPNOTSUPP
    -> Unsupported

EIO
    -> Io
```

公共 API 不保存原始 errno。

也不暴露：

```cpp
std::error_code
std::error_category
std::system_category()
std::generic_category()
```

核心原则：

> **公共错误只表达调用者真正需要处理的稳定领域语义；Linux native error 属于实现细节。**

如果未来实际工程证明原始 errno 对诊断必须保留，应单独重新设计 diagnostic seam，而不是提前增加每个 Error 的公共负担。

---

# 13. Error Diagnostic Boundary

Error hot path 只处理枚举值。

可以提供：

```cpp
const char* to_string(Error error) noexcept;
```

用于静态字符串映射。

不得让 Error 或 Result 隐式执行：

```text
std::string allocation
strerror formatting
logging
stack trace
diagnostic report allocation
```

复杂诊断属于 NRT/application layer。

---

# 14. Failure Semantics

每个返回 Result 的 API 都必须明确：

```text
Success 的含义
Failure 的含义
失败后对象状态
外部副作用
调用者是否可以 retry
```

Result 本身不自动提供 transactional semantics。

---

## 14.1 Strong failure guarantee

适用于可以先验证、再提交状态的操作。

例如：

```text
Serial Port::open()
SPI Device::open()
PeriodicSchedule::configure()
```

失败时对象保持调用前状态。

---

## 14.2 Committed-state failure

某些生命周期变化一旦发生就不能通过简单 retry 恢复旧 ownership。

例如：

```text
close()
```

如果 native close 已被调用，即使底层报告异常：

```text
对象仍然进入 closed
不再持有 fd
```

避免重复 close 一个 ownership 已不明确的 descriptor。

---

## 14.3 External-side-effect failure

部分 transport 操作可能在最终报告 failure 前已经被 kernel 或设备部分观察。

例如：

```text
SPI transaction
CAN send
```

因此：

```text
Failure
```

只意味着 API 的完整成功契约没有成立。

不得泛化理解为：

```text
硬件一定完全没有观察到该 operation
```

---

# 15. Value Progress 与 Error 的边界

对于有 value payload 的 API：

```cpp
hardware::Result<T, Error>
```

成功 value 必须能够作为一个完整、独立、有意义的 operation result 解释。

如果 operation 已经产生这样的有效结果，则返回：

```text
Success(value)
```

而不是同时返回：

```text
value + Error
```

因此 Error 不和普通数据 value 绑定。

---

# 16. Serial Transfer 语义

Serial read/write 使用：

```cpp
hardware::Result<std::size_t, serial::Error>
```

而不是：

```text
TransferResult struct
TransferError
bytes_transferred + Error
```

---

## 16.1 Read

```cpp
hardware::Result<std::size_t, Error>
Port::read(
    std::byte* data,
    std::size_t capacity,
    Timeout timeout) noexcept;
```

Success：

```text
Success(n)
```

表示本次调用成功获得 `n` bytes。

`n` 不要求等于 `capacity`。

例如：

```text
capacity = 256
实际读取 = 37

-> Success(37)
```

partial read 是正常结果，不是错误。

如果 deadline 到期且没有获得数据：

```text
Failure(Error::TimedOut)
```

如果没有产生有效数据前设备断开：

```text
Failure(Error::Disconnected)
```

---

## 16.2 Write

```cpp
hardware::Result<std::size_t, Error>
Port::write(
    const std::byte* data,
    std::size_t size,
    Timeout timeout) noexcept;
```

Success：

```text
Success(n)
```

表示本次调用实际完成 `n` bytes。

其中：

```text
n == size
```

表示完整写入。

```text
0 < n < size
```

表示部分写入。

部分写入是有效 operation result，不等于 error。

例如：

```text
requested = 100
transferred = 37

-> Success(37)
```

调用者可以显式继续：

```cpp
auto result = port.write(data, size, timeout);

if (!result) {
    handle(result.error());
    return;
}

const auto written = result.value();

if (written < size) {
    // caller decides whether to continue
}
```

---

## 16.3 单次 Transfer Attempt 规则

每次 `read/write` 都是一次低层 transfer attempt：先等待 readable/writable（最长为
`timeout`），随后只执行一次会产生结果的底层 `read(2)` / `write(2)`。函数不会在内部
为了填满 `capacity` 或 `size` 而循环，也不会在已经得到正 progress 后继续观察终止条件。

底层 syscall 返回 `n > 0` 时立即返回 `Success(n)`；在没有 progress 时失败才返回
`Failure(Error)`。这不是“progress 优先吞掉已经观察到的错误”：一次调用中不会同时观察
成功 transfer 与其后的 timeout、disconnect 或 `EIO`。这保持：

```text
Value
XOR
Error
```

`write()` 的 `Success(n)` 精确定义为 `n` bytes 已被 kernel / TTY driver 接受，**不**表示
这些 bytes 已经物理发送到线路。调用者若需要等待 transmission completion，必须显式调用
`drain()`。

语义严格成立。

---

# 17. Realtime API 重构

Realtime 保持：

```cpp
namespace realtime {}
```

新增：

```text
include/realtime/error.hpp
```

Error 采用简单枚举。

例如可能包含：

```cpp
enum class Error : std::uint8_t {
    InvalidArgument,
    NotConfigured,
    PermissionDenied,
    ResourceUnavailable,
    ValueOverflow,
    Unsupported,
    System,
};
```

具体集合由 Realtime API Design 重新冻结，避免保留无实际使用价值的分类。

---

## 17.1 Current-thread Configuration

从：

```cpp
std::error_code set_current_thread_scheduling(...);

std::error_code set_current_thread_affinity(...);
```

改为：

```cpp
hardware::Result<void, Error>
set_current_thread_scheduling(
    SchedulingPolicy policy,
    int priority) noexcept;

hardware::Result<void, Error>
set_current_thread_affinity(
    unsigned int cpu_index) noexcept;
```

二者继续属于：

```text
Setup-only
```

采用 Result 不改变 RT classification。

---

## 17.2 Process Memory

从：

```cpp
std::error_code lock_process_memory() noexcept;
```

改为：

```cpp
hardware::Result<void, Error>
lock_process_memory() noexcept;
```

保持：

```text
Setup-only
process-wide operation
```

---

## 17.3 PeriodicSchedule

删除当前把 error 嵌入 observation 的形式：

```cpp
struct PeriodicWaitResult {
    std::error_code error;
    ...
};
```

改为纯 observation：

```cpp
struct PeriodicWait final {
    TimePoint scheduled_time;
    TimePoint wake_time;
    Duration period;
    Duration lateness;
    std::uint64_t missed_releases;
};
```

API：

```cpp
class PeriodicSchedule final {
 public:
    PeriodicSchedule() noexcept = default;

    [[nodiscard]]
    hardware::Result<void, Error>
    configure(
        TimePoint first_release,
        Duration period,
        MissedPeriodPolicy policy) noexcept;

    [[nodiscard]]
    bool is_configured() const noexcept;

    [[nodiscard]]
    hardware::Result<PeriodicWait, Error>
    wait_next() noexcept;
};
```

Success：

```text
PeriodicWait 所有字段有效
```

Failure：

```text
不存在伪造 observation
schedule 不推进
```

`EINTR` 仍针对同一个 absolute target 内部继续等待，不作为对外 Error。

---

## 17.4 Queue

保持：

```cpp
bool try_push(const T& value) noexcept;

bool try_pop(T& value) noexcept;
```

Queue full / empty 是 flow-control，不使用 Result。

---

## 17.5 Buffer

保持：

```cpp
void write(const T& value) noexcept;

bool try_read(T& value) noexcept;
```

尚无 publication 是正常状态。

---

## 17.6 TimingStatistics

改为：

```cpp
bool try_observe(
    const PeriodicWait& wait,
    TimePoint completion_time) noexcept;
```

不再消费带 error state 的 `PeriodicWaitResult`。

`PeriodicWait` 一旦存在就是有效 observation。

---

# 18. Serial API 重构

Serial 使用：

```cpp
namespace serial {}
```

新增：

```text
include/serial/error.hpp
```

可能的 Error：

```cpp
enum class Error : std::uint8_t {
    InvalidArgument,
    AlreadyOpen,
    NotOpen,
    TimedOut,
    Unsupported,
    PermissionDenied,
    DeviceNotFound,
    Disconnected,
    Busy,
    Io,
};
```

最终成员根据现有 API 和 Linux 行为重新冻结。

---

## 18.1 Lifecycle

```cpp
hardware::Result<void, Error>
Port::open(
    const std::string& path,
    const Configuration& config) noexcept;

hardware::Result<void, Error>
Port::close() noexcept;

bool Port::is_open() const noexcept;
```

`open()`：

```text
Strong failure guarantee
```

失败后仍保持 closed。

`close()`：

```text
Committed-state failure
```

调用 native close 后对象不再持有 fd。

---

## 18.2 Transfer

统一：

```cpp
hardware::Result<std::size_t, Error>
read(...) noexcept;

hardware::Result<std::size_t, Error>
write(...) noexcept;
```

删除：

```text
TransferError
error + bytes_transferred
```

模型。

实际传输字节数是 operation value，而不是 Error metadata。

---

## 18.3 Flush / Drain

```cpp
hardware::Result<void, Error>
flush(...) noexcept;

hardware::Result<void, Error>
drain(...) noexcept;
```

timeout、unsupported、lifecycle 和 I/O failure 均使用 `serial::Error`。

---

# 19. SPI API 重构

SPI 使用：

```cpp
namespace spi {}
```

新增：

```text
include/spi/error.hpp
```

可能的 Error：

```cpp
enum class Error : std::uint8_t {
    InvalidArgument,
    AlreadyOpen,
    NotOpen,
    Unsupported,
    ConfigurationMismatch,
    PermissionDenied,
    DeviceNotFound,
    Busy,
    Io,
};
```

---

## 19.1 Device

```cpp
class Device final {
 public:
    Device() noexcept = default;
    ~Device() noexcept;

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    Device(Device&&) noexcept;
    Device& operator=(Device&&) = delete;

    [[nodiscard]]
    hardware::Result<void, Error>
    open(
        const std::string& path,
        const Config& config) noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    close() noexcept;

    [[nodiscard]]
    bool is_open() const noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    transfer(
        const std::byte* tx,
        std::byte* rx,
        std::size_t size) noexcept;
};
```

---

## 19.2 SPI Failure Semantics

`open()`：

```text
Strong failure guarantee
```

任意 configuration apply / readback 失败时 Device 保持 closed。

`close()`：

```text
Committed-state failure
```

`transfer()`：

```text
External-side-effect failure
```

Failure 时：

* Device lifecycle state 不改变；
* library 不自动 retry；
* 不承诺 peripheral 未看到任何 clock / CS / MOSI activity。

SPI 不需要专用 TransferResult。

---

# 20. CAN API 基线

CAN 使用：

```cpp
namespace can {}
```

CAN 从首次正式 API Design 起直接采用新的 Result / Error 模型。

典型 API：

```cpp
hardware::Result<void, Error>
open(...) noexcept;

hardware::Result<void, Error>
close() noexcept;

hardware::Result<void, Error>
send(const Frame& frame, ...) noexcept;

hardware::Result<ReceivedFrame, Error>
receive(...) noexcept;
```

具体资源类名称由 CAN API Design 冻结。

---

## 20.1 CAN Error

Error 只表示 transport operation failure。

例如可能包含：

```cpp
enum class Error : std::uint8_t {
    InvalidArgument,
    AlreadyOpen,
    NotOpen,
    TimedOut,
    PermissionDenied,
    InterfaceNotFound,
    Busy,
    Disconnected,
    Io,
};
```

---

## 20.2 CAN Error Frame

必须区分：

```text
SocketCAN error frame
```

和：

```text
can::Error
```

SocketCAN error frame 是成功收到的总线事件。

因此：

```text
receive error frame
    -> Success(ReceivedFrame::ErrorFrame)
```

而不是：

```text
Failure(can::Error)
```

transport Error 只用于：

```text
socket/lifecycle/timeout/kernel operation failure
```

`ReceivedFrame` 是固定存储、无动态分配的 tagged value；它能明确表示
`ClassicalFrame`、`FdFrame` 或 `ErrorFrame`。具体采用自定义 tagged union 还是等价实现
由 CAN API Design 冻结，但 `receive()` 的公共语义不依赖文字中的 `Frame / ErrorFrame`
歧义。

SocketCAN configuration 必须明确区分：

```text
CAN_RAW_FILTER      -> normal Classical/FD frame filtering
CAN_RAW_ERR_FILTER  -> ErrorFrame subscription
CAN_RAW_FD_FRAMES   -> permits FD; receive then distinguishes Classical and FD
```

---

## 20.3 Protocol Error

未来：

```text
CANopen SDO abort
```

属于协议结果。

不得映射成：

```text
can::Error
```

必须保持：

```text
frame validation error
transport error
bus diagnostic event
protocol error
```

互相独立。

---

# 21. Frame Factory

需要 runtime validation 的 frame value type 可以采用：

```cpp
hardware::Result<ClassicalFrame, FrameError>
ClassicalFrame::data(...) noexcept;

hardware::Result<ClassicalFrame, FrameError>
ClassicalFrame::remote(...) noexcept;

hardware::Result<FdFrame, FrameError>
FdFrame::data(...) noexcept;
```

`FrameError` 可以同样采用简单 enum class。

例如：

```cpp
enum class FrameError : std::uint8_t {
    InvalidLength,
    InvalidFlags,
    InvalidFrameType,
};
```

不要将 frame construction error 混入 transport `can::Error`。

---

# 22. CMake 与安装模型

## 22.1 单一 Root Project

统一入口：

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix <prefix>
```

各模块不再维护 repository-style 独立：

```text
<module>/cmake/
<module>Config.cmake
独立 package version
独立 install package
```

---

## 22.2 Dynamic Library Targets

内部 target：

```text
realtime
serial
spi
can
```

导出：

```text
realtime::realtime
serial::serial
spi::spi
can::can
```

生成：

```text
librealtime.so
libserial.so
libspi.so
libcan.so
```

未来：

```text
libcanopen.so
```

---

## 22.3 Unified CMake Package

安装：

```text
hardwareConfig.cmake
hardwareConfigVersion.cmake
realtimeTargets.cmake
serialTargets.cmake
spiTargets.cmake
canTargets.cmake
```

每个模块 target 必须安装到自己的 export set；`hardwareConfig.cmake` 只负责按已安装
模块 `include()` 对应的 `*Targets.cmake`。一个 export 文件只能有一个 CMake namespace，
因此不得恢复单一 `hardwareTargets.cmake` 并期望它导出多个领域 namespace。

消费者：

```cmake
find_package(hardware CONFIG REQUIRED)

target_link_libraries(
    driver
    PRIVATE
        serial::serial
        realtime::realtime
)
```

不存在：

```text
hardware::hardware
libhardware.so
```

---

## 22.4 Install Tree

目标：

```text
<prefix>/
├── include/
│   ├── hardware/
│   ├── realtime/
│   ├── serial/
│   ├── spi/
│   └── can/
│
├── lib/
│   ├── librealtime.so
│   ├── libserial.so
│   ├── libspi.so
│   └── libcan.so
│
└── lib/cmake/hardware/
    ├── hardwareConfig.cmake
    ├── hardwareConfigVersion.cmake
    ├── realtimeTargets.cmake
    ├── serialTargets.cmake
    ├── spiTargets.cmake
    └── canTargets.cmake
```

`canopen` 在其未来里程碑实际实现并安装后，才增加对应 include root、dynamic library 和
export file；V1 install tree 不包含占位 CANopen artifact。

---

# 23. Link Independence

统一 package 不意味着统一链接。

例如：

```cmake
target_link_libraries(
    application
    PRIVATE
        spi::spi
)
```

不得自动引入：

```text
libserial.so
libcan.so
librealtime.so
```

`hardware::Result` 是 header-only，不形成 runtime dependency。

---

# 24. Documentation Structure

统一：

```text
docs/
├── project-charter.md
├── v1-requirements.md
├── architecture.md
├── api-conventions.md
│
├── realtime/
├── serial/
├── spi/
└── can/
```

---

## 24.1 `api-conventions.md`

统一冻结：

* public return semantics；
* `hardware::Result`；
* module-local Error；
* Result accessor contract；
* Linux errno mapping；
* value / error exclusivity；
* progress semantics；
* failure semantics；
* realtime error-handling constraints。

模块文档不重复整套 Result philosophy。

---

## 24.2 Module API Documentation

每个 Result-returning operation 必须说明：

```text
Signature
Success semantics
Failure conditions
Error values
Object state after failure
External side effects
Retry responsibility
Realtime safety
```

---

# 25. Realtime Safety

采用 Result 不改变 operation-level RT classification。

每个 API 继续单独分类：

```text
RT-callable
Setup-only
Non-RT
```

评审至少检查：

* allocation；
* locking；
* syscall；
* blocking；
* hidden retry；
* exception；
* payload behavior；
* thread ownership；
* external synchronization。

例如：

```cpp
hardware::Result<void, realtime::Error>
set_current_thread_affinity(...)
```

仍然是 Setup-only。

而：

```cpp
hardware::Result<PeriodicWait, realtime::Error>
PeriodicSchedule::wait_next()
```

可以基于其 absolute wait contract 保持 RT-callable。

---

# 26. Testing

## 26.1 Result

新增：

```text
tests/hardware/result_test.cpp
```

至少验证：

* Value state；
* Error state；
* `Result<void, E>`；
* move construction；
* move-only T；
* move-only E；
* non-default-constructible T；
* non-default-constructible E；
* `T == E`；
* alignment；
* active member destruction；
* rvalue accessors；
* `[[nodiscard]]`；
* noexcept traits；
* compile-time invalid type rejection；
* no allocation；
* manual union lifetime。

结合：

```text
ASan
UBSan
```

验证 lifetime implementation。

---

## 26.2 Error Mapping

每个模块测试 Linux failure 到领域 Error 的映射。

根据模块适用情况覆盖：

```text
invalid argument
permission denied
device/interface not found
busy
unsupported
timeout
disconnect
configuration mismatch
generic I/O failure
```

---

## 26.3 Result Semantics

重点验证：

```text
Result success/error exclusive
Result<void,E> success
Result<void,E> failure
Result<T,E> value preservation
```

---

## 26.4 Serial Transfer

必须验证：

```text
full read/write
partial read/write
timeout without progress
successful transfer stops after one syscall
failure before transfer progress
disconnect before progress
```

成功的 syscall 返回 `Success(n)` 后不得继续内部等待或进行第二次 I/O；失败 syscall
在未产生 progress 时返回对应 Error。`write` tests 还必须验证文档没有将 accepted bytes
误表述为物理线路发送完成。

---

## 26.5 Failure Semantics

验证：

```text
open strong guarantee
configure strong guarantee
close committed-state behavior
SPI transfer object-state stability
PeriodicSchedule wait failure does not advance schedule
```

---

# 27. 重构顺序

本次重构严格按以下顺序执行。

## Phase 1 — Architecture Rebaseline

修改：

```text
docs/project-charter.md
docs/v1-requirements.md
docs/architecture.md
CONTEXT.md
```

新增：

```text
docs/api-conventions.md
```

首先删除旧的：

```text
standalone distribution component
禁止公共 Result
std::error_code 为主要公共错误模型
generic Result 不进入 public API
```

等设计结论。

---

## Phase 2 — Result

将现有 Result 迁移并重构为：

```text
include/hardware/result.hpp
```

namespace：

```cpp
hardware
```

完成 Result tests。

---

## Phase 3 — Repository Layout

将：

```text
realtime/include/...
serial/include/...
spi/include/...
```

统一迁移到：

```text
include/
```

private implementation 迁移到：

```text
src/
```

测试迁移到：

```text
tests/
```

---

## Phase 4 — Root CMake

建立：

```text
一个 root project
一个 hardware package
多个 shared-library targets
每模块一个 CMake export file
```

确保：

```text
realtime::realtime
serial::serial
spi::spi
can::can
```

可独立链接。

---

## Phase 5 — Realtime

完成：

* `realtime::Error`；
* current-thread API Result 化；
* process-memory API Result 化；
* `PeriodicWaitResult` 重构为纯 `PeriodicWait`；
* `wait_next()` 改为 `Result<PeriodicWait, Error>`；
* TimingStatistics 适配；
* tests / docs 更新。

---

## Phase 6 — Serial

完成：

* `serial::Error`；
* lifecycle Result 化；
* read/write 改为 `Result<size_t, Error>`；
* 删除旧 `TransferResult` / error-field model；
* 冻结 single transfer-attempt semantics；
* flush / drain Result 化；
* tests / docs 更新。

---

## Phase 7 — SPI

完成：

* `spi::Error`；
* open / close / transfer Result 化；
* failure semantics；
* tests / docs 更新。

---

## Phase 8 — CAN API Baseline

基于新统一约定设计：

* frame model；
* frame factory；
* Error；
* lifecycle；
* send；
* receive；
* timeout；
* ErrorFrame semantics；
* failure semantics。

如果 CAN production implementation 尚未进入当前阶段，不提前实现超出 V1 需求的内容。

---

## Phase 9 — Cleanup

全局搜索并处理：

```text
std::error_code
common::Result
old hardware/result.hpp path
PeriodicWaitResult
TransferResult with embedded error
TransferError
ErrorCode
native_code
Error::None
standalone component package
find_package(serial)
find_package(spi)
find_package(realtime)
robohardware::
librobohardware_*
```

注意：

implementation 内部使用：

```text
errno
pthread return code
Linux constants
```

属于正常实现，不需要删除。

需要清理的是 public API 和已经失效的设计模型。

---

## Phase 10 — Final Verification

验证：

```text
root configure
build
unit tests
integration tests
install
consumer project
public-header self-contained build
sanitizer
shared-library dependencies
```

消费者必须可以：

```cmake
find_package(hardware CONFIG REQUIRED)

target_link_libraries(
    test_app
    PRIVATE
        spi::spi
)
```

并确认不会错误链接其他模块。

---

# 28. 明确排除

本次重构不引入：

```text
HAL
ITransport
TransportBase
DeviceBase
BusBase
HardwareManager
runtime polymorphism framework
plugin system
service locator
dependency injection framework
logging runtime
thread runtime
background worker
unified scheduler
custom allocator
third-party expected/result
C++20 dependency
```

统一 Result 不得成为扩大架构范围的理由。

---

# 29. 最终设计原则

重构完成后必须满足：

## Repository

```text
一个 repository
一个 root build
一个 install
一个 CMake package
```

## Runtime

```text
多个独立动态库
```

## Shared Identity

```text
hardware::
<hardware/...>
find_package(hardware)
```

## Domain Identity

```text
realtime::
serial::
spi::
can::
canopen::
```

## CMake Targets

```text
realtime::realtime
serial::serial
spi::spi
can::can
canopen::canopen
```

## Dynamic Libraries

```text
librealtime.so
libserial.so
libspi.so
libcan.so
libcanopen.so
```

## Error Model

```text
module-local enum class Error
```

没有：

```text
ErrorCode
native_code
Error::None
std::error_code public API
```

## Result Model

所有可失败 API：

```text
Result<T, Error>
```

或者：

```text
Result<void, Error>
```

## Return Rule

```text
无失败、有值             -> T
无失败、无值             -> void
正常 flow-control         -> bool
可失败、有值             -> Result<T, Error>
可失败、无值             -> Result<void, Error>
```

## Value / Error Rule

```text
Value XOR Error
```

不得同时返回：

```text
有效 value
+
error code
```

## Serial Progress Rule

```text
已产生有效 bytes progress
    -> Success(bytes)

尚未产生 progress 且发生 terminal failure
    -> Failure(Error)
```

## Realtime Rule

```text
Result != automatically realtime-safe
```

RT 属性始终由：

```text
operation
+
T/E
+
implementation path
+
platform evidence
```

共同决定。

---

# 30. 结论

本次重构最终形成：

```text
                 hardware
          shared API vocabulary
                    |
          Result<T, Error>
                    |
        ┌───────────┼───────────┐
        │           │           │
    realtime      serial       spi       can
       .so          .so         .so       .so
       │            │           │         │
 realtime::      serial::     spi::      can::
```

`robohardware` 只承担 repository 组织身份。

消费者实际面对的是：

```text
hardware
realtime
serial
spi
can
```

所有需要失败原因的 operation 统一采用：

```cpp
hardware::Result<T, module::Error>
```

或：

```cpp
hardware::Result<void, module::Error>
```

各模块 Error 保持简单：

```cpp
enum class Error
```

不引入额外 error wrapper，不保存 native errno，不把普通数据 progress 塞进 Error。

最终模型追求的是：

```text
统一
简单
强类型
固定大小
无异常
低运行时开销
低使用负担
明确的领域边界
可验证的 realtime contract
```

该设计作为 Realtime、Serial、SPI、CAN 以及未来 CANopen API Design 的共同上位约束。
