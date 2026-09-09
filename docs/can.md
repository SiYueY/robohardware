# RoboHardware CAN 模块设计

## 1. 定位与边界

### 1.1 模块定位

`can` 是面向 Linux SocketCAN 的通用 C++17 CAN 基础工具模块。

它负责提供：

* Classic CAN Frame；
* CAN Filter；
* Linux SocketCAN Socket；
* non-blocking RX / TX；
* RX 时间信息；
* CAN Error Frame 解码；
* Bus Event 与 Bus State；
* 基础运行统计；
* 结构化错误；
* 面向上层协议的最小 CAN Interface。

典型用途包括：

```text
CANopen
自定义 CAN 协议
机器人硬件通信
驱动器通信
传感器通信
CAN 测试工具
```

CAN 模块只解决：

> 如何可靠、清晰地通过 Linux SocketCAN 发送、接收和诊断 CAN Frame。

它不理解具体设备或上层协议。

---

### 1.2 平台与版本范围

V1：

```text
Language:
    C++17

Platform:
    Linux

Backend:
    SocketCAN / CAN_RAW

Protocol:
    Classic CAN
```

实现允许直接使用：

```text
socket()
bind()
setsockopt()
recvmsg()
send()
fcntl()
poll()
epoll()

linux/can.h
linux/can/raw.h
linux/can/error.h
```

当前没有第二个平台，因此不建立：

```text
Platform
Backend
TransportFactory
SocketBackend
DriverBackend
```

等跨平台抽象。

---

### 1.3 非目标

V1 不负责：

```text
CANopen
CiA 301
CiA 402

NMT
PDO
SDO
SYNC
Heartbeat
EMCY

Device state machine
Motor semantics
Robot safety
System lifecycle

ROS / ROS2

Bus Manager
Singleton
Global Registry

Thread Pool
Executor
Async Runtime
Callback Dispatcher
```

CAN interface 的系统配置也不属于本模块：

```text
bitrate
sample point
restart-ms
interface up/down
CAN FD enable
```

这些应由：

```text
iproute2
systemd-networkd
deployment scripts
system configuration
```

完成。

---

### 1.4 核心原则

V1 必须遵守：

1. CAN 层只表达 CAN bus 和 CAN Frame 事实。
2. CAN 层不得包含 CANopen 或设备语义。
3. Linux `can_frame` 和 CAN flag 不泄漏到 public API。
4. Standard / Extended、Data / Remote 使用明确领域类型。
5. Error Frame 与普通 Frame 使用不同语义。
6. Socket 默认并固定为 non-blocking。
7. 不创建隐藏 RX/TX worker。
8. 不隐藏 retry、sleep 或后台 queue。
9. 一个 Linux CAN interface 可以同时拥有多个 Socket。
10. Filter 属于单个 Socket。
11. `send()` 成功只代表 kernel TX path 接受 Frame。
12. 并发依赖明确 ownership，而不是任意线程安全。
13. 所有时间戳必须明确 clock domain。
14. 运行路径不得因为诊断逻辑产生无界工作。
15. 内部队列必须 bounded。
16. 没有真实需求的功能不进入 V1。

最终设计目标是：

> API 简单，但 CAN 语义完整；实现轻量，但关键行为明确。

---

## 2. 数据模型

### 2.1 Frame

V1 只支持 Classic CAN：

```cpp
enum class FrameFormat : std::uint8_t {
    Standard,
    Extended,
};

enum class FrameType : std::uint8_t {
    Data,
    Remote,
};

struct Frame {
    std::uint32_t id{0};
    std::uint8_t size{0};

    FrameFormat format{FrameFormat::Standard};
    FrameType type{FrameType::Data};

    std::array<std::byte, 8> data{};
};
```

`FrameFormat` 和 `FrameType` 是明确的 CAN domain semantics，因此不应简化成：

```cpp
bool extended;
bool remote;
```

### FrameFormat

```text
Standard
    11-bit CAN identifier

Extended
    29-bit CAN identifier
```

### FrameType

```text
Data
    normal CAN data frame

Remote
    RTR frame
```

---

### 2.2 Frame validation

发送前必须验证：

```text
Standard:
    id <= 0x7FF

Extended:
    id <= 0x1FFFFFFF

Classic CAN:
    size <= 8
```

非法 Frame 返回：

```cpp
ErrorCode::InvalidFrame
```

并且必须在进入 kernel syscall 之前拒绝。

