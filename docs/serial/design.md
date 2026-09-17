# Serial V1 完整重构设计与实施规范

## 1. 目标、范围与设计原则

### 1.1 组件定位

Serial V1 是 `robohardware` 中面向 Linux 平台的底层串口通信组件，目标是提供一套成熟、稳定、可长期作为机器人驱动基础依赖的 UART / RS-485 同步通信能力。

组件关注的是：

* Linux TTY 串口资源；
* UART 线路配置；
* Linux driver-managed RS-485；
* 同步数据收发；
* 有界等待与非阻塞访问；
* RX/TX 队列状态与控制；
* modem/control line；
* BREAK；
* 串口设备发现；
* 统一错误模型；
* 明确的资源所有权和执行时间语义。

它不是协议库，也不是通用终端框架。

V1 明确不负责：

* Modbus、私有协议或 packet framing；
* `readline()`、`read_until()`、字符串处理；
* 自动重试和自动重连；
* 后台线程；
* callback、future、coroutine；
* runtime UART 配置动态修改；
* native fd 暴露；
* Linux console、line discipline、PTY 管理；
* 外部 GPIO 控制 RS-485 DE/RE；
* vendor-specific workaround；
* RS-485 addressing、termination、RS-422 等高级扩展。

### 1.2 设计目标

整个重构必须同时满足：

**命名精确。**

Public API 使用串口领域本身的术语，例如：

```text
read
write
drain
RTS
DTR
CTS
DCD
BREAK
```

而不是为了复用实现细节设计模糊的：

```text
operate()
set_line()
control()
action()
```

**功能正交。**

不同用户能力保持独立。

例如：

```text
write
    提交 TX 数据

bytes_pending
    查询 TX queue

discard_output
    丢弃 TX queue

drain
    等待 TX completion
```

四者底层虽然可能使用相近的 TTY 能力，但用户语义完全不同，不应为了减少函数数量而合并。

**错误统一。**

所有可恢复错误统一使用：

```cpp
hardware::Result<T, serial::Error>
```

不混用：

* exception；
* `errno`；
* `bool + out parameter`；
* `optional`；
* `TransferError{error, progress}`；
* operation-specific error class。

**资源唯一。**

`Port` 是 fd 的唯一 owner。

不允许：

* copy；
* shared ownership；
* native fd escape；
* double close；
* half-open object。

**实时可预测。**

需要进入周期性路径的操作必须：

* 无 hidden allocation；
* 无 hidden mutex；
* 无 background thread；
* timeout 使用 `CLOCK_MONOTONIC`；
* timeout 表示整个 operation deadline；
* EINTR 不重置 deadline；
* partial transfer 直接返回；
* 不隐藏 loop-until-complete。

**接口最小但能力完备。**

“接口最小”不是减少函数数量，而是：

> 不提供没有独立用户价值的 public capability。

因此：

```cpp
set_rts()
set_dtr()
cts()
dsr()
```

虽然是多个函数，但对应不同串口能力，应保留。

而：

```cpp
read_byte()
read_vector()
read_string()
write_byte()
write_string()
```

只是 `read()/write()` 的便利包装，不进入 V1。

### 1.3 V1 能力边界

V1 应覆盖以下完整能力面：

```text
Resource
├── open
├── close
└── is_open

Configuration
├── baud rate
├── data bits
├── parity
├── stop bits
├── flow control
└── RS-485

Data I/O
├── blocking read/write
├── bounded read/write
└── immediate read/write

Readiness
├── wait readable
└── wait writable

Queue
├── RX bytes available
├── TX bytes pending
├── discard RX
├── discard TX
├── discard both
└── drain TX

Control / Status
├── RTS
├── DTR
├── CTS
├── DSR
├── RI
├── DCD
└── BREAK

Discovery
└── list_ports
```

完成这组能力后，V1 public surface 应冻结，不再因为看到新的 ioctl 或其他库的 convenience API 而继续扩展。

---

## 2. Public API 设计

### 2.1 Public Header 结构

建议最终保持：

```text
include/serial/
├── config.hpp
├── error.hpp
├── port.hpp
└── tool.hpp
```

职责：

