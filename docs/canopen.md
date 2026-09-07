# RoboHardware CANopen 模块设计

## 1. 模块定位与设计原则

`canopen` 是 RoboHardware 中建立在 `can::Interface` 之上的 CANopen 协议模块。

V1 主要实现 CiA 301 核心能力：

```text
Network
Node
Object Dictionary
NMT
SDO
PDO
Heartbeat
EMCY
SYNC
Master
Slave
```

并在其下提供：

```text
canopen::cia402
```

用于 CiA 402 Drive Profile。

整体关系：

```text
Application / Device
        │
        ▼
canopen::cia402
        │
        ▼
     CANopen
        │
        ▼
  can::Interface
        │
        ▼
  Linux SocketCAN
```

CANopen 模块负责：

```text
CANopen frame dispatch
COB-ID routing
Object Dictionary
NMT state management
SDO client/server
PDO encode/decode
Heartbeat producer/consumer
EMCY handling
SYNC handling
Master / Slave composition
per-node ProcessImage
protocol Status / Stats / Error
```

它不负责：

```text
SocketCAN implementation
CAN bitrate/interface configuration

Linux realtime scheduling
robot control algorithm
motor physical model
robot safety policy
System lifecycle

ROS2
ros2_control
```

### 核心设计原则

1. CANopen core 与设备 profile 解耦。
2. Master 与 Slave 共享协议组件，不复制两套协议栈。
3. Object Dictionary 属于配置/控制平面。
4. ProcessImage 属于周期运行数据平面。
5. RT hot path 不进行 OD lookup、字符串查询、动态分配或动态 PDO remapping。
6. SDO 禁止进入 RT cyclic hot path。
7. Safety 快速响应不依赖 SDO。
8. PDO mapping 由启动阶段静态配置构建并冻结。
9. Network routing table 在启动阶段构建并冻结。
10. ProcessImage 为 per-node，不建立 CANopen 全局机器人状态。
11. Heartbeat timeout 由本地配置显式指定。
12. SYNC、Heartbeat、timeout、freshness 全部使用 monotonic time domain。
13. CANopen 只表达协议事实，不直接决定机器人 Safety。
14. CiA402 位于 `canopen::cia402`，不得污染 CANopen core。
15. V1 使用静态网络拓扑，不提前引入 EDS/LSS/Plugin Framework。
16. 实现优先参考成熟 CANopen 项目，而不是从零发明协议行为。

主要参考：

```text
CANopenNode
    CiA301 protocol semantics
    OD / NMT / SDO / PDO
    Heartbeat / EMCY / SYNC

Lely Core
    Linux CANopen architecture
    network / node organization
    protocol layering

ros_canopen
    CANopen Master
    CiA402 integration
    PDO / SDO engineering patterns

ros2_canopen
    modern integration
    CiA402 driver
    fake slave / testing
```

---

## 2. 模块结构与核心对象

推荐目录：

```text
include/canopen/
├── network.hpp
├── node.hpp
├── object_dictionary.hpp
├── nmt.hpp
├── sdo.hpp
├── pdo.hpp
├── heartbeat.hpp
├── emcy.hpp
├── sync.hpp
├── process_image.hpp
├── master.hpp
├── slave.hpp
├── status.hpp
├── statistics.hpp
├── error.hpp
└── cia402/
    ├── state.hpp
    ├── master.hpp
    ├── slave.hpp
    ├── process_data.hpp
    └── status.hpp

src/canopen/
├── network.cpp
├── node.cpp
├── object_dictionary.cpp
├── nmt.cpp
├── sdo.cpp
├── pdo.cpp
├── heartbeat.cpp
├── emcy.cpp
├── sync.cpp
├── process_image.cpp
├── master.cpp
├── slave.cpp
├── status.cpp
├── statistics.cpp
├── error.cpp
└── cia402/
    ├── master.cpp
    ├── slave.cpp
    └── status.cpp
```

V1 不增加：

```text
protocol/
transport/
runtime/
executor/
registry/
plugin/
device_factory/
eds_runtime/
```

namespace：

```cpp
namespace canopen {}
namespace canopen::cia402 {}
```