CAN 层不得校验：

```text
COB-ID
Node ID
PDO payload
SDO payload
device-specific command
```

这些属于上层协议。

---

### 2.3 Linux Frame 映射

Public API 不得暴露：

```text
CAN_EFF_FLAG
CAN_RTR_FLAG
CAN_ERR_FLAG

CAN_SFF_MASK
CAN_EFF_MASK

canid_t
struct can_frame
```

转换仅存在于 SocketCAN implementation。

例如内部可以有：

```cpp
::can_frame to_native(const Frame&) noexcept;
Result<Frame> from_native(const ::can_frame&) noexcept;
```

但这些不是 public API。

---

### 2.4 Remote Frame

`FrameType::Remote` 表示 RTR。

`size` 表示请求的数据长度。

对于 Remote Frame：

* `data` 不具有普通 Data Frame payload 语义；
* CAN 层不得解释 `data`；
* native conversion 必须正确设置和解析 RTR flag。

---

### 2.5 CAN FD

V1 不支持 CAN FD。

不得为了未来 CAN FD 将：

```cpp
std::array<std::byte, 8>
```

提前改为：

```cpp
std::array<std::byte, 64>
```

未来如需 CAN FD，应独立增加：

```cpp
struct FdFrame;
```

并独立处理：

```text
64-byte payload
BRS
ESI
CAN_RAW_FD_FRAMES
CANFD_MTU
```

---

### 2.6 Filter

```cpp
struct Filter {
    std::uint32_t id{0};
    std::uint32_t mask{0};
    FrameFormat format{FrameFormat::Standard};
};
```

Filter 只表达 CAN identifier matching。

不得包含：

```text
CANopen COB-ID
Node ID
PDO type
protocol ownership
```

Validation：

```text
Standard:
    id <= 0x7FF
    mask <= 0x7FF

Extended:
    id <= 0x1FFFFFFF
    mask <= 0x1FFFFFFF
```

---

### 2.7 Filter 与 SocketCAN

Linux `CAN_RAW_FILTER` 最终使用：

```cpp
struct can_filter {
    canid_t can_id;
    canid_t can_mask;
};
```

匹配语义：

```text
received_can_id & mask
    ==
filter_can_id & mask
```

Standard / Extended 必须显式参与匹配。

对于 Standard：

```text
CAN_EFF_FLAG expected = 0
```

对于 Extended：

```text
CAN_EFF_FLAG expected = 1
```

因此 native filter conversion 必须把 EFF flag 正确加入：

```text
can_id
can_mask
```

否则：

> Extended Frame 与 Standard Frame 低 11-bit 相同时可能错误匹配。

这是 V1 必须测试的关键行为。

Filter 在 `Socket::open()` 时安装。

V1 不提供：

```cpp
set_filters()
add_filter()
remove_filter()
clear_filters()
```

即：

> Socket 打开以后 Filter 不动态修改。

---

### 2.8 Timestamp 与 RxInfo

公共时间类型：

```cpp
using Timestamp =
    std::chrono::time_point<
        std::chrono::steady_clock,
        std::chrono::nanoseconds>;
```

表示 monotonic domain。

```cpp
struct RxInfo {
    Timestamp received_at{};
};
```

`received_at` 表示：

> 用户态成功接收到该 CAN Frame 时记录的 monotonic timestamp。

它不等于：

```text
controller hardware RX timestamp
exact physical bus arrival time
```

---

### 2.9 Kernel timestamp

Linux 提供：

```text
SO_TIMESTAMPNS
SO_TIMESTAMPING
```

但不同 timestamp 可能属于：

```text
CLOCK_REALTIME
system clock
hardware clock
PHC
```

等不同 clock domain。

因此 V1 必须保证：

> 不能把 kernel timestamp 未经 clock-domain 验证直接转换成 `steady_clock::time_point`。

尤其禁止：

```text
CLOCK_REALTIME timespec
    ↓ direct cast
steady_clock::time_point
```

V1 首先保证：

```text
RxInfo::received_at
    = 明确的 monotonic userspace receive timestamp
```

未来如果确实需要 kernel/hardware RX timestamp，再单独设计：

```text
TimestampSource
clock domain
clock conversion
PHC synchronization
```

---

### 2.10 Event 与 State

正常 CAN Frame 与 Error Frame 是不同语义。

Error Frame 不作为：

```cpp
Frame
```

返回给 protocol 层。

定义：