```text
config.hpp
    UART 与 RS-485 配置模型

error.hpp
    serial::Error

port.hpp
    一个串口资源及其运行时能力

tool.hpp
    与具体 Port 实例无关的 discovery/tool API
```

删除：

```text
configuration.hpp
timeout.hpp
```

不新增：

```text
backend.hpp
interface.hpp
manager.hpp
utils.hpp
platform.hpp
```

除非未来真实职责要求。

---

### 2.2 Config

建议：

```cpp
namespace serial {

enum class DataBits : std::uint8_t {
    Five = 5,
    Six = 6,
    Seven = 7,
    Eight = 8,
};

enum class Parity : std::uint8_t {
    None,
    Odd,
    Even,
    Mark,
    Space,
};

enum class StopBits : std::uint8_t {
    One = 1,
    Two = 2,
};

enum class FlowControl : std::uint8_t {
    None,
    XonXoff,
    RtsCts,
};

struct Config final {
    struct RS485 final {
        bool enabled{false};

        bool rts_on_send{true};
        bool rts_after_send{false};
        bool receive_during_transmit{false};

        std::chrono::milliseconds delay_before_send{0};
        std::chrono::milliseconds delay_after_send{0};
    };

    std::uint32_t baud_rate;

    DataBits data_bits{DataBits::Eight};
    Parity parity{Parity::None};
    StopBits stop_bits{StopBits::One};
    FlowControl flow_control{FlowControl::None};

    RS485 rs485{};
};

}  // namespace serial
```

`Config` 是普通 value object。

不再使用：

```text
PortConfig
Rs485Config
RtsLevel
private fields
friend class Port
factory constructor
builder
```

使用方式：

```cpp
serial::Config config{115200};

config.parity = serial::Parity::Even;
config.flow_control = serial::FlowControl::RtsCts;
```

RS-485：

```cpp
serial::Config config{1'000'000};

config.rs485.enabled = true;
config.rs485.rts_on_send = true;
config.rs485.rts_after_send = false;
config.rs485.receive_during_transmit = false;
```

配置只在：

```cpp
Port::open()
```

时应用。

成功 open 后冻结。

如果需要修改：

```text
close
→ 修改 Config
→ open
```

不提供 runtime：

```text
set_baud_rate
set_parity
set_flow_control
set_rs485
```

---

### 2.3 RS-485

V1 只支持 Linux driver-managed direction control。

必须支持：

```text
enabled
RTS during TX
RTS after TX
receive during TX
delay before TX
delay after TX
```

不支持：

```text
external GPIO direction
manual DE/RE
RS-485 address mode
termination
RS-422
vendor workaround
```

配置过程必须：

```text
request
→ apply
→ readback
→ verify
```

如果 driver 返回成功但清除了请求 flag：

```text
Error::Unsupported
```

禁止 silent fallback。

`enabled == false` 的定义必须明确：

> 请求普通 UART mode。

而不是：

> 不处理当前 RS-485 state。

---

### 2.4 Error

建议：

```cpp
enum class Error : std::uint8_t {
    InvalidArgument,
    InvalidState,

    AlreadyOpen,
    NotOpen,

    WouldBlock,
    TimedOut,

    Unsupported,

    PermissionDenied,
    DeviceNotFound,
    NotTerminal,
    Busy,
    Disconnected,

    OutOfMemory,
    Io,
};
```

语义：

`InvalidArgument`

表示调用本身非法，例如：

```text
nullptr + non-zero size
negative timeout
invalid Config value
```

`InvalidState`

表示操作本身合法，但当前对象状态不允许，例如：

```text
RS-485 automatic RTS active
→ set_rts()
```

`AlreadyOpen`

表示当前 `Port` 已持有 fd。

`Busy`

表示 OS / driver resource busy，两者不能混淆。

`WouldBlock`

仅用于 immediate I/O 无法立即取得进展。

`TimedOut`

仅用于 bounded operation deadline 到期。

`Unsupported`

表示请求是合法 serial capability，但当前 kernel / driver / hardware 不支持。

`Disconnected`

表示已建立的 port 在运行中断开。

`OutOfMemory`

主要用于 `list_ports()` 等允许 allocation 的 control-plane API。

`Io`

作为最后的稳定 fallback，不用于掩盖本可以准确分类的错误。

---

### 2.5 Port

最终建议：