核心关系：

```text
                       Network
                         │
            ┌────────────┴────────────┐
            ▼                         ▼
          Master                    Slave
            │                         │
            └────────────┬────────────┘
                         ▼
                        Node
                         │
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
         NMT            SDO            PDO
          │                              │
     Heartbeat                           ▼
       EMCY                        ProcessImage
       SYNC
                         │
                         ▼
                 ObjectDictionary
```

Master / Slave 是协议能力的组合者，而不是两套完全独立的实现。

---

## 3. Network、Node 与运行时路由

### 3.1 Network

`Network` 表示一条 CANopen 网络的协议上下文。

推荐：

```cpp
class Network {
public:
    explicit Network(
        std::shared_ptr<can::Interface> interface);

    Result<void> initialize();
    Result<void> start();
    Result<void> stop();

    Status status() const noexcept;
    Stats stats() const noexcept;
};
```

Network 负责：

```text
CAN frame dispatch
COB-ID routing
Node registration
shared Interface ownership
protocol Status/Stats aggregation
BusEvent propagation to protocol/system
```

Network 不负责：

```text
CAN bitrate
SocketCAN lifecycle policy
System Safety
robot lifecycle
```

推荐一个 CANopen Network 共享一个：

```text
can::Interface
```

例如：

```cpp
auto bus =
    std::make_shared<can::SocketCan>(config);

canopen::Network network(bus);
```

这是 CANopen ownership 策略，不改变 CAN 模块允许多个 socket 绑定同一 `can0` 的事实。

---

### 3.2 Node

Node-ID：

```text
1..127
```

配置：

```cpp
struct NodeConfig {
    std::uint8_t node_id;

    std::vector<PdoConfig> rpdos;
    std::vector<PdoConfig> tpdos;

    std::optional<HeartbeatConsumerConfig> heartbeat;
};
```

Node 关联：

```text
Node-ID
NMT
SDO endpoint
PDO configuration
Heartbeat state
EMCY state
SYNC state
Object Dictionary
ProcessImage
Status
```

Node 不包含：

```text
joint semantics
gear ratio
motor model
robot safety state
```

---

### 3.3 静态拓扑

V1 使用静态网络配置：

```text
configure
    ↓
create nodes
    ↓
validate Node-ID
    ↓
validate COB-ID uniqueness/conflicts
    ↓
build protocol structures
    ↓
build RouteTable
    ↓
build PdoPlan
    ↓
freeze
    ↓
runtime
```

V1 不实现：

```text
LSS
runtime discovery
dynamic node creation
```

---

### 3.4 Runtime RouteTable

多个 Node 共用一个 Network 时，RX frame 由 Network 进行路由。

启动阶段构建：

```text
Node/PDO/Heartbeat/EMCY/SDO configuration
        ↓
COB-ID registration
        ↓
immutable RouteTable
        ↓
freeze
```

概念：

```cpp
enum class RouteKind {
    Nmt,
    Sdo,
    Rpdo,
    Tpdo,
    Heartbeat,
    Emcy,
    Sync,
};

struct Route {
    RouteKind kind;
    std::uint8_t node_id;
    std::uint8_t index;
};
```

Runtime：

```text
CAN-ID
   ↓
RouteTable lookup
   ↓
protocol endpoint
```

要求：

```text
bounded
deterministic
no allocation
no string lookup
no route mutation
```

V1 不规定必须使用 hash table。

优先考虑：

```text
fixed array
sorted fixed table
```

等简单结构。

对于 Classic CAN 11-bit ID，固定索引表是非常合理的实现。

---

## 4. Object Dictionary 与 ProcessImage

### 4.1 Object Dictionary

OD 是 CANopen 的配置/协议对象模型。

核心 key：

```cpp
struct ObjectKey {
    std::uint16_t index;
    std::uint8_t subindex;
};
```

OD 用于：

```text
SDO access
startup configuration
diagnostics
protocol metadata
device parameters
```

典型对象：

```text
0x1000 Device Type
0x1001 Error Register
0x1005 COB-ID SYNC
0x1006 Communication Cycle Period
0x1007 Synchronous Window Length
0x1017 Producer Heartbeat Time
0x1400... RPDO communication
0x1600... RPDO mapping
0x1800... TPDO communication
0x1A00... TPDO mapping
```