```cpp
enum class State : std::uint8_t {
    Unknown,
    Active,
    Warning,
    Passive,
    BusOff,
};
```

以及：

```cpp
enum class EventType : std::uint8_t {
    Warning,
    Passive,
    BusOff,
    Restarted,

    ArbitrationLost,
    ControllerError,
    ProtocolError,
    RxOverflow,

    Unknown,
};

struct Event {
    EventType type{EventType::Unknown};
    Timestamp timestamp{};

    std::uint32_t detail{0};
};
```

`State` 表示：

> 当前 best-known CAN bus/controller state。

`Event` 表示：

> 已发生的一次离散诊断事实。

例如：

```text
BusOff Event
    ↓
State = BusOff
```

但：

```text
ArbitrationLost Event
```

通常不会形成长期 State。

无法可靠判断状态时：

```cpp
State::Unknown
```

优于猜测。

---

### 2.11 Stats

运行统计保持最小：

```cpp
struct Stats {
    std::uint64_t rx_frames{0};
    std::uint64_t tx_frames{0};

    std::uint64_t rx_errors{0};
    std::uint64_t tx_errors{0};

    std::uint64_t error_frames{0};
    std::uint64_t rx_overruns{0};
    std::uint64_t dropped_events{0};
};
```

含义：

```text
rx_frames
    成功返回给用户的正常 CAN Frame

tx_frames
    成功提交给 kernel TX path 的 Frame

rx_errors
    真正 receive/socket I/O errors
    EAGAIN 不计入

tx_errors
    send/socket I/O errors

error_frames
    收到的 SocketCAN Error Frame

rx_overruns
    已知 RX overflow / overrun

dropped_events
    内部 Event queue 满导致丢弃的事件
```

不得统计：

```text
PDO timeout
SDO abort
Heartbeat timeout
EMCY
CiA402 fault
device fault
```

---

### 2.12 Error

CAN 层使用统一结构化错误。

```cpp
enum class ErrorCode {
    InvalidArgument,
    InvalidFrame,
    InvalidState,

    OpenFailed,
    IoFailed,
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

不为每个 syscall 创建独立 ErrorCode，例如：

```text
BindFailed
FilterFailed
TimestampFailed
SetsockoptFailed
ReceiveFailed
SendFailed
IoctlFailed
```

具体 Linux 原因保存在：

```cpp
native_code
```

中。

---

## 3. Socket API 与行为

### 3.1 Interface

保留一个最小 `Interface` 作为上层 protocol boundary：

```cpp
class Interface {
public:
    virtual ~Interface() = default;

    virtual Result<void> send(
        const Frame& frame) noexcept = 0;

    virtual Result<bool> receive(
        Frame& frame,
        RxInfo& info) noexcept = 0;

    virtual bool try_pop_event(
        Event& event) noexcept = 0;
};
```

它的目的只有两个：

1. CANopen / custom protocol 不直接依赖 Linux SocketCAN；
2. protocol test 可以使用 fake/in-memory CAN implementation。

`Interface` 不负责：

```text
open / close
fd
state
stats
filters
Linux configuration
```

因此它不是 backend framework。

---

### 3.2 Socket

具体 Linux SocketCAN implementation：

```cpp
class Socket final : public Interface {
public:
    struct Options {
        std::string interface;

        bool error_frames{true};
        bool receive_own{false};

        std::vector<Filter> filters;
    };

    static Result<Socket> open(
        Options options);

    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;

    Result<void> send(
        const Frame& frame) noexcept override;

    Result<bool> receive(
        Frame& frame,
        RxInfo& info) noexcept override;

    bool try_pop_event(
        Event& event) noexcept override;

    State state() const noexcept;
    Stats stats() const noexcept;

    int fd() const noexcept;
};
```

使用：

```cpp
auto socket = can::Socket::open({
    .interface = "can0",
});
```

名称使用：

```text
can::Socket
```

而不是：

```text
can::SocketCan
```

因为 namespace 和模块定位已经明确 SocketCAN context。

---

### 3.3 生命周期

采用：

> factory + RAII

成功返回的 `Socket` 必须已经：

```text
socket created
configured
filtered
bound
ready
```

因此不提供：

```cpp
open()
close()
is_open()
```

不存在：

```text
constructed-but-not-open
partially-open
double-open
```

等中间 public state。

析构负责关闭 fd。

---

### 3.4 open() 流程

`Socket::open()` 推荐顺序：

```text
validate Options
    ↓