```cpp
class Port final {
public:
    Port() noexcept = default;
    ~Port() noexcept;

    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;

    Port(Port&& other) noexcept;
    Port& operator=(Port&& other) = delete;

    [[nodiscard]]
    hardware::Result<void, Error> open(
        const std::string& path,
        const Config& config) noexcept;

    [[nodiscard]]
    hardware::Result<void, Error> close() noexcept;

    [[nodiscard]]
    bool is_open() const noexcept;

    [[nodiscard]]
    hardware::Result<std::size_t, Error> read(
        std::byte* data,
        std::size_t size) noexcept;

    [[nodiscard]]
    hardware::Result<std::size_t, Error> read(
        std::byte* data,
        std::size_t size,
        std::chrono::nanoseconds timeout) noexcept;

    [[nodiscard]]
    hardware::Result<std::size_t, Error> try_read(
        std::byte* data,
        std::size_t size) noexcept;

    [[nodiscard]]
    hardware::Result<std::size_t, Error> write(
        const std::byte* data,
        std::size_t size) noexcept;

    [[nodiscard]]
    hardware::Result<std::size_t, Error> write(
        const std::byte* data,
        std::size_t size,
        std::chrono::nanoseconds timeout) noexcept;

    [[nodiscard]]
    hardware::Result<std::size_t, Error> try_write(
        const std::byte* data,
        std::size_t size) noexcept;

    [[nodiscard]]
    hardware::Result<void, Error> wait_readable(
        std::chrono::nanoseconds timeout) noexcept;

    [[nodiscard]]
    hardware::Result<void, Error> wait_writable(
        std::chrono::nanoseconds timeout) noexcept;

    [[nodiscard]]
    hardware::Result<std::size_t, Error>
    bytes_available() const noexcept;

    [[nodiscard]]
    hardware::Result<std::size_t, Error>
    bytes_pending() const noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    discard_input() noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    discard_output() noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    discard_buffers() noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    drain() noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    set_rts(bool asserted) noexcept;

    [[nodiscard]]
    hardware::Result<bool, Error>
    rts() const noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    set_dtr(bool asserted) noexcept;

    [[nodiscard]]
    hardware::Result<bool, Error>
    dtr() const noexcept;

    [[nodiscard]]
    hardware::Result<bool, Error>
    cts() const noexcept;

    [[nodiscard]]
    hardware::Result<bool, Error>
    dsr() const noexcept;

    [[nodiscard]]
    hardware::Result<bool, Error>
    ri() const noexcept;

    [[nodiscard]]
    hardware::Result<bool, Error>
    dcd() const noexcept;

    [[nodiscard]]
    hardware::Result<void, Error>
    set_break(bool asserted) noexcept;

private:
    int fd_{-1};
};
```

`Port` 是唯一资源 owner。

不提供：

```text
native_handle
fd
release
share
clone
```

move assignment 保持删除。

原因是目标对象如果已经拥有 fd，就需要在 assignment 中隐式 close，而 close 可能失败，但普通 move assignment 无法返回 `Result`。

---

### 2.6 I/O contract

`read()`、`write()`、`try_read()`、`try_write()` 都采用：

> single-progress semantics。

例如：

```text
read(buffer, 256, 2ms)
```

底层第一次成功读取：

```text
17 bytes
```

立即：

```text
Success(17)
```

不继续填满 256。

`write()` 同理。

partial transfer 是成功，不是 error。

blocking API：

```cpp
read(data, size);
write(data, size);
```

允许无限等待，用于：

* 非 RT thread；
* initialization；
* tool；
* control plane。

bounded API：

```cpp
read(data, size, timeout);
write(data, size, timeout);
```

使用：

```text
CLOCK_MONOTONIC
absolute deadline
```

timeout 覆盖整个 operation。

EINTR 不刷新 timeout。

immediate API：

```cpp
try_read();
try_write();
```

绝不等待。

当前不能取得进展：

```text
WouldBlock
```

不通过 `timeout == 0` sentinel 模拟。

---

### 2.7 Readiness、Queue 与 Line Control

`wait_readable()`：

> 等待 port readable，但不消费数据。

`wait_writable()`：

> 等待 port 可以接受数据，但不提交数据。

`bytes_available()`：