---

### 4.2 OD 不进入 RT Hot Path

禁止：

```text
RT cycle
    ↓
OD lookup(index, subindex)
    ↓
map / variant / metadata
```

正确模型：

```text
Startup
    ↓
Object Dictionary / static config
    ↓
resolve PDO mapping
    ↓
build PdoPlan
    ↓
freeze
    ↓
Runtime direct access
```

OD 属于：

```text
control/configuration plane
```

---

### 4.3 ProcessImage

ProcessImage 是：

> Node-local cyclic runtime storage。

V1 明确采用 per-node 模型：

```text
Network
 ├── Node 1
 │    └── ProcessImage
 ├── Node 2
 │    └── ProcessImage
 └── Node N
      └── ProcessImage
```

CANopen 不建立：

```text
Global Robot ProcessImage
```

跨 Node 聚合属于 System：

```text
Node ProcessImage
       ↓
profile/device interpretation
       ↓
System Runtime Snapshot
       ↓
HardwareStateView
```

这样 CANopen 保持协议层边界。

---

## 5. NMT、Heartbeat、EMCY 与 SYNC

### 5.1 NMT

支持状态：

```cpp
enum class NmtState {
    Initializing,
    PreOperational,
    Operational,
    Stopped,
};
```

支持：

```text
Start Remote Node
Stop Remote Node
Enter Pre-Operational
Reset Node
Reset Communication
```

NMT Operational 只表示 CANopen 协议状态。

它不等于：

```text
motor enabled
joint ready
robot running
```

---

### 5.2 Heartbeat

支持：

```text
Heartbeat Producer
Heartbeat Consumer
```

Producer：

```text
periodically transmit NMT state
```

Consumer：

```text
record last heartbeat
evaluate timeout
```

配置：

```cpp
struct HeartbeatConsumerConfig {
    Duration timeout;
};
```

由：

```cpp
NodeConfig::heartbeat
```

显式传入。

CANopen core 不自动强制：

```text
timeout = producer_time * 2 + margin
```

这样的策略。

上层可以据设备特性计算 timeout 后传入。

---

### 5.3 0x1017

必须明确：

```text
0x1017
    Producer Heartbeat Time
```

它不是：

```text
timestamp
SYNC period
control period
heartbeat consumer timeout
```

Producer heartbeat 配置与本地 consumer timeout 是两个不同概念。

---

### 5.4 Heartbeat Timeout

Heartbeat timeout 是：

```text
communication fact
```

例如：

```text
Node heartbeat lost
```

它不直接触发：

```text
QuickStop
System Fault
```

System/Safety 决定最终动作。

---

### 5.5 EMCY

支持 Emergency message。

至少保留：

```cpp
struct EmcyEvent {
    std::uint8_t node_id;

    std::uint16_t error_code;
    std::uint8_t error_register;

    std::array<std::byte, 5> manufacturer_data;

    MonotonicTimestamp received_at;
};
```

EMCY 是离散协议事件。

不能只保存：

```text
last_emcy_code
```

而丢失时间和事件顺序。

V1 可通过 bounded event queue 向上提供。

不建设通用 EventBus。

---

### 5.6 SYNC

支持：

```text
SYNC Producer
SYNC Consumer
```

关键对象：

```text
0x1005
    COB-ID SYNC

0x1006
    Communication Cycle Period

0x1007
    Synchronous Window Length
```

SYNC 时间使用统一 monotonic domain。

不得使用：

```text
CLOCK_REALTIME
wall clock
0x1017
```

作为 control timing。

SYNC 表示通信同步点，不保证所有设备物理上完全同时采样。

---

## 6. SDO

### 6.1 SDO 定位

SDO 用于：

```text
startup configuration
OD read/write
diagnostics
recovery
PDO mapping setup
```

V1 支持：

```text
SDO Client
SDO Server
```

至少实现当前真实设备需要的：

```text
expedited transfer
segmented transfer where required
abort handling
timeout
```

---