socket(PF_CAN, SOCK_RAW, CAN_RAW)
    ↓
set O_NONBLOCK
    ↓
resolve interface index
    ↓
configure SocketCAN options
    ↓
install filters
    ↓
configure error-frame mask
    ↓
configure receive-own-message
    ↓
bind()
    ↓
return ready Socket
```

任意阶段失败：

```text
close temporary fd
return Error
```

不得返回半初始化对象。

---

### 3.5 Non-blocking

V1 Socket 固定为 non-blocking。

不提供：

```cpp
bool non_blocking;
```

配置项。

原因：

* `receive()` 已定义 empty 状态；
* 等待策略应由调用者控制；
* `poll/epoll` 已足够；
* 减少 blocking/non-blocking 两套行为。

因此：

```text
O_NONBLOCK
```

是 V1 invariant。

---

### 3.6 send()

API：

```cpp
Result<void> send(
    const Frame& frame) noexcept;
```

行为：

```text
validate Frame
    ↓
convert to native can_frame
    ↓
send()
    ↓
update Stats
```

`send()` 成功只表示：

> 当前 Linux SocketCAN TX path 接受了 Frame。

不表示：

```text
赢得 arbitration
已经实际发送到 CAN bus
远端节点收到
远端协议接受
设备已经执行
```

V1 不在 `send()` 内：

```text
retry
sleep
spin
internal queue
```

一次调用只执行一次 bounded send attempt。

---

### 3.7 receive()

API：

```cpp
Result<bool> receive(
    Frame& frame,
    RxInfo& info) noexcept;
```

语义严格定义为：

```text
success + true
    返回一个正常 CAN Frame

success + false
    本次没有正常 CAN Frame 返回

failure
    真正的 socket / I/O failure
```

如果 kernel 返回：

```text
EAGAIN
EWOULDBLOCK
```

必须返回：

```text
success + false
```

而不是 Error。

---

### 3.8 单次 receive 的工作量

每次 `receive()`：

> 最多消费一个 native CAN frame。

不允许：

```text
不断读取 Error Frame，
直到找到一个正常 Frame 才返回
```

原因是需要保证：

```text
bounded work per receive call
```

正常 Frame：

```text
recvmsg()
    ↓
decode
    ↓
capture monotonic timestamp
    ↓
update Stats
    ↓
return true
```

Error Frame：

```text
recvmsg()
    ↓
decode Error Frame
    ↓
update Event / State / Stats
    ↓
return false
```

---

### 3.9 Error Frame

如果 native CAN ID 包含：

```text
CAN_ERR_FLAG
```

不得作为普通 `Frame` 返回。

至少识别：

```text
warning
error passive
bus off
restart/recovery
arbitration lost
controller error
protocol error
RX overflow
```

不能完整识别时：

```cpp
EventType::Unknown
```

不得：

```text
忽略
assert
throw
```

---

### 3.10 Event queue

`Socket` 内部维护一个小型 fixed-capacity Event queue。

要求：

```text
bounded
non-blocking
runtime no allocation
```

如果 queue full：

```text
drop Event
increment dropped_events
```

不得阻塞 CAN RX path。

读取：

```cpp
bool try_pop_event(Event& event) noexcept;
```

---

### 3.11 Bus State

Socket 保存当前：

```cpp
State
```

典型 transition：

```text
warning
    → Warning

error passive
    → Passive

bus off
    → BusOff

restarted / reliable recovery
    → Active
```

如果 kernel 信息不足：

```text
保持原状态
或使用 Unknown
```

不得人为推断不存在的状态。

---

### 3.12 Error Frame subscription

如果：

```cpp
Options::error_frames == true
```

则通过：

```text
CAN_RAW_ERR_FILTER
```

订阅 V1 所需 Error Frame。

默认：

```cpp
true
```

如果为 false：

> Socket 不保证提供 Event / State 的完整错误诊断。

---

### 3.13 receive own messages

```cpp
bool receive_own{false};
```

映射：

```text
CAN_RAW_RECV_OWN_MSGS
```

默认：

```text
false
```

避免发送 socket 默认把自己的 frame 重新作为 RX frame 返回。

不额外抽象 SocketCAN 完整 loopback 配置。

---

### 3.14 fd()

```cpp
int fd() const noexcept;
```

用于：

```text
poll
epoll
external event loop
custom scheduler
diagnostic tool
```

模块明确 Linux-only，因此 `fd()` 比：

```text
native_handle()
```

更直接。

`Interface` 不暴露 fd。

---

### 3.15 多 Socket

同一 interface 必须允许：

```cpp
auto control = can::Socket::open({
    .interface = "can0",
});