> 返回 RX queue 当前 byte count snapshot。

`bytes_pending()`：

> 返回 TX queue 当前 pending byte count snapshot。

`discard_input()`：

> 丢弃 RX backlog。

`discard_output()`：

> 丢弃尚未发送的 TX bytes。

`discard_buffers()`：

> 同时丢弃 RX/TX。

`drain()`：

> 等待已经提交的 TX 数据完成发送。

这里特意不用 `flush_*`，避免“flush output”被误解为主动发送。

modem/control line 直接使用领域名：

```text
set_rts / rts
set_dtr / dtr
cts
dsr
ri
dcd
```

不设计：

```text
ControlLine
InputLine
set_line
line_state
modem_status
```

因为 public API 应服务用户认知，而不是服务 ioctl 实现复用。

BREAK：

```cpp
set_break(bool asserted)
```

调用者自己控制 timing。

不提供：

```text
send_break(duration)
```

避免隐藏 sleep / timer policy。

---

### 2.8 Tool API

`tool.hpp` 用于：

> 与具体 `Port` 实例无关的 serial tooling/discovery。

V1 提供：

```cpp
struct PortInfo final {
    struct USB final {
        bool available{false};

        std::uint16_t vendor_id{0};
        std::uint16_t product_id{0};

        std::string serial_number;
        std::string manufacturer;
        std::string product;
    };

    std::string path;
    std::string description;

    USB usb{};
};

[[nodiscard]]
hardware::Result<std::vector<PortInfo>, Error>
list_ports() noexcept;
```

不用：

```text
hardware_id
```

opaque string。

USB metadata 必须结构化。

没有串口：

```text
Success(empty vector)
```

不是 error。

某个设备 metadata 不完整时：

* 保留该 `PortInfo`；
* 缺失字段为空；
* 不让整个 enumeration 失败。

只有 enumeration 本身失败才返回 error。

Linux 实现优先：

```text
/sys/class/tty
→ 判断 real device backing
→ 映射 /dev node
→ sysfs metadata enrichment
```

避免依赖 hard-coded：

```text
ttyUSB*
ttyACM*
ttyS*
...
```

也避免为了 enumeration 引入 `libudev` 强依赖。

---

## 3. 内部实现与代码质量要求

### 3.1 实现结构

建议内部保持简单：

```text
src/serial/
├── port.cpp
├── tool.cpp
├── tty_adapter.hpp
└── tty_adapter_linux.cpp
```

只有当确实形成稳定、独立职责时，才考虑增加：

```text
configuration.cpp
error.cpp
```

不为了目录整齐人为拆分。

内部禁止逐步演化成：

```text
backend/
platform/
manager/
common/
helpers/
utils/
factory/
strategy/
```

这类泛化架构。

Serial 是 Linux-only 模块。

不设计：

```cpp
ISerialBackend
SerialBackend
PosixBackend
LinuxBackend
```

除非未来真的出现第二个平台需求。

### 3.2 Syscall seam

允许存在私有 Linux syscall seam，用于：

* deterministic unit test；
* EINTR injection；
* errno injection；
* clock simulation；
* rollback testing；
* ioctl readback testing。

例如：

```cpp
namespace serial::detail {

int open_path(...);
int close_fd(...);

int get_termios(...);
int set_termios(...);

int wait_fd(...);

ssize_t read_fd(...);
ssize_t write_fd(...);

int ioctl_fd(...);

int monotonic_time(...);

}
```

要求：

* private；
* stateless；
* narrow；
* 不出现在 public header；
* 不使用 virtual；
* 不成为通用 portability abstraction。

---

### 3.3 fd ownership 与 open transaction

内部状态必须始终满足：

```text
fd_ == -1
    Port closed

fd_ >= 0
    Port uniquely owns fd
```

任何错误路径都必须证明：

```text
no fd leak
no double close
no half-open state
```

`open()` 不应一开始就直接写：

```cpp
fd_ = ::open(...);
```

应采用 temporary ownership：

```text
temporary fd
→ configure
→ verify
→ commit
```

完整流程：

```text
validate path/config
→ check AlreadyOpen
→ open temporary fd
→ verify TTY
→ capture original relevant state
→ raw-mode configuration
→ baud/data/parity/stop/flow configuration
→ RS-485 configuration
→ readback
→ validation
→ commit fd_
```