### 6.2 SDO 不进入 RT Cyclic Path

硬性规则：

> RT cyclic path MUST NOT initiate SDO transactions.

原因：

```text
request/response
variable latency
timeout
retry
multi-frame transfer
remote dependency
```

都不具备 deterministic cyclic timing。

---

### 6.3 故障响应与 SDO 诊断

如果 RT 控制发现异常：

```text
RT cycle
    ↓
detect abnormal state
    ↓
System Safety
    ↓
Hold / QuickStop / Disable
```

快速安全响应不能依赖 SDO。

详细故障诊断：

```text
non-RT diagnostic/recovery path
        ↓
SDO read
        ↓
Diagnostic Snapshot
        ↓
System / Application
```

因此 V1 接受：

> 详细诊断信息比快速安全动作晚若干毫秒。

如果某个故障信息对实时安全决策必不可少，它应通过：

```text
PDO
EMCY
periodic status
```

进入实时可见路径，而不是只通过 SDO 提供。

---

### 6.4 V1 不设计 SdoRequestQueue

V1 不增加：

```text
RT
 ↓
SdoRequestQueue
 ↓
non-RT SDO worker
```

机制。

原因：

```text
当前没有真实需求证明其必要性
增加第二条 RT/non-RT command path
增加 request lifetime / cancellation / result ownership 复杂度
```

未来确有需求再设计。

---

### 6.5 SDO Error

SDO 至少区分：

```text
transport error
timeout
SDO abort
invalid response
protocol state error
```

并保留：

```text
SDO abort code
index
subindex
node-id
```

作为 root cause。

---

## 7. PDO、PdoPlan 与 Runtime Data

### 7.1 PDO Direction

区分：

```text
RPDO
    PDO received by node

TPDO
    PDO transmitted by node
```

CANopen core 不写死：

```text
RPDO = command
TPDO = feedback
```

因为这取决于当前 node/role。

---

### 7.2 V1 PDO 配置来源

V1 明确：

> **静态 C++ configuration 是 PDO mapping 的 source of truth。**

例如：

```cpp
struct PdoMappingEntry {
    ObjectKey object;
    std::uint8_t bit_length;
};

struct PdoConfig {
    std::uint32_t cob_id;

    std::uint8_t transmission_type;

    std::vector<PdoMappingEntry> mappings;
};
```

配置位于：

```text
NodeConfig
```

startup：

```text
Static C++ configuration
        ↓
validate
        ↓
build local PdoPlan
```

如果 Slave 需要通过 SDO 配置 PDO mapping：

```text
same static config
      ↓
SDO write to slave
```

也就是说：

```text
local C++ config
    = source of truth

SDO
    = configuration transport
```

不是：

> 必须先从 Slave 读取 PDO mapping 才能建立本地 PdoPlan。

EDS/DCF 后续作为 MAY。

---

### 7.3 PDO Transmission Type

V1 明确支持范围。

#### MUST

```text
type 1
    synchronous cyclic
    transmit/process every SYNC
```

这是 500 Hz 控制闭环的核心场景。

#### SHOULD

```text
type 2..240
    synchronous cyclic
    every N-th SYNC

type 0
    synchronous acyclic
```

例如：

```text
SYNC = 500 Hz

type 1
    → 500 Hz

type 5
    → 100 Hz
```

非常适合总线负载分级。

#### MAY

```text
type 254
type 255
```

event-driven 行为。

其相关：

```text
event timer
inhibit time
```

等机制在真实需求出现后完善。

#### V1 不支持

```text
241..253
```

保留/非普通应用范围。

V1 不优先支持 RTR-driven PDO 行为。

---

### 7.4 PdoPlan

启动阶段构建 immutable PdoPlan。

流程：

```text
PdoConfig
    ↓
validate mapping
    ↓
resolve bit width
    ↓
resolve ProcessImage offset/access
    ↓
PdoPlanBuilder
    ↓
immutable PdoPlan
```

Runtime：

```text
CAN Frame
   ↓
PdoPlan decode
   ↓
Node ProcessImage
```

TX：

```text
Node ProcessImage
   ↓
PdoPlan encode
   ↓
CAN Frame
```