auto diagnostics = can::Socket::open({
    .interface = "can0",
});
```

不同 Socket 可以拥有不同：

```text
filters
error-frame mask
receive-own setting
```

禁止：

```text
global CanManager
interface opened registry
"can0 already opened"
Singleton
```

---

## 4. 错误、诊断与并发

### 4.1 Error 与 empty receive

必须严格区分：

```text
无数据
错误
```

因此：

```text
EAGAIN / EWOULDBLOCK
    → receive() == false

EBADF / ENETDOWN / actual recv failure
    → Error
```

native errno 必须保留。

---

### 4.2 Error 与 Event

`Error`：

> 本地 API / syscall 执行失败。

`Event`：

> CAN bus/controller 报告的运行时诊断事实。

例如：

```text
recvmsg() fails
    → Error

CAN_ERR_BUSOFF received
    → EventType::BusOff
```

两者不得混用。

---

### 4.3 Stats

`Stats` 是轻量 snapshot，不是 metrics subsystem。

CAN 模块不实现：

```text
Prometheus
CSV
ROS diagnostics
InfluxDB
logging backend
tracing
```

上层可以读取：

```cpp
socket.stats()
```

自行发布。

---

### 4.4 Thread ownership

`Socket` 不承诺任意并发访问安全。

V1 contract：

```text
receive()
    一个 RX owner

send()
    一个 TX owner
    或调用者自行序列化

try_pop_event()
    一个 Event consumer

state()
stats()
fd()
    observational access
```

典型合法模型：

```text
RX thread
    → receive()

TX thread
    → send()
```

V1 不保证：

```text
multiple concurrent receive()
multiple event consumers
multiple unsynchronized TX writers
destruction concurrent with active I/O
```

---

### 4.5 Realtime 边界

non-blocking 不等于 hard realtime。

CAN 模块能够保证的是：

```text
no hidden worker
no hidden retry
no hidden sleep
no unbounded Error Frame drain
bounded Event queue
no Event-path blocking
```

但不能保证：

```text
recvmsg WCET
send WCET
CAN arbitration latency
driver latency
physical bus latency
500 Hz deadline
1 kHz deadline
```

是否在：

```cpp
realtime::PeriodicTask
```

中直接调用：

```cpp
socket.send()
socket.receive()
```

必须根据目标平台 benchmark 决定。

---

### 4.6 Memory allocation

允许 setup 阶段分配：

```text
Socket::open()

std::string
std::vector<Filter>
internal setup storage
```

但 runtime：

```text
send()
receive()
try_pop_event()
```

不得主动执行：

```text
new
vector growth
string creation
unbounded allocation
```

Event storage 必须在 open/setup 阶段准备。

---

### 4.7 Stats synchronization

`stats()` 不得为了获取完全一致 snapshot 而给：

```text
send()
receive()
```

增加 blocking mutex。

允许使用：

```text
atomic counters
lightweight snapshot
```

诊断数据允许是非事务性 snapshot。

I/O path 简洁性优先。

---

## 5. 实现约束

### 5.1 推荐代码结构

```text
can/
├── CMakeLists.txt
├── README.md
├── error.hpp
├── event.hpp
├── filter.hpp
├── frame.hpp
├── interface.hpp
├── socket.hpp
├── timestamp.hpp
├── event.cpp
├── socket.cpp
├── timestamp.cpp
├── tests/
└── benchmarks/
```

保持扁平。

不要新增：

```text
detail/
internal/
backend/
platform/
manager/
runtime/
transport/
```

少量内部 helper 放 `.cpp` anonymous namespace。

---

### 5.2 Public API 基线

V1 public API 应接近：

```cpp
namespace can {

enum class FrameFormat : std::uint8_t {
    Standard,
    Extended,
};

enum class FrameType : std::uint8_t {
    Data,
    Remote,
};

struct Frame {
    std::uint32_t id{0};
    std::uint8_t size{0};
    FrameFormat format{FrameFormat::Standard};
    FrameType type{FrameType::Data};
    std::array<std::byte, 8> data{};
};

struct Filter {
    std::uint32_t id{0};
    std::uint32_t mask{0};
    FrameFormat format{FrameFormat::Standard};
};

using Timestamp =
    std::chrono::time_point<
        std::chrono::steady_clock,
        std::chrono::nanoseconds>;

struct RxInfo {
    Timestamp received_at{};
};

enum class State : std::uint8_t {
    Unknown,
    Active,
    Warning,
    Passive,
    BusOff,
};

enum class EventType : std::uint8_t {
    Warning,
    Passive,
    BusOff,
    Restarted,
    ArbitrationLost,
    ControllerError,
    ProtocolError,
    RxOverflow,
    Unknown,
};

struct Event {
    EventType type{EventType::Unknown};
    Timestamp timestamp{};
    std::uint32_t detail{0};
};

struct Stats {
    std::uint64_t rx_frames{0};
    std::uint64_t tx_frames{0};
    std::uint64_t rx_errors{0};
    std::uint64_t tx_errors{0};
    std::uint64_t error_frames{0};
    std::uint64_t rx_overruns{0};
    std::uint64_t dropped_events{0};
};

enum class ErrorCode {
    InvalidArgument,
    InvalidFrame,
    InvalidState,
    OpenFailed,
    IoFailed,
};

struct Error {
    ErrorCode code{ErrorCode::InvalidState};
    int native_code{0};
};

class Interface {
public:
    virtual ~Interface() = default;