任一步失败：

```text
rollback where necessary
→ close temporary fd
→ return error
```

保证：

```text
open success
    Port fully usable

open failure
    Port equivalent to state before call
```

---

### 3.4 Configuration implementation

termios 操作不得散落在整个 `port.cpp`。

建议内部围绕职责组织：

```text
apply_raw_mode
apply_data_bits
apply_parity
apply_stop_bits
apply_flow_control
apply_baud_rate
apply_rs485
validate_effective_configuration
```

优先实现为私有 free functions，而不是大量 classes。

要求：

* 输入明确；
* 无隐藏状态；
* 无动态分配；
* `noexcept`；
* 易于单测。

配置必须进行 readback。

不能认为：

```text
tcsetattr success
=
requested config effective
```

至少验证：

```text
baud
data bits
parity
stop bits
flow control
RS-485 enable
RS-485 RTS polarity
RS-485 RX during TX
RS-485 delays
```

如果 driver sanitize 请求：

```text
Unsupported
```

而不是接受一个与 public request 不一致的配置。

---

### 3.5 Baud implementation

公共 API 使用：

```cpp
std::uint32_t baud_rate;
```

实现优先：

```text
standard termios baud
```

必要且可验证时再支持：

```text
termios2 / BOTHER
```

不优先使用 legacy custom divisor workaround。

所有 baud 设置都需要 readback 验证。

---

### 3.6 I/O 与 deadline

bounded operation 必须共用一个 deadline infrastructure。

基本模型：

```text
deadline = monotonic_now + timeout

loop:
    remaining = deadline - monotonic_now

    if remaining <= 0
        TimedOut

    wait(remaining)

    EINTR
        continue

    ready
        execute one read/write
```

禁止：

```text
每次 EINTR 重新等待完整 timeout
```

也禁止：

```text
while total < requested
    keep reading/writing
```

successful transfer 一旦产生 progress 就返回。

`try_read()` / `try_write()`：

* 不执行 blocking wait；
* 不 sleep；
* 不 spin；
* 不隐藏 retry-until-progress；
* `EAGAIN/EWOULDBLOCK` → `WouldBlock`。

`wait_readable()` 与 `wait_writable()` 应复用同一个 internal wait primitive，不维护独立 timeout 算法。

---

### 3.7 Error handling

所有 syscall 都必须检查返回值。

禁止：

```cpp
tcsetattr(...);  // ignored
```

errno 使用规则：

* 只有 syscall 失败后读取；
* 必须在执行其他 syscall 前保存；
* 不允许根据 stale errno 判断结果；
* public API 不暴露 raw errno。

错误映射集中管理，但不能机械做一个“任何 errno → Error”的万能表。

可以采用：

```text
common errno mapping
+
operation-specific mapping
```

例如：

```text
EAGAIN in try_read
→ WouldBlock

EAGAIN in configuration ioctl
→ 根据该 ioctl contract 分类
```

不能把所有 `EAGAIN` 都映射成 `WouldBlock`。

---

### 3.8 Modem、Queue 与 BREAK

modem line 内部可以共享：

```text
TIOCMGET
TIOCMBIS
TIOCMBIC
```

但 public API 不受这个实现方式影响。

当：

```text
RS-485 driver-managed RTS
```

或：

```text
RtsCts flow control
```

正在自动控制 RTS 时：

```cpp
set_rts(...)
```

必须返回：

```text
InvalidState
```

不能悄悄覆盖自动控制状态。

Queue API 必须保持语义严格：

```text
bytes_available
    query only

bytes_pending
    query only

discard_*
    destructive

drain
    blocking completion
```

BREAK 只做 assert/clear。

内部不得添加 duration sleep。

---

### 3.9 实时与资源约束

以下运行时路径禁止 heap allocation：

```text
read
write
try_read
try_write
wait_readable
wait_writable
bytes_available
bytes_pending
discard_*
drain
RTS/DTR/CTS/DSR/RI/DCD
set_break
```

以下 control-plane API 允许 allocation：

```text
list_ports
PortInfo strings
filesystem/sysfs discovery
```

Serial implementation 不得：

