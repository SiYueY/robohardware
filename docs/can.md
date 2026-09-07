# RoboHardware CAN 模块设计

## 1. 模块定位与设计原则

`can` 是 RoboHardware 中面向 Linux SocketCAN 的通用 C++17 CAN 基础模块。

它提供：

```text
CAN Frame
CAN Interface
SocketCAN backend
Filter
Non-blocking RX / TX
RX Timestamp
Bus Event
Bus State
Status / Stats
Error
```

它不负责：

```text
CANopen
CiA301
CiA402

NMT
SDO
PDO
Heartbeat
EMCY
SYNC

Motor semantics
Robot Safety
System Lifecycle
ROS2
```

模块目标是：

> 提供一个轻量、明确、可独立复用的 Linux CAN 基础层，使 CANopen 或其他 CAN 协议能够建立在稳定的 `can::Interface` 之上，而不直接依赖 Linux SocketCAN API。

### 平台范围

V1 明确：

```text
Language:
    C++17

Platform:
    Linux only

Backend:
    Linux SocketCAN

Protocol:
    Classic CAN first

Future:
    CAN FD
```

V1 不承诺：

```text
Windows CAN API
vendor-specific CAN SDK
cross-platform CAN abstraction
CANopen FD
```

CAN 模块可以直接使用：

```text
socket
bind
setsockopt
recvmsg
send
poll / epoll
fcntl
ioctl
SocketCAN
```

等 Linux 接口，不为了理论跨平台提前建设 backend framework。

### 核心原则

1. CAN 模块只表达 CAN 总线、Frame 和总线运行事实。
2. CAN 不认识 CANopen、CiA402 或机器人设备。
3. `can::Interface` 是上层协议依赖的稳定边界。
4. Linux `struct can_frame` 和 kernel flag 不泄漏到协议层。
5. Classic CAN 与 CAN FD 使用不同类型表达。
6. RX/TX 默认面向 non-blocking 使用方式。
7. Error Frame 不得静默丢弃。
8. 正常 CAN Frame 与 Bus Event 使用不同语义通路。
9. RX timestamp 必须属于 monotonic control-time compatible domain。
10. `send()` 成功只表达本地 TX path 接受，不表示远端执行成功。
11. 一个 Linux CAN interface 可以被多个 socket 同时绑定。
12. CAN 模块不引入全局 Bus Manager 或 Singleton。
13. 线程 ownership 必须明确，不默认任意多线程安全。
14. Status / Stats 只表达 CAN 层事实。
15. 性能优化必须以完整 SocketCAN RX/TX benchmark 为依据。

主要参考：

```text
Linux SocketCAN
    CAN RAW socket semantics
    filters
    error frames
    timestamps
    multiple sockets
    CAN FD extension

ros-industrial/ros_canopen
    socketcan_interface
    CAN abstraction boundary

CANopenNode
    CAN driver abstraction
    frame / filter concepts

can-utils
    integration testing
    traffic generation
    bus diagnostics
```

---

## 2. 模块结构与核心接口

推荐目录：

```text
include/can/
├── frame.hpp
├── interface.hpp
├── socketcan.hpp
├── filter.hpp
├── timestamp.hpp
├── event.hpp
├── status.hpp
├── statistics.hpp
└── error.hpp

src/can/
├── socketcan.cpp
├── event.cpp
├── status.cpp
├── statistics.cpp
└── error.cpp
```

V1 不增加：

```text
manager.hpp
bus.hpp
transport.hpp
executor.hpp
session.hpp
protocol.hpp
factory.hpp
```

namespace：

```cpp
namespace can {}
```

核心关系：

```text
             CANopen / Custom Protocol
                       │
                       ▼
                can::Interface
                       │
                       ▼
                can::SocketCan
                       │
        ┌──────────────┼───────────────┐
        ▼              ▼               ▼
      Frame         BusEvent       Status/Stats
                       │
                       ▼
                Linux SocketCAN
                       │
                       ▼
                    CAN Bus
```

建议公共类型：