    virtual Result<void> send(
        const Frame&) noexcept = 0;

    virtual Result<bool> receive(
        Frame&,
        RxInfo&) noexcept = 0;

    virtual bool try_pop_event(
        Event&) noexcept = 0;
};

class Socket final : public Interface {
public:
    struct Options {
        std::string interface;
        bool error_frames{true};
        bool receive_own{false};
        std::vector<Filter> filters;
    };

    static Result<Socket> open(Options);

    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;

    Result<void> send(
        const Frame&) noexcept override;

    Result<bool> receive(
        Frame&,
        RxInfo&) noexcept override;

    bool try_pop_event(
        Event&) noexcept override;

    State state() const noexcept;
    Stats stats() const noexcept;

    int fd() const noexcept;
};

}
```

新增 public API 前必须证明：

> V1 已有真实 caller 需要该能力。

---

### 5.3 必须保持的实现不变量

#### Frame

* Linux CAN flags 不进入 public API。
* Standard / Extended ID 范围正确。
* Data / Remote 转换正确。
* Error Frame 不进入正常 Frame path。

#### Socket

* `Socket` move-only。
* `Socket::open()` 返回 fully initialized object。
* Socket 固定 non-blocking。
* 无 background worker。
* 无 hidden retry。
* 无 hidden sleep。

#### receive

* EAGAIN 不是 Error。
* 单次最多消费一个 native frame。
* Error Frame 处理之后本次返回 false。
* 不循环 drain Error Frame。

#### Filter

* Standard / Extended matching 正确。
* EFF flag 必须参与 native mask。
* Filter 只在 open 阶段配置。
* V1 无 runtime filter mutation。

#### Event

* Event queue bounded。
* queue full 不阻塞 RX。
* queue full 增加 dropped counter。
* Unknown error frame 可安全表达。

#### Timestamp

* Public Timestamp 必须 monotonic。
* 不得把 realtime clock timestamp 直接 cast 为 steady clock。
* V1 不声明 hardware RX timestamp。

#### Runtime

* send / receive / event path 不主动 heap allocate。
* Stats 不给 I/O path 引入 blocking mutex。
* 一个 CAN interface 允许多个 Socket。

---

### 5.4 明确不实现

V1 不增加：

```text
CAN FD

dynamic filter update

hardware RX timestamp
kernel RX timestamp API
TX timestamp

SO_TXTIME

CAN BCM
J1939

netlink configuration

automatic bus restart

vendor backend

CAN Manager
Runtime
Executor
Worker Thread

logging
tracing
metrics framework
```

---

## 6. 测试与验收

测试分为：

```text
Unit
vcan Integration
Physical CAN
Performance
```

---

### 6.1 Unit Tests

#### Frame

覆盖：

```text
Standard min/max ID
Extended min/max ID

invalid Standard ID
invalid Extended ID

size = 0
size = 8
size > 8

Data
Remote

native encode/decode
EFF
RTR
```

#### Filter

必须覆盖：

```text
Standard exact filter
Extended exact filter
masked filter

Standard accepts Standard
Standard rejects Extended with same low 11 bits