* 创建 thread；
* 创建 worker；
* 使用 hidden mutex；
* 使用 condition variable；
* 使用 hidden queue；
* 使用 background timer。

并发 contract：

> V1 不保证一个 `Port` 的共享多线程访问安全。

如果多个线程访问，调用者负责同步。

---

### 3.10 Logging、exception 与 side effect

底层 Serial implementation 不打印：

```text
stdout
stderr
syslog
ROS log
```

错误统一通过 `Result` 返回。

Public/runtime path 不使用 exception。

对于 `list_ports()` 中 STL allocation 可能产生的异常，需要保证：

```cpp
list_ports() noexcept
```

不被破坏。

如果保留 `OutOfMemory`：

```text
std::bad_alloc
→ Error::OutOfMemory
```

不得让异常跨越 public API。

---

### 3.11 代码规范

项目目标为 C++17。

Serial 实现至少要求：

```text
-Wall
-Wextra
-Wpedantic
```

并在项目现有策略允许的情况下启用：

```text
-Wconversion
-Wshadow
```

要求：

* clang-format clean；
* clang-tidy 关键 correctness rule clean；
* 无 owning raw pointer；
* 无 C-style cast；
* 无未初始化变量；
* 无隐式 narrowing；
* 无无意义 macro；
* header 内无 `using namespace`；
* magic number 必须使用具名常量或 Linux UAPI symbol。

类型优先：

```text
std::byte
std::size_t
std::uint32_t
std::chrono
enum class
```

避免：

```text
int timeout_ms
unsigned baud
long delay
```

---

### 3.12 注释规范

注释的职责是解释：

* 非显然约束；
* Linux 特殊语义；
* workaround 原因；
* ownership decision；
* error mapping decision；
* timeout/deadline 原因。

好的注释例如：

```cpp
// Keep the descriptor non-blocking so all waiting is controlled by the
// monotonic deadline path rather than termios VMIN/VTIME.
```

```cpp
// Some drivers silently clear unsupported RS-485 flags while returning
// success. Read the configuration back before committing the open.
```

```cpp
// EAGAIN maps to WouldBlock only for immediate data operations.
```

不要写：

```cpp
// Increment i.
++i;
```

Public header 注释重点描述 contract：

* 是否 blocking；
* 是否允许 partial transfer；
* timeout 是什么；
* 是否修改 queue；
* 重要 error 条件。

不要只有：

```cpp
/// Opens the port.
```

这种重复函数名的无价值注释。

TODO / FIXME 不允许无限期存在。

已知限制应：

* 写入文档；
* 建 issue；
* 或立即解决。

---

## 4. 测试、验证与工业化要求

Serial V1 的测试目标不是追求代码覆盖率数字，而是验证所有 public contract 和危险错误路径。

### 4.1 Unit Test

通过 syscall seam deterministic 覆盖：

```text
open failure
close failure
isatty failure
termios read failure
termios apply failure
termios readback mismatch

RS-485 unsupported
RS-485 apply failure
RS-485 driver sanitization
RS-485 readback mismatch

read partial
write partial
EAGAIN
EINTR
POLLHUP
POLLERR
disconnect

deadline expiry
multiple EINTR before deadline
spurious wakeup

queue ioctl errors
modem line ioctl errors
BREAK errors
```

尤其必须证明：

```text
EINTR does not reset deadline
partial transfer is success
failed open leaves Port closed
rollback does not leak fd
```

### 4.2 PTY Integration Test

PTY 用于验证真实 Linux TTY 行为：

* open/close；
* raw mode；
* blocking read/write；
* bounded I/O；
* partial transfer；
* peer disconnect；
* queue behavior；
* readiness；
* HUP handling。

不能只依赖 mock。

mock 验证分支；

PTY 验证 Linux 行为。

两者职责不同。

### 4.3 Hardware Validation

需要保留少量真机 smoke/integration test：

```text
USB-UART
native UART if available
RS-485 adapter
```

验证：

* real termios behavior；
* baud readback；
* modem line；
* RS-485 ioctl；
* driver sanitization；
* disconnect。

硬件测试不要求进入所有 CI runner，但必须有明确执行方式和记录。

### 4.4 `list_ports()` Test

需要独立 discovery seam 或可控 sysfs fixture。