```cpp
namespace can {

struct Frame;
struct Filter;

struct MonotonicTimestamp;
struct RxMetadata;

struct BusEvent;

struct SocketCanConfig;
struct Status;
struct Stats;

class Interface;
class SocketCan;

enum class ErrorCode;
struct Error;

}
```

---

## 3. Frame、Interface 与 SocketCAN

### 3.1 Classic CAN Frame

V1 只定义 Classic CAN：

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

语义：

```text
Standard:
    11-bit CAN ID

Extended:
    29-bit CAN ID

Data:
    normal data frame

Remote:
    RTR frame
```

公共 `Frame` 不暴露：

```text
CAN_EFF_FLAG
CAN_RTR_FLAG
CAN_ERR_FLAG
struct can_frame
```

这些只存在于 SocketCAN backend。

---

### 3.2 Classic CAN 与 CAN FD

V1 不采用：

```cpp
std::array<std::byte, 64>
```

统一覆盖 Classic CAN / CAN FD。

未来 CAN FD 使用独立类型：

```cpp
struct FdFrame {
    std::uint32_t id;
    std::uint8_t size;

    FrameFormat format;

    std::array<std::byte, 64> data;
};
```

CAN FD 的：

```text
BRS
ESI
payload length semantics
socket option
```

以后单独设计。

原则：

> 不为了未来 CAN FD 提前复杂化 Classic CAN V1 API。

---

### 3.3 Frame Validation

发送前必须验证：

```text
Standard:
    id <= 0x7FF

Extended:
    id <= 0x1FFFFFFF

Classic CAN:
    size <= 8
```

非法 Frame：

```text
reject before kernel send
    ↓
ErrorCode::InvalidFrame
```

CAN 模块不验证：

```text
COB-ID
PDO size
CANopen object
device-specific payload
```

这些属于上层协议。

---

### 3.4 Interface

`can::Interface` 是协议层依赖的核心边界。

建议：

```cpp
class Interface {
public:
    virtual ~Interface() = default;

    virtual Result<void> open() = 0;
    virtual Result<void> close() = 0;

    virtual Result<void> send(
        const Frame& frame) noexcept = 0;

    virtual Result<bool> receive(
        Frame& frame,
        RxMetadata& metadata) noexcept = 0;

    virtual bool try_pop_event(
        BusEvent& event) noexcept = 0;

    virtual Status status() const noexcept = 0;
    virtual Stats stats() const noexcept = 0;
};
```

Interface 只表达：

```text
lifecycle
Frame TX
Frame RX
Bus Event
Status
Stats
```

不表达：

```text
CANopen node
callback registry
protocol dispatcher
thread pool
bus manager
```

---

### 3.5 receive() Contract

V1 保持：

```cpp
Result<bool> receive(
    Frame& frame,
    RxMetadata& metadata) noexcept;
```

语义：

```text
success + true
    返回一个正常 Data / Remote Frame

success + false
    当前没有可返回的正常 Frame

failure
    发生真正的 socket / I/O failure
```

因此：

```text
No Data
```

不是错误。

V1 暂不因为高频场景提前改成：

```cpp
enum class ReceiveStatus {
    Ok,
    Empty,
    Error,
};
```

如果 benchmark 确认 `Result<bool>` 在完整 RX path 中形成可测瓶颈，再进行 API 优化。

优化判断必须基于：

```text
recvmsg
timestamp ancillary parsing
frame conversion
filtering
dispatch
```

整个路径，而不是仅基于返回值大小猜测。

---

### 3.6 Error Frame 与 BusEvent

Linux CAN Error Frame 不作为普通 `Frame` 暴露。

原因：

```text
normal CAN Frame
    → protocol input

CAN Error Frame
    → bus diagnostic fact
```

二者语义不同。

V1 定义：

```cpp
enum class BusEventType : std::uint8_t {
    ErrorWarning,
    ErrorPassive,
    BusOff,
    Restarted,

    ArbitrationLost,
    ControllerError,
    ProtocolError,
    RxOverflow,

    Unknown,
};

struct BusEvent {
    BusEventType type{BusEventType::Unknown};

    MonotonicTimestamp timestamp;

    std::uint32_t detail{0};
    int native_code{0};
};
```