Extended accepts Extended
Extended rejects Standard

invalid id
invalid mask
```

特别防止：

> native filter 忘记将 `CAN_EFF_FLAG` 加入 mask。

#### Event

覆盖：

```text
Warning
Passive
BusOff
Restarted
ArbitrationLost
ControllerError
ProtocolError
Unknown
```

验证：

```text
Event type
State transition
Stats
```

#### Timestamp

验证：

```text
Timestamp 使用 monotonic type
连续 receive timestamp 不倒退
不存在 realtime → steady_clock 直接转换
```

---

### 6.2 vcan Integration

使用：

```text
vcan0
```

至少验证：

```text
Socket open

Standard send / receive
Extended send / receive
Remote Frame

non-blocking empty receive

exact filter
masked filter

Standard filter rejects Extended
Extended filter rejects Standard

receive own off
receive own on

multiple sockets on same vcan

fd() + poll

Stats

high-volume RX/TX
```

必须存在多个 Socket 同时绑定：

```text
vcan0
```

的测试，确保没有错误的全局 interface ownership。

---

### 6.3 Physical CAN

V1 冻结前必须使用真实 CAN controller 验证。

至少覆盖：

```text
normal traffic
high traffic

multiple nodes

node disconnect
bus errors

error passive
bus off
recovery

RX pressure
TX pressure
```

可配合：

```text
can-utils
candump
cansend
cangen
canbusload
ip -details link show
```

验证。

`vcan` 无法替代 physical CAN 对：

```text
arbitration
electrical errors
controller states
bus off
driver/hardware queue
```

的验证。

---

### 6.4 Performance

Benchmark 至少关注：

```text
RX frames/s
TX frames/s

send syscall latency

recvmsg
+ decode
+ timestamp
+ Stats

Error Frame handling cost

Event queue overflow behavior

CPU usage
```

测试：

```text
light load
representative load
high load
near saturation
```

Benchmark 的目的不是证明：

> `send()` 某个 wrapper 有多快。

而是确认：

> CAN 模块没有成为完整 SocketCAN I/O path 的主要软件瓶颈。

只有真实 benchmark 证明某个公共抽象存在明显成本时，才允许为了性能修改 API。

---

### 6.5 V1 完成条件

只有同时满足以下条件，CAN V1 才可以冻结。

#### API

* Frame / Filter / Socket / Interface 语义稳定；
* 无不必要 Config / Manager / Runtime；
* Interface 保持最小；
* Socket 无半初始化状态。

#### Correctness

* Standard / Extended 转换正确；
* RTR 正确；
* Filter EFF matching 正确；
* Error Frame 与正常 Frame 分离；
* Timestamp clock domain 正确；
* EAGAIN 不作为 Error。

#### Runtime

* 无隐藏线程；
* 无隐藏 retry；
* 无隐藏 sleep；
* 单次 receive 工作 bounded；
* Event queue bounded；
* runtime I/O 不主动分配内存。

#### Linux

* 多 Socket 可共享同一 CAN interface；
* Socket filter 独立；
* receive-own 行为正确；
* native errno 保留。

#### Validation

* unit tests 通过；
* vcan integration 通过；
* physical CAN 验证通过；
* representative benchmark 完成；
* compiler warnings 为零。

---

## 最终边界

模块最终依赖关系：

```text
CANopen / Custom Protocol
          │
          ▼
     can::Interface
          │
          ▼
       can::Socket
          │
          ▼
   Linux SocketCAN
```

`can` 与 `realtime` 相互独立：

```text
can
    不依赖 realtime

realtime
    不依赖 can
```

上层可以自由组合：

```cpp
realtime::PeriodicTask
can::Socket
```

但两者都保持独立工具模块定位。

CAN V1 最终希望使用者只需要理解：

```text
Frame
Filter
Interface
Socket
RxInfo
Event
State
Stats
```

即可完成基础 CAN 通信。

如果需要理解：

```text
Manager
Runtime
Transport
Backend
Context
Session
Dispatcher
```

才能使用 CAN，那么模块已经设计过重。

如果为了减少类型数量又把：

```text
Standard / Extended
Data / Remote
Frame / Event
```

这些真实 CAN 领域概念退化成 bool、flag 或隐式约定，那么模块又被过度简化。

因此最终原则是：

> **减少无价值抽象，不减少实现正确性所需的领域语义；减少重复文档，不减少 Codex 实现所需的行为约束。**