至少覆盖：

```text
no ports
normal tty
USB tty
missing optional metadata
invalid sysfs entry
device disappears during enumeration
duplicate/path handling
allocation failure
```

单个 metadata 失败不能让整个 enumeration 失败。

### 4.5 Sanitizer 与 Static Analysis

非 RT test build 至少支持：

```text
ASan
UBSan
```

如未来并发 contract 扩大，再加入 TSan。

Static analysis：

```text
clang-tidy
```

重点检查：

* lifetime；
* use-after-move；
* narrowing；
* unchecked return；
* bugprone；
* undefined behavior；
* resource leak。

不要通过大面积 suppression 换取表面 clean。

### 4.6 CI 验收

CI 至少应检查：

```text
configure/build
unit tests
PTY integration tests
clang-format
warning clean
sanitizer build/test
install
find_package consumer test
```

如项目已有统一 lint 工具，则 Serial 必须直接接入统一流程，不建立特殊例外。

---

## 5. 实施计划与完成标准

整个重构建议控制在四个阶段。

### 阶段一：冻结 API 与工程约束

完成：

```text
include/serial/config.hpp
include/serial/error.hpp
include/serial/port.hpp
include/serial/tool.hpp
```

同时完成正式 API contract 文档，冻结：

* 类型；
* 命名；
* blocking 语义；
* timeout；
* partial transfer；
* error；
* ownership；
* RS-485；
* discovery。

同时冻结本文件中的：

* 实现原则；
* coding standard；
* comment standard；
* test requirements。

本阶段不以现有实现为理由修改 public design。

### 阶段二：核心 Linux 实现重构

完成：

```text
fd ownership
open transaction
termios configuration
baud
parity
flow control
RS-485
error mapping
deadline infrastructure
read/write
readiness
queue
modem lines
BREAK
```

优先保证：

```text
correctness
→ testability
→ maintainability
→ optimization
```

禁止边实现边继续扩 public API。

### 阶段三：Discovery 与系统测试

完成：

```text
tool.hpp implementation
list_ports()
sysfs discovery
PortInfo metadata
PTY integration
fault injection
hardware smoke tests
sanitizers
```

重点确认：

```text
discovery ≠ runtime fast path
```

不能为了 `list_ports()` 把 filesystem/string/allocation 逻辑扩散到 `Port`。

### 阶段四：工业化收尾

完成：

```text
documentation
examples
format
lint
warning cleanup
install/export
CI
consumer test
independent code review
```

最后一次 code review 不只看“功能有没有实现”，必须按以下维度逐项检查：

```text
API
ownership
state model
error handling
deadline
Linux semantics
allocation
locking
rollback
readback
disconnect
test coverage
comments
maintainability
```

Serial V1 只有同时满足以下条件才算完成：

1. Public API 与冻结设计完全一致；
2. 不保留旧 API compatibility residue；
3. V1 capability matrix 全部实现；
4. fd ownership 唯一且所有失败路径无 leak；
5. `open()` 具有完整事务语义；
6. 配置关键字段进行 readback；
7. RS-485 driver sanitization 可检测；
8. bounded I/O 使用 monotonic absolute deadline；
9. EINTR 不重置 deadline；
10. read/write partial progress 正确返回；
11. immediate I/O 不隐藏阻塞；
12. fast path 无 heap allocation；
13. fast path 无 hidden lock/thread；
14. errno mapping 稳定且集中；
15. 所有 syscall return value 被检查；
16. disconnect 能与普通 I/O error 区分；
17. queue / modem / BREAK 行为均有测试；
18. `list_ports()` 能正确处理 metadata 缺失；
19. PTY integration test 通过；
20. sanitizer 无已知问题；
21. warning / format / lint clean；
22. install 和 downstream `find_package` 验证通过；
23. Public API 注释描述真实 contract；
24. 非显然 Linux 行为均有必要注释；
25. 没有无解释的 TODO / FIXME / workaround。

最终目标不是实现一个：

> “可以工作的 termios wrapper”。

而是交付一个：

> **能力完整、API 稳定、行为严格、资源安全、时间语义明确、错误可预测、代码可审计、异常路径经过验证，并能够长期作为机器人硬件驱动基础依赖的 Linux Serial 组件。**