SocketCan 收到 kernel Error Frame 时：

```text
recvmsg()
    ↓
detect CAN_ERR_FLAG
    ↓
decode BusEvent
    ↓
update Status
    ↓
update Stats
    ↓
push to bounded internal BusEvent queue
```

上层通过：

```cpp
bool try_pop_event(
    BusEvent& event) noexcept;
```

获取事件。

因此 CAN 层对外有三类不同信息：

```text
Frame
    protocol data

BusEvent
    discrete bus diagnostic event

Status / Stats
    aggregated bus facts
```

三者不能互相替代。

---

### 3.7 BusEvent Queue

内部 BusEvent delivery 必须：

```text
bounded
non-blocking
no hidden dynamic allocation in runtime
```

事件队列满时：

```text
不能阻塞 RX path
```

因此应：

```text
increment dropped_event counter
update Stats
```

而不是阻塞接收线程。

BusEvent queue 是 CAN 模块内部诊断通路，不建设通用 EventBus。

---

### 3.8 Error Frame 接收行为

`receive(Frame...)` 只向上返回普通 Frame。

如果一次底层读取获得 Error Frame，则：

```text
decode event
    ↓
queue event
    ↓
continue according to bounded receive policy
```

实现不得在一个 `receive()` 调用中形成无界：

```text
while(error_frame)
    recvmsg()
```

循环。

V1 实现应保证：

> 单次 `receive()` 的用户态处理量有明确上限。

具体是：

```text
one native frame per call
```

还是：

```text
small fixed number of native frames per call
```

属于实现细节，但不得无限消费 error frames。

若本次只消费了 BusEvent，没有正常 Frame：

```text
receive() → success + false
```

BusEvent 通过 `try_pop_event()` 获取。

---

### 3.9 SocketCan

Linux backend：

```cpp
class SocketCan final : public Interface {
public:
    explicit SocketCan(
        SocketCanConfig config);

    Result<void> open() override;
    Result<void> close() override;

    Result<void> send(
        const Frame& frame) noexcept override;

    Result<bool> receive(
        Frame& frame,
        RxMetadata& metadata) noexcept override;

    bool try_pop_event(
        BusEvent& event) noexcept override;

    Status status() const noexcept override;
    Stats stats() const noexcept override;

    int native_handle() const noexcept;
};
```

`native_handle()` 是：

> Linux integration escape hatch。

可用于：

```text
poll
epoll
external event loop
diagnostics
```

但上层 CANopen 协议不应依赖 native fd 作为普通接口。

---

## 4. 配置、过滤、时间戳与总线状态

### 4.1 SocketCanConfig

V1：

```cpp
struct SocketCanConfig {
    std::string interface;

    bool non_blocking{true};

    bool receive_error_frames{true};
    bool receive_own_messages{false};

    std::vector<Filter> filters;
};
```

配置只描述 SocketCAN socket。

不负责：

```text
ip link set can0 up
bitrate
sample point
restart-ms
CAN FD enable
hardware firmware
```

这些属于：

```text
deployment
iproute2
systemd-networkd
scripts
```

CAN V1 不成为 Linux network configuration manager。

---

### 4.2 Filter

公共 API：

```cpp
struct Filter {
    std::uint32_t id{0};
    std::uint32_t mask{0};

    FrameFormat format{FrameFormat::Standard};
};
```

Filter 只表达：

```text
CAN ID
ID mask
Standard / Extended
```

不表达 CANopen COB-ID semantics。

---

### 4.3 Filter → SocketCAN 映射

Linux：

```cpp
struct can_filter {
    canid_t can_id;
    canid_t can_mask;
};
```

Standard 与 Extended 必须显式匹配 `CAN_EFF_FLAG`。

#### Standard

逻辑：

```text
expected EFF flag = 0
```

转换：

```cpp
native.can_id =
    filter.id;

native.can_mask =
    filter.mask | CAN_EFF_FLAG;
```

这样 Extended Frame 即使低 11 bit ID 相同，也不会错误匹配 Standard Filter。

#### Extended

逻辑：

```text
expected EFF flag = 1
```