PdoPlan runtime 必须：

```text
immutable
bounded
no allocation
no OD lookup
no string
no mapping mutation
```

---

### 7.5 PdoPlan 与 ObjectDictionary

PdoPlan 可以在 build 阶段利用 OD metadata 验证：

```text
object exists
bit width
accessibility
mapping legality
```

但 runtime 不再查询 OD。

即：

```text
OD
 ↓ startup only
PdoPlan
 ↓ runtime
ProcessImage
```

---

### 7.6 Dynamic Remapping

V1 不要求 dynamic PDO remapping。

以后如实现：

```text
leave cyclic operation
      ↓
configuration state
      ↓
SDO remap
      ↓
build new PdoPlan
      ↓
validate
      ↓
replace before resume
```

它不属于 RT deadline。

因此不要求：

```text
< 2 ms
```

完成。

---

### 7.7 PDO Encode / Decode

禁止：

```cpp
reinterpret_cast<MyPackedStruct*>(
    frame.data.data());
```

解码。

必须使用明确：

```text
little-endian load/store
bit width validation
signedness handling
bounds check
```

例如：

```cpp
auto value =
    load_le<std::int32_t>(
        frame.data.data() + offset);
```

避免：

```text
alignment
padding
strict aliasing
endianness
```

问题。

---

## 8. Master、Slave 与 CiA402

### 8.1 Master

Master 聚合：

```text
NMT producer
SDO clients
PDO producer/consumer
Heartbeat consumers
EMCY consumers
SYNC producer/consumer
Node state
```

Master 不负责：

```text
trajectory
PID
impedance control
robot Safety
```

---

### 8.2 Slave

Slave 聚合：

```text
NMT state
SDO server
PDO producer/consumer
Heartbeat producer
EMCY producer
SYNC consumer
Object Dictionary
ProcessImage
```

主要用于：

```text
virtual device
integration test
CiA402 fake drive
protocol simulation
```

---

### 8.3 CiA402

CiA402 位于：

```text
canopen/cia402/
namespace canopen::cia402
```

关系：

```text
CAN
 ↓
CANopen
 ↓
CiA402
```

CANopen core 不知道：

```text
0x6040 Controlword
0x6041 Statusword
```

这些属于 CiA402。

---

### 8.4 CiA402 Master

负责：

```text
Statusword decode
PDS state decode
desired transition
Controlword generation
mode handling
fault reset semantics
process-data binding
```

路径：

```text
Statusword
   ↓
PDS State
   ↓
transition
   ↓
Controlword
```

---

### 8.5 CiA402 Slave

负责：

```text
Controlword decode
PDS FSA
Statusword generation
mode-specific behavior
fault behavior
```

与 simulation：

```text
CANopen Slave
      ↓
CiA402 Slave
      ↓
simulation::Motor
```

Simulation Motor 不知道：

```text
PDO
SDO
NMT
Object Index
```

它只处理：

```text
position
velocity
torque
limits
fault
```

---

### 8.6 工程单位

设备 raw unit 与工程单位转换：

```text
rad
rad/s
Nm
    ↕
encoder count
drive raw velocity
drive raw torque
```

startup 预计算：

```text
scale
offset
conversion parameters
```

RT path 只执行固定成本转换。

禁止每周期查询 OD 或解析单位 metadata。

---

## 9. CANopen 与 Realtime/System 边界

CANopen 不创建：

```text
PeriodicTask
RT scheduler
control thread
```

推荐：

```text
System / Device
      ↓
Realtime scheduler
      ↓
CANopen cyclic API
```

System 决定：

```text
何时调用
哪个线程调用
什么周期
```

避免形成第二套 Runtime。

### RX 路径

```text
SocketCAN
    ↓
can::Frame
    ↓
Network RouteTable
    ↓
Node protocol endpoint
    ↓
PDO / SDO / Heartbeat / EMCY / SYNC
    ↓
Node ProcessImage / protocol state
```

### 周期控制路径

```text
System RT cycle
      ↓
read Node ProcessImage
      ↓
Device/CiA402 interpretation
      ↓
Application control
      ↓
update command ProcessImage
      ↓
PDO encode
      ↓
CAN TX
```