转换：

```cpp
native.can_id =
    filter.id | CAN_EFF_FLAG;

native.can_mask =
    filter.mask | CAN_EFF_FLAG;
```

因此：

```text
CAN_EFF_FLAG in mask
    ↓
frame format participates in filter matching
```

如果未来 Filter 还需要严格区分：

```text
RTR
```

则使用同样原则：

```text
CAN_RTR_FLAG
```

参与 `can_id` / `can_mask` 转换。

kernel flag 转换必须集中到 SocketCAN internal helper，例如：

```cpp
::can_filter to_native_filter(
    const can::Filter&) noexcept;
```

不得让 kernel bit flag 出现在公共 Filter API。

---

### 4.4 Filter Tests

至少覆盖：

```text
Standard filter:
    matches Standard same ID
    rejects Extended same low 11-bit ID

Extended filter:
    matches Extended same ID
    rejects Standard frame

masked filter:
    expected ID-mask matching

invalid Standard ID:
    rejected

invalid Extended ID:
    rejected
```

避免由于 `CAN_EFF_FLAG` mask 错误造成隐蔽 frame 泄漏。

---

### 4.5 Dynamic Filter

V1 MUST：

```text
configure before open/start
```

V1 不要求 runtime 动态替换 filter。

如果 CANopen 上层需要动态节点变化，可以：

```text
kernel wide filter
    ↓
user-space protocol dispatch
```

后续有真实性能需求时，再增加 runtime filter replacement。

---

### 4.6 Monotonic Timestamp

CAN 模块不依赖 `realtime` target。

但所有 CAN control timestamp 必须与 RoboHardware 的 monotonic control-time domain 兼容。

定义：

```cpp
struct MonotonicTimestamp {
    std::int64_t nanoseconds{0};
};
```

语义：

```text
nanoseconds in Linux monotonic time domain
```

RxMetadata：

```cpp
struct RxMetadata {
    MonotonicTimestamp received_at;
};
```

System 可以无损转换为自己的：

```text
realtime::TimePoint
```

而无需 CAN → Realtime 依赖。

V1 不为了共享 timestamp 创建：

```text
common/
core/
time/
```

模块。

---

### 4.7 RX Timestamp Source

V1 RX timestamp 是 MUST。

优先读取 Linux socket ancillary timestamp：

```text
recvmsg()
    ↓
control message
    ↓
SO_TIMESTAMPNS / SO_TIMESTAMPING
```

而不是简单：

```text
recvmsg()
    ↓
Clock::now()
```

后者只能表示：

```text
userspace handling time
```

无法准确表达 kernel receive time。

实现必须明确 timestamp 的 clock domain。

禁止把：

```text
CLOCK_REALTIME timestamp
```

直接假装成 monotonic control timestamp。

如果特定 timestamping 模式无法提供兼容 monotonic domain，应：

```text
明确转换
或
标记能力不可用
```

而不是静默混用时钟。

---

### 4.8 TX Timestamp

V1：

```text
RX timestamp
    MUST

TX kernel/hardware timestamp
    MAY
```

V1 可以记录：

```text
userspace send attempt
send syscall completion
```

用于基本性能分析，但必须明确它们不是：

```text
actual bus transmission timestamp
```

如果未来引入：

```text
SO_TIMESTAMPING
hardware TX timestamp
SO_TXTIME
```

单独设计。

---

### 4.9 BusState

定义：

```cpp
enum class BusState {
    Unknown,
    Active,
    Warning,
    Passive,
    BusOff,
};
```

Status：

```cpp
struct Status {
    bool open{false};
    bool non_blocking{false};

    bool error_frames_enabled{false};
    bool timestamping_enabled{false};

    BusState bus_state{BusState::Unknown};
};
```

BusState 来源可以包括：

```text
SocketCAN error frame
netlink/controller state
other available kernel facts
```

但如果无法可靠判断：

```text
Unknown
```

优于错误推断。

---

## 5. I/O、并发、错误与可观测性

### 5.1 Non-blocking I/O

V1 默认：

```text
non-blocking = true
```

底层使用：

```text
O_NONBLOCK
```

上层可以根据需要组合：

```text
poll
epoll
dedicated RX worker
external event loop
```

CAN module 不隐藏：

```text
blocking receive thread
retry thread
async executor
```

---

### 5.2 Non-blocking 不等于 Realtime-safe

必须明确：

> `O_NONBLOCK` 只表示 syscall 不等待数据，不代表该 syscall 本身具备 hard realtime guarantee。

`recvmsg()` / `send()` 仍然涉及：

```text
kernel
network stack
driver
CAN controller
```

是否直接进入 500 Hz RT callback 必须由上层架构和 benchmark 决定。

推荐总体结构：

```text
CAN RX worker
      ↓
protocol decode
      ↓
RT-safe snapshot

RT control
      ↓
command snapshot
      ↓
CAN TX path
```

CAN 模块本身只提供：

```text
non-blocking primitives
no hidden sleep
no hidden retry loop
```

---

### 5.3 Thread Ownership

V1 不承诺 `SocketCan` 任意线程安全。

推荐 contract：

```text
open / close / configure
    externally serialized

receive()
    one RX owner

send()
    one TX owner
    or externally serialized

try_pop_event()
    one event consumer
```

允许典型：

```text
RX thread
    receive()

TX thread
    send()
```

但不默认支持：

```text
multiple concurrent receive callers
multiple concurrent config mutations
close while send/receive still active
```

生命周期由 System / upper layer 协调。

---

### 5.4 Multiple SocketCAN Sockets

Linux SocketCAN 允许：

```text
Socket A ─┐
Socket B ─┼→ can0
Socket C ─┘
```

每个 socket 可以：

```text
own filters
own receive-own-message setting
own error mask
```

因此禁止：

```text
global CanManager singleton
one interface can only be opened once
```

这样的错误假设。

可以合法：

```cpp
can::SocketCan can0_rx(...);
can::SocketCan can0_diag(...);
can::SocketCan can1(...);
```

但 CANopen Network 通常更适合共享一个 Interface：

```cpp
auto bus =
    std::make_shared<can::SocketCan>(...);

canopen::Network network(bus);
```

这是上层 ownership 决策，不是 CAN 模块强制限制。

---

### 5.5 send() 成功的严格语义

```cpp
send(frame)
```

成功只表示：

> frame 被本地主机 SocketCAN/kernel TX path 接受。

不保证：

```text
完成 arbitration
实际出现在 CAN bus
远端节点收到
远端协议接受
驱动器执行命令
```

因此：

```text
send success
    ≠
device command confirmed
```

闭环确认必须由上层使用：

```text
feedback
protocol state
device status
watchdog
```

完成。

---

### 5.6 Error

V1：

```cpp
enum class ErrorCode {
    InvalidArgument,
    InvalidFrame,
    InvalidState,

    InterfaceNotFound,

    OpenFailed,
    BindFailed,
    ConfigureFailed,

    SendFailed,
    ReceiveFailed,

    TimestampFailed,
    FilterFailed,
};
```

Error：

```cpp
struct Error {
    ErrorCode code;
    int native_code{0};
};
```

CAN V1 基本都使用 Linux syscall API，因此：

```text
native_code = errno
```

即可。

`EAGAIN` / `EWOULDBLOCK` 在 non-blocking `receive()` 中：

```text
不是 Error
→ success + false
```

CAN 的 Error 不负责转换成：

```text
SystemFault
CommunicationFault
SafetyAction
```

这些属于 System。

---

### 5.7 Stats

定义：

```cpp
struct Stats {
    std::uint64_t rx_frames{0};
    std::uint64_t tx_frames{0};

    std::uint64_t rx_bytes{0};
    std::uint64_t tx_bytes{0};

    std::uint64_t rx_errors{0};
    std::uint64_t tx_errors{0};

    std::uint64_t error_frames{0};
    std::uint64_t dropped_events{0};

    std::uint64_t rx_overruns{0};

    std::uint64_t bus_off_events{0};
};
```

只统计 CAN 层事实。

不统计：

```text
PDO missing
SDO abort
Heartbeat timeout
CiA402 fault
```