### Non-RT 路径

```text
startup
diagnostics
recovery
      ↓
SDO
NMT
PDO configuration
device setup
```

---

## 10. 状态、错误与统计

### 10.1 Error

建议：

```cpp
enum class ErrorCode {
    InvalidArgument,
    InvalidNodeId,
    InvalidState,

    InvalidCobId,
    InvalidPdoMapping,
    InvalidObject,

    TransportError,

    Timeout,
    SdoAbort,
    InvalidSdoResponse,

    NmtError,
    HeartbeatTimeout,

    PdoDecodeError,
    PdoEncodeError,

    SyncError,
};
```

Error：

```cpp
struct Error {
    ErrorCode code;

    std::uint8_t node_id{0};

    std::uint16_t index{0};
    std::uint8_t subindex{0};

    std::uint32_t protocol_code{0};
};
```

`protocol_code` 保存：

```text
SDO abort code
EMCY code
other CANopen protocol code
```

底层 CAN root cause 由上层错误包装时保留，不要求 V1 构建通用 ErrorChain。

---

### 10.2 NodeStatus

```cpp
struct NodeStatus {
    std::uint8_t node_id;

    NmtState nmt_state;

    bool heartbeat_alive;
    bool communication_ok;

    MonotonicTimestamp last_heartbeat;
    MonotonicTimestamp last_pdo;

    bool emcy_active;
};
```

这些只表达协议事实。

不表达：

```text
joint safe
robot enabled
System Running
```

---

### 10.3 Network Status

```cpp
struct Status {
    bool running;

    std::size_t configured_nodes;
    std::size_t operational_nodes;
    std::size_t degraded_nodes;
};
```

---

### 10.4 Stats

```cpp
struct Stats {
    std::uint64_t rx_frames{0};
    std::uint64_t tx_frames{0};

    std::uint64_t rpdo_received{0};
    std::uint64_t tpdo_sent{0};

    std::uint64_t sdo_requests{0};
    std::uint64_t sdo_aborts{0};
    std::uint64_t sdo_timeouts{0};

    std::uint64_t heartbeat_received{0};
    std::uint64_t heartbeat_timeouts{0};

    std::uint64_t emcy_received{0};

    std::uint64_t sync_received{0};
    std::uint64_t sync_sent{0};

    std::uint64_t decode_errors{0};
};
```

CAN 层已有：

```text
bus-off
RX overflow
error frame
```

不在 CANopen 重复统计。

---

### 10.5 Safety 边界

CANopen 只提供：

```text
Heartbeat timeout
EMCY
NMT state
PDO freshness
CiA402 fault
```

System/Safety 决定：

```text
Continue
Hold
QuickStop
Disable
Fault
```

禁止 CANopen 内部直接修改 System 生命周期。

---

### 10.6 Recovery

CANopen 可以提供：

```text
NMT reset
reset communication
SDO reconfiguration
PDO reconfiguration
protocol reinitialize
```

但 recovery 成功只表示：

```text
CANopen protocol recovered
```

不代表：

```text
System Running
```

---

## 11. V1 范围与测试

### MUST

```text
Network
Node

static NodeConfig

immutable RouteTable

Object Dictionary

NMT

SDO Client
SDO Server
timeout
abort handling

PDO
RPDO / TPDO

static C++ PdoConfig

transmission type 1

PdoPlan
immutable runtime plan

per-node ProcessImage

Heartbeat producer/consumer
explicit consumer timeout

EMCY

SYNC
0x1005
0x1006
0x1007

Master
Slave

CiA402 Master
CiA402 Slave

Status / Stats / Error

vcan end-to-end test

Virtual Motor closed loop
```

### SHOULD

```text
PDO transmission type 0
PDO transmission type 2..240

segmented SDO where needed

multiple PDO per node

PDO stale detection

engineering-unit conversion

7-node integration

physical CAN validation

CiA402 fault injection

long-running soak
```

### MAY

```text
PDO type 254 / 255

EDS / DCF

LSS

dynamic PDO remapping

block SDO

TIME protocol

CANopen FD

advanced recovery

device profile plugins

SdoRequestQueue
    only if real asynchronous diagnostic demand appears
```