---

### 5.8 Observability

CAN 模块只暴露：

```text
Status
Stats
BusEvent
Error
```

不实现：

```text
spdlog integration
CSV
Prometheus
ROS publisher
InfluxDB
```

高频：

```text
RX log
TX log
```

默认关闭。

诊断功能不能显著改变正常 CAN timing。

---

## 6. V1 范围、测试与架构不变量

### V1 MUST

必须实现：

```text
Classic CAN Frame

Standard / Extended ID

Data / Remote Frame

Frame validation

Filter

Interface abstraction

SocketCan backend

open / close

non-blocking send / receive

RX timestamp

BusEvent

CAN error-frame decoding

bounded event delivery

BusState

Status

Stats

structured Error

native fd escape hatch

vcan integration tests

physical CAN validation
```

### SHOULD

建议完成：

```text
RX overflow accounting

additional socket diagnostics

poll/epoll example

bus-off test

physical CAN stress benchmark

TX software timing metrics
```

### MAY

按真实需求增加：

```text
CAN FD

kernel / hardware TX timestamp

runtime filter replacement

SO_TXTIME

CAN BCM

CAN J1939

netlink CAN configuration

automatic bus restart

hardware-specific CAN backend
```

不提前建设：

```text
CanManager Singleton
Generic Transport
Protocol Registry
Bus Framework
Async Runtime
Callback Dispatcher Framework
Driver Factory
```

---

### Unit Tests

覆盖：

```text
Frame validation

Standard ID
Extended ID

Frame encode/decode

Data / RTR conversion

Filter conversion

Standard/Extended EFF matching

timestamp conversion

Error mapping

BusEvent decode

Status / Stats
```

---

### vcan Integration

使用：

```text
vcan0
```

验证：

```text
open / close

send / receive

non-blocking empty receive

Standard Frame
Extended Frame
RTR Frame

kernel filter

Standard filter rejects Extended same low ID
Extended filter rejects Standard

receive own messages

multiple SocketCan sockets on same vcan

timestamp

error/event path where vcan allows simulation

high-volume traffic
```

尤其验证：

> CAN 模块绝不能实现“一个 interface 只能打开一次”的错误 ownership 规则。

---

### Physical CAN Validation

vcan 无法证明：

```text
arbitration
electrical error
error passive
bus-off
controller queues
physical bus saturation
hardware timestamp
```

所以必须尽早用真实 CAN controller 验证：

```text
normal traffic

high load

near saturation

TX queue pressure

RX overflow

node disconnect

bus errors

bus-off

recovery behavior
```

工具：

```text
can-utils
candump
cansend
cangen
canbusload

ip -details link show
```

---

### Performance

至少测量：

```text
RX throughput
TX throughput

send syscall latency

recvmsg handling latency

kernel RX timestamp
    →
userspace processing latency

error-frame processing cost

event queue overflow

CPU usage under high load
```

对于当前：

```text
7 joints
500 Hz
Classic CAN
```

若粗略：

```text
1 command frame / joint / cycle
1 feedback frame / joint / cycle
```

则：

```text
7 × 2 × 500
=
7000 frames/s
```

这对 1 Mbps Classic CAN 已经是非常高的负载。

因此 CAN 层必须提供：

```text
Stats
Timestamp
BusEvent
Overflow information
```

支持上层评估总线运行状态。

但：

```text
PDO packing
signal frequency
SYNC scheduling
COB-ID priority
```

都属于 CANopen/System，而不是 CAN 模块。

CAN 自身不承诺：

```text
500 Hz control deadline
```

CAN 模块要证明：

> 在目标总线负载下，SocketCAN I/O 和用户态转换不会成为主要软件瓶颈。

---

### 参考实现

#### Linux SocketCAN

最终事实依据：

```text
CAN_RAW
struct can_frame
CAN_EFF_FLAG
CAN_RTR_FLAG
CAN_ERR_FLAG
CAN_RAW_FILTER
CAN_RAW_ERR_FILTER
timestamps
multiple sockets
```

#### ros_canopen/socketcan_interface

参考：

```text
CAN abstraction boundary
SocketCAN encapsulation
protocol / transport separation
```

不复制：

```text
ROS dependency
legacy framework
```

#### CANopenNode

参考：

```text
CAN driver abstraction
frame model
filter model
```

但 CAN 模块不依赖 CANopenNode。

#### can-utils

用于：

```text
integration test
traffic generation
bus diagnostics
physical validation
```

---

### 架构不变量

1. `can` 不依赖 CANopen、CiA402、System、ROS2。
2. V1 只面向 Linux SocketCAN。
3. V1 以 Classic CAN 为第一目标。
4. Classic CAN 与 CAN FD 必须使用独立 Frame 类型。
5. 公共 Frame 不暴露 Linux `struct can_frame`。
6. Linux `CAN_EFF_FLAG`、`CAN_RTR_FLAG`、`CAN_ERR_FLAG` 不泄漏到上层协议。
7. `can::Interface` 是上层协议依赖的稳定边界。
8. `SocketCan` 只负责 Linux SocketCAN backend。
9. CAN 模块不解析 CANopen COB-ID 或协议 payload。
10. Frame 必须在进入 kernel TX 前完成 CAN 层合法性验证。
11. `receive()` 必须区分“当前无数据”和真正 I/O failure。
12. 普通 Frame 与 Error Frame 必须使用不同语义通路。
13. Error Frame 必须转换为 `BusEvent`，并同步更新 Status/Stats。
14. BusEvent 不得被静默丢弃；发生内部事件队列溢出时必须计数。
15. V1 默认使用 non-blocking SocketCAN。
16. non-blocking syscall 不等价于 hard realtime-safe。
17. RX timestamp 必须具有明确 monotonic time-domain 语义。
18. wall-clock timestamp 不得混入 control timing。
19. Standard / Extended Filter 必须显式使用 `CAN_EFF_FLAG` 参与 kernel mask 匹配。
20. Filter API 不表达 CANopen semantics。
21. BusState 无法确认时必须报告 `Unknown`，不得推测。
22. `send()` 成功不代表物理发送成功，更不代表远端设备执行成功。
23. CAN 模块不决定机器人 Safety 行为。
24. open / close / configure 的生命周期必须由上层序列化。
25. V1 `receive()` 使用单 RX owner。
26. V1 `send()` 使用单 TX owner或由上层显式序列化。
27. 一个 Linux CAN interface 可以同时存在多个 SocketCAN socket。
28. 禁止全局 `CanManager::instance()`。
29. CANopen Network 是否共享单个 Interface 属于 CANopen/System ownership 策略。
30. Status / Stats 只记录 CAN 层事实。
31. CAN 层不记录 PDO、Heartbeat、SDO、CiA402 等协议状态。
32. `Result<bool>` 是否优化必须由完整 RX benchmark 决定。
33. vcan 用于软件行为和 SocketCAN 接口验证。
34. Physical CAN 用于 arbitration、error、bus-off、overflow 和负载验证。
35. V1 不为了未来 CAN FD、J1939 或其他协议提前建设通用 Transport Framework。
36. CAN 模块长期保持轻量 CAN infrastructure 定位。

最终模型：

```text
               CANopen / Custom Protocol
                         │
                         ▼
                  can::Interface
                         │
                         ▼
                  can::SocketCan
                         │
        ┌────────────────┼─────────────────┐
        ▼                ▼                 ▼
      Frame           BusEvent         Status/Stats
 protocol data       bus events       aggregate facts
                         │
                         ▼
                  Linux SocketCAN
                         │
                         ▼
                      CAN Bus
```

CAN 模块最终目标是：

> **提供一个 Linux-only、Classic-CAN-first、Frame 与 BusEvent 语义分离、时间和错误边界明确、支持 non-blocking I/O 和总线诊断，并能独立支撑 CANopen 及其他 CAN 协议的 C++17 SocketCAN 基础模块。**

**CAN V1 的架构与公共语义在上述边界内冻结。后续修改应由真实 CAN FD、硬件时间戳、物理总线测试或明确上层需求驱动，而不是为了构建通用总线框架提前增加抽象。**