明确不提前建设：

```text
EDS runtime framework
generic protocol registry
CANopen executor
CANopen scheduler
plugin manager
dynamic OD reflection
generic event framework
```

---

### Unit Tests

覆盖：

```text
Node-ID validation

COB-ID routing

RouteTable conflicts

NMT

Heartbeat timeout

EMCY encode/decode

SYNC object semantics

0x1017 semantics

OD access

SDO expedited
SDO segmented if implemented
SDO abort
SDO timeout

PDO config validation

PDO transmission type validation

PdoPlan build/freeze

PDO encode/decode

little-endian
signed values
bit width
mapping bounds

ProcessImage per-node isolation

CiA402 state decode

Controlword generation

CiA402 slave transitions
```

---

### vcan Integration

构造：

```text
CANopen Master
      │
      ▼
    vcan0
      │
      ▼
CANopen Slave
      │
      ▼
CiA402 Slave
      │
      ▼
Virtual Motor
```

验证：

```text
NMT startup

SDO configuration

PDO mapping

SYNC

type 1 cyclic PDO

Heartbeat

EMCY

CiA402 enable

target command

feedback update

fault injection

reset/recovery
```

---

### Physical CAN

真实设备验证：

```text
node startup

NMT

SDO

PDO

Heartbeat

EMCY

SYNC

7 nodes

node unplug

heartbeat loss

device fault

bus-off propagation

PDO jitter

bus load
```

---

### 500 Hz 与总线负载

目标：

```text
7 nodes
500 Hz
Classic CAN
```

如果：

```text
1 command + 1 feedback
per node per cycle
```

则：

```text
7 × 2 × 500
=
7000 frames/s
```

对 1 Mbps Classic CAN 很高。

因此网络配置必须允许：

```text
critical command
    type 1
    500 Hz

critical feedback
    type 1
    500 Hz

secondary feedback
    type 5
    100 Hz

slow status
    lower frequency / event-driven
```

需要综合：

```text
PDO packing
transmission type
SYNC frequency
COB-ID priority
feedback decimation
event-driven traffic
```

CANopen 模块提供机制，但不自行决定业务频率。

---

### Runtime 性能约束

CANopen cyclic hot path：

```text
no allocation
no SDO
no OD lookup
no string
no dynamic remapping
no blocking wait
bounded routing
bounded encode/decode
```

Startup/non-RT 可：

```text
allocate
build config
perform SDO
validate OD
build RouteTable
build PdoPlan
```

Benchmark 至少测：

```text
RouteTable lookup

PDO decode

PDO encode

per-node ProcessImage update

7-node dispatch cost

SYNC-to-PDO timing

PDO age/jitter
```

---

## 12. 参考实现与架构不变量

### CANopenNode

首要参考：

```text
NMT
SDO
PDO
Heartbeat
EMCY
SYNC
OD
```

重点借鉴协议语义和状态机。

不机械复制其嵌入式全局状态组织。

### Lely

参考：

```text
Network / Node organization
Linux CANopen
Master architecture
error handling
```

不引入大型 async runtime。

### ros_canopen / ros2_canopen

参考：

```text
Master integration
CiA402
PDO / SDO engineering patterns
fake slave
integration tests
```

不引入 ROS dependency。

---

### 架构不变量

1. `canopen` 只依赖 CAN abstraction，不直接依赖 SocketCAN。
2. CANopen core 不依赖 RoboHardware System、Realtime scheduler 或 ROS2。
3. CiA402 必须位于 `canopen::cia402`。
4. CANopen core 不包含机器人或电机控制算法语义。
5. Master/Slave 共享协议组件，不复制两套协议实现。
6. V1 使用静态 Node-ID 和静态 C++ 网络配置。
7. Network 在 startup 阶段构建并冻结 COB-ID RouteTable。
8. Runtime routing 必须 bounded、无动态分配、无字符串查找。
9. Object Dictionary 属于配置/控制平面。
10. ProcessImage 属于 runtime process-data 平面。
11. ProcessImage 必须是 per-node。
12. 全局 HardwareState 聚合属于 System。
13. RT PDO path 不进行 OD lookup。
14. RT PDO path 不动态分配。
15. RT PDO path 不进行动态 mapping mutation。
16. SDO 不允许从 RT cyclic hot path 发起。
17. Safety 快速响应不得依赖 SDO。
18. 详细 SDO 诊断允许通过 non-RT 路径延迟获得。
19. 若某信息对实时安全必要，应通过 PDO/EMCY 等实时可见机制暴露。
20. V1 不实现 SdoRequestQueue。
21. V1 PDO mapping 的 source of truth 是静态 C++ 配置。
22. SDO 可用于把相同静态 mapping 应用到远端 Slave。
23. PdoPlan 在 startup 构建并冻结。
24. Runtime PdoPlan 必须 immutable。
25. PDO type 1 是 V1 MUST。
26. PDO type 0 和 2..240 属于 SHOULD。
27. PDO type 254/255 属于 MAY。
28. 241..253 不作为 V1 普通支持范围。
29. Heartbeat consumer timeout 必须由配置显式给出。
30. Heartbeat timeout 只是通信事实，不直接决定 Safety。
31. `0x1017` 只表示 Producer Heartbeat Time。
32. `0x1017` 不得用于 control timestamp 或 consumer timeout。
33. SYNC 使用 `0x1005 / 0x1006 / 0x1007`。
34. SYNC、Heartbeat、timeout、freshness 都使用 monotonic time domain。
35. SYNC 不等价于所有设备物理同时采样。
36. EMCY 必须作为带时间戳的离散协议事件保留。
37. PDO direction 只表达 RPDO/TPDO，不硬编码 command/feedback。
38. PDO payload 禁止通过 packed-struct `reinterpret_cast` 解析。
39. 协议整数必须显式处理 endian、位宽和 signedness。
40. Dynamic PDO remapping 若未来实现，只能在 cyclic runtime 外执行。
41. CANopen 不自行创建第二套 realtime scheduler。
42. System 决定 CANopen cyclic API 的线程和周期。
43. CAN BusEvent 与 CANopen EMCY/Heartbeat 必须保持层级区分。
44. `send()` 成功不表示远端 PDO 已处理。
45. CANopen Status/Stats 只表达 CANopen 协议事实。
46. bus-off、RX overflow 等底层事实由 CAN 层提供。
47. CANopen recovery primitive 不得自行将 System 切回 Running。
48. V1 不依赖 EDS/DCF/LSS。
49. Master ↔ vcan ↔ Slave 是核心集成测试路径。
50. CiA402 Slave + Virtual Motor 必须形成完整协议闭环。
51. 7 节点 500 Hz 必须通过真实 CAN load 与 timing benchmark 验证。
52. CANopen 模块长期保持协议基础层定位，不演化成机器人控制框架。

最终核心模型：

```text
                     System / Device
                           │
                           ▼
                    canopen::Master
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
         NMT              SDO              PDO
          │                                  │
   Heartbeat / EMCY                          ▼
          │                          per-node ProcessImage
         SYNC                                │
          │                                  ▼
          └───────────────┬──────── canopen::cia402
                          │
                          ▼
                   immutable RouteTable
                          │
                          ▼
                    can::Interface
                          │
                          ▼
                       CAN Bus
```

测试/仿真：

```text
canopen::Master
      │
      ▼
     vcan
      │
      ▼
canopen::Slave
      │
      ▼
canopen::cia402::Slave
      │
      ▼
simulation::Motor
```

CANopen 模块最终目标是：

> **提供一个建立在通用 CAN abstraction 之上的、CiA301 语义正确、配置面与周期数据面严格分离、支持静态多节点网络、Master/Slave、per-node ProcessImage 和 immutable PdoPlan，并能够通过 vcan 完整验证 CiA402 500 Hz 控制链路的 C++17 CANopen 协议基础模块。**

**CANopen V1 的架构与公共语义在上述边界内冻结。后续扩展应由真实设备对 EDS、LSS、动态 PDO、event-driven PDO、CANopen FD 或异步 SDO 需求驱动，而不是提前建设通用协议 Runtime。**
