# RoboHardware 整体架构设计

## 1. 项目定位与设计原则

`RoboHardware` 是一个面向 Linux 机器人硬件接入、实时运行和设备编排的 C++17 基础项目。

它的目标是为上层机器人应用提供统一、可实时、可测试、可扩展的硬件运行基础，使 Application 不需要直接依赖 CAN、Serial、CANopen 或具体设备协议。

当前重点支持：

```text
Realtime
CAN
Serial
CANopen
CiA402
Simulation
System
```

第一阶段核心闭环：

```text
Application
    ↓
RoboHardware System
    ↓
CANopen Master
    ↓
SocketCAN
    ↓
CANopen Slave
    ↓
CiA402
    ↓
Virtual Motor
```

目标环境：

```text
Linux
C++17
Classic CAN first
PREEMPT_RT compatible
500 Hz drive control cycle
```

RoboHardware 的定位是：

> 机器人硬件基础设施，而不是机器人控制算法框架。

它负责：

```text
设备接入
硬件通信
设备协议
实时执行
设备生命周期
系统配置
设备拓扑
统一时间基线
安全协调
状态与故障汇总
真实 / 仿真设备编排
```

它不负责：

```text
PID
阻抗控制
轨迹生成
运动规划
碰撞检测
机器人行为逻辑
任务规划
```

这些属于 Application。

项目不从头重新发明已有机制，而采用：

```text
成熟参考实现
      ↓
理解标准行为和工程边界
      ↓
保留成熟设计
      ↓
去除历史依赖和过度框架
      ↓
使用现代 C++17 重新实现
      ↓
Unit / Integration / HIL 验证
```

整体设计坚持以下原则：

1. **分层清晰**
   Application → System → Device → Infrastructure，依赖只能自上而下。

2. **通用模块独立**
   `realtime`、`can`、`serial` 可以脱离 RoboHardware 单独使用。

3. **System 只负责编排**
   System 管理配置、生命周期、拓扑、时间、安全和状态，但不重新实现 CANopen、Serial 或 Realtime。

4. **Device 隔离具体协议**
   Application 不直接感知 CANopen、SocketCAN、termios 等底层细节。

5. **允许异构设备**
   系统不假设所有 Device 都是 500 Hz 周期设备。

6. **统一控制时间域**
   所有控制、timeout、freshness、安全判断使用同一个 monotonic time domain。

7. **真实与仿真共享边界**
   Real Device 与 Simulation Device 对 System/Application 保持一致的设备语义。

8. **错误根因不可丢失**
   上层可以提升错误语义，但必须保留底层 root cause。

9. **接口预留，实现推迟**
   为未来扩展保留稳定边界，但不提前建设 Factory、Registry、Plugin Framework。

10. **先完成闭环，再泛化**
    V1 第一优先级永远是可运行、可测试的端到端硬件闭环。

---

## 2. 整体分层与核心对象

RoboHardware 采用四层架构：

```text
┌──────────────────────────────────────────────┐
│                Application                   │
│                                              │
│ Robot Logic / Controller / ROS2             │
└──────────────────────┬───────────────────────┘
                       │
          HardwareState / HardwareCommand
                       │
                       ▼
┌──────────────────────────────────────────────┐
│                   System                     │
│                                              │
│ Config / Topology / Lifecycle               │
│ Clock / Runtime Snapshot                    │
│ Safety / Status / Fault                     │
│ Device Orchestration                        │
└──────────────────────┬───────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────┐
│                   Device                     │
│                                              │
│ CANopen / CiA402 / Future Devices           │
│ Real Backend / Simulation Backend           │
│ Cyclic / Async / Event-driven               │
└──────────────────────┬───────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────┐
│               Infrastructure                 │
│                                              │
│ Realtime        CAN        Serial           │
└──────────────────────┬───────────────────────┘
                       │
                       ▼
                Linux / Hardware
```

### Application

Application 是 RoboHardware 的使用者。

它通过：

```text
HardwareStateView
HardwareCommandView
```

与 System 交互。

典型关系：

```text
HardwareStateView
       ↓
Application Controller
       ↓
HardwareCommandView
       ↓
RoboHardware
```

Application 不直接操作：

```text
CAN Frame
SocketCAN fd
SDO
PDO
Serial byte stream
termios
```

RoboHardware 不规定控制算法实现方式。

Application 可以是：

```text
Standalone C++ application
Robot controller
ROS2 node
ros2_control controller
Test program
```

---

### System

System 是 RoboHardware 的项目级入口和运行协调者。

它拥有并管理：

```text
SystemConfig
DeviceTopology
Unified Clock
Runtime Storage
Realtime Execution
Safety
SystemStatus
FaultRecord
```

System 的核心职责：

```text
配置
拓扑构建
生命周期
设备编排
统一时间
Runtime Snapshot
安全决策
状态聚合
故障记录
停止 / 恢复协调
```

建议核心 API：

```cpp
namespace robohardware {

class System {
public:
    Result<void> configure(const SystemConfig&);
    Result<void> initialize();
    Result<void> start();
    Result<void> stop();

    SystemState state() const noexcept;
    SystemStatus status() const;

    const realtime::Clock& clock() const noexcept;
};

}  // namespace robohardware
```

`System` 是 facade，而不是 God Object。

它不能承担：

```text
CAN protocol implementation
CANopen state machine
Serial protocol parser
Control algorithm
Telemetry backend
Plugin manager
```

---

### Device

Device 是 System 与具体设备实现之间的最小契约。

V1 建议定义：

```cpp
namespace robohardware {

class Device {
public:
    virtual ~Device() = default;

    virtual Result<void> initialize() = 0;
    virtual Result<void> start() = 0;
    virtual Result<void> stop() = 0;

    virtual DeviceCapabilities capabilities() const noexcept = 0;
    virtual DeviceStatus status() const noexcept = 0;
};

}  // namespace robohardware
```

Device 契约只统一：

```text
生命周期
能力描述
状态
数据端点
```

而不强制所有设备实现：

```text
read()
write()
500 Hz update()
```

因为设备可能是：

```text
Cyclic
Asynchronous
Event-driven
```

例如：

```text
CiA402 Drive
    500 Hz cyclic

Serial actuator
    100 Hz cyclic + async RX

IMU
    1 kHz asynchronous

LiDAR
    10~20 Hz asynchronous

Camera
    frame-driven
```

可以定义轻量能力描述：

```cpp
struct DeviceCapabilities {
    bool has_state;
    bool accepts_command;
    bool cyclic;

    std::chrono::nanoseconds preferred_period;
};
```

System 根据能力参与调度，但不要求所有设备共享同一周期。

---

### Infrastructure

Infrastructure 当前包括：

```text
realtime
can
serial
```

三者平级。

依赖关系：

```text
CANopen → CAN
CiA402  → CANopen

System → Device
System → Realtime

CAN ↛ CANopen
Serial ↛ CAN
Realtime ↛ CAN
Realtime ↛ CANopen
```

CAN 与 Serial 不继承统一 `Transport` 基类。

原因：

```text
CAN
    frame-oriented
    CAN ID
    filter
    bus state

Serial
    byte-stream
    baudrate
    parity
    timeout
```

只复用真正通用的 Linux/realtime 基础能力，不强行统一通信语义。

---

## 3. 项目结构、配置与运行时数据

推荐目录：

```text
robohardware/
├── include/
│   ├── robohardware/
│   │   ├── system.hpp
│   │   ├── config.hpp
│   │   ├── state.hpp
│   │   ├── status.hpp
│   │   └── device.hpp
│   │
│   ├── realtime/
│   ├── can/
│   ├── serial/
│   ├── canopen/
│   │   └── cia402/
│   └── simulation/
│
├── src/
│   ├── system/
│   ├── realtime/
│   ├── can/
│   ├── serial/
│   ├── canopen/
│   │   └── cia402/
│   └── simulation/
│
├── apps/
├── examples/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── realtime/
│   └── hardware/
│
├── config/
├── tools/
├── scripts/
│
├── docs/
│   ├── design.md
│   ├── realtime.md
│   ├── can.md
│   ├── serial.md
│   └── canopen.md
│
└── CMakeLists.txt
```

总体 `design.md` 只描述系统级架构。

具体：

```text
PDO
SDO
Object Dictionary
PdoPlan
ProcessMeta
lock-free algorithm
memory_order
CiA402 Controlword
Serial parser
```

全部放入对应模块设计文档。

---

### Namespace

项目级公共模型：

```cpp
robohardware::System
robohardware::SystemConfig
robohardware::SystemState
robohardware::SystemStatus
robohardware::Device
robohardware::HardwareStateView
robohardware::HardwareCommandView
```

独立模块：

```cpp
realtime::
can::
serial::
canopen::
canopen::cia402::
simulation::
```

即：

```text
Repository
    robohardware

Project-level facade
    robohardware::

Reusable modules
    realtime::
    can::
    serial::
    canopen::
    simulation::
```

---

### CMake

推荐 target：

```text
RoboHardware::System
RoboHardware::Realtime
RoboHardware::Can
RoboHardware::Serial
RoboHardware::Canopen
RoboHardware::Simulation
```

主要依赖：

```text
System
 ├── Realtime
 ├── Canopen
 ├── Serial
 └── Simulation

Canopen
 └── Can
```

底层 target 不允许依赖 `RoboHardware::System`。

构建边界应与模块边界一致，但不要求所有模块都是动态库。

---

### SystemConfig

System 提供统一配置入口，但不建设万能配置框架。

配置应该表达硬件 topology：

```text
SystemConfig
├── realtime
├── safety
└── devices
    ├── CANopen device
    ├── Serial device
    └── Simulation device
```

模块继续拥有自己的具体配置：

```text
RealtimeConfig
CanConfig
SerialConfig
CanopenNodeConfig
SimulationConfig
```

System 负责：

```text
组合
校验
拓扑构建
生命周期
```

配置生命周期：

```text
Load
 ↓
Validate
 ↓
Build Topology
 ↓
Freeze
 ↓
Initialize
 ↓
Runtime
```

进入 Runtime 后，以下内容默认 immutable：

```text
Device topology
Backend selection
CAN interface
Node-ID
Realtime period
Runtime storage layout
```

修改关键配置需要：

```text
stop
 ↓
reconfigure
 ↓
validate
 ↓
initialize
 ↓
restart
```

---

### Real / Simulation 配置

Real 与 Simulation 是 **per-device backend**，不是整个 System 的全局模式。

概念上：

```text
Device
├── identity
├── backend
└── module-specific configuration
```

例如：

```text
left_arm
    backend = Real

right_arm
    backend = Real

lift
    backend = Simulation

base
    backend = Simulation
```

V1 可以通过 C++ Builder 显式构造：

```cpp
builder.add_canopen_drive(...);
builder.add_simulated_drive(...);
```

不要求 V1 立即实现：

```text
YAML parser
DeviceFactory
DriverRegistry
Dynamic Plugin Loader
```

以后配置文件只是在相同 SystemConfig 模型上的输入方式扩展。

---

### Runtime Storage 与 Snapshot

Application 不直接读取 Device 私有内存。

System 拥有统一 Runtime Storage。

初始化阶段：

```text
Configuration
      ↓
Build topology
      ↓
Allocate runtime storage
      ↓
Freeze layout
      ↓
Runtime
```

Application 只读取 System 生成的：

```text
HardwareStateView
```

并写：

```text
HardwareCommandView
```

推荐使用 non-owning view：

```cpp
struct JointStateView {
    Span<const double> position;
    Span<const double> velocity;
    Span<const double> effort;
};

struct JointCommandView {
    Span<double> position;
    Span<double> velocity;
    Span<double> effort;
};
```

这些 View：

```text
不拥有内存
不 resize
不分配
Runtime layout 固定
```

因此既支持运行时拓扑，又避免 RT 中动态分配。

---

### Global Runtime Snapshot

System 每个控制周期生成一个 Application 可见的稳定快照：

```text
Device-private state
       ↓
System snapshot collection
       ↓
RuntimeSnapshot
       ↓
HardwareStateView
       ↓
Application
```

这样 Application 在一个周期内读取的数据不会被 Device worker 并发修改。

但：

> Snapshot 一致并不意味着所有设备具有同一物理采样时刻。

例如：

```text
Drive   500 Hz
IMU     1 kHz
LiDAR   10 Hz
```

因此每个状态 domain 仍需保留：

```text
timestamp
validity
freshness
```

System 保证：

> 一个 Application cycle 内看到的 Snapshot 不发生并发变化。

而不是强行保证所有异构设备同步采样。

---

## 4. 生命周期、时间、安全与错误

### System Lifecycle

保持简洁：

```cpp
enum class SystemState {
    Unconfigured,
    Configured,
    Initialized,
    Running,
    Degraded,
    Stopping,
    Stopped,
    Faulted,
};
```

正常路径：

```text
Unconfigured
    ↓ configure()
Configured
    ↓ initialize()
Initialized
    ↓ start()
Running
    ↓ stop()
Stopping
    ↓
Stopped
```

异常：

```text
Running
   ↓
Degraded
   ↓
Faulted
```

恢复：

```text
Faulted
   ↓
recover / reinitialize
   ↓
Initialized
   ↓
start
   ↓
Running
```

V1 不建设复杂 Lifecycle Framework。

---

### 启动顺序

System 统一执行：

```text
1. Load configuration

2. Validate configuration

3. Build and freeze topology

4. Allocate runtime storage

5. Initialize transport
   - CAN
   - Serial

6. Initialize devices
   - CANopen
   - CiA402
   - Simulation
   - future device

7. Configure devices

8. Start asynchronous RX / event paths

9. Wait for required initial state

10. Prepare runtime snapshots and commands

11. Start realtime execution

12. Enable cyclic devices

13. Enter Running
```

基本规则：

> 所有资源必须在其消费者 execution context 启动前准备完成。

---

### Graceful Stop

`System::stop()` 必须严格遵守：

```text
signal
 ↓
safe exit
 ↓
join
 ↓
release
```

完整过程：

```text
1. stop_requested = true

2. Stop accepting new Application commands

3. RT thread observes stop request

4. RT emits final safe output

5. RT exits at safe cycle boundary

6. System joins RT thread

7. Stop cyclic device output

8. Stop asynchronous device workers / RX threads

9. Join all workers

10. Stop Device instances

11. Close CAN / Serial transports

12. Release runtime storage

13. Enter Stopped
```

RT callback 规则：

```cpp
void rt_cycle(...) noexcept {
    if (stop_requested_.load(std::memory_order_acquire)) {
        return;
    }

    // All referenced runtime resources are frozen
    // and guaranteed to outlive this thread.
}
```

关键不变量：

> 任何线程完全退出之前，不得销毁其可能访问的对象。

所有 RT 依赖资源必须：

```text
start() 前完成构建
↓
Runtime 生命周期内保持稳定
↓
RT join 后才允许释放
```

---

### Unified Time Base

System 维护唯一 control-time domain：

```text
CLOCK_MONOTONIC
```

通过：

```cpp
realtime::Clock
```

提供。

所有参与：

```text
Control dt
Realtime cycle
CAN RX/TX timing
Serial receive timing
Device state timestamp
Timeout
Freshness
Safety deadline
```

的时间必须来自该时间域。

System 暴露：

```cpp
const realtime::Clock& clock() const noexcept;
```

Application 可以使用：

```text
system_clock
UTC
wall time
```

用于：

```text
UI
文件名
普通日志
外部显示
```

但 wall clock 禁止参与控制、安全和 timeout 运算。

---

### Realtime Execution

RoboHardware 提供 realtime execution，但不提供控制算法框架。

基本流程：

```text
Realtime Scheduler
       ↓
Build Runtime Snapshot
       ↓
Safety Check
       ↓
Application Control
       ↓
Commit Hardware Command
```

可提供：

```cpp
system.run_cycle(
    [&](const HardwareStateView& state,
        HardwareCommandView& command,
        const CycleContext& cycle) noexcept {
        // Application control
    });
```

System 不假设所有设备都参与这个周期。

例如：

```text
500 Hz
    arm drives

100 Hz
    lift

async 1 kHz
    IMU

async 10 Hz
    LiDAR
```

Async/Event Device 在独立 execution context 中更新自己的状态，由 System 在 Snapshot 阶段读取其最新稳定状态。

---

### Safety 与 Recovery

Safety 属于 System。

底层模块只提供事实：

```text
Realtime status
Transport status
Device status
Communication status
Fault
Timestamp
```

System 决定：

```text
Continue
Hold
Quick Stop
Disable
Fault
```

分成：

```text
Fast RT Safety
+
Non-RT Recovery
```

Fast RT Safety：

```text
detect unsafe condition
      ↓
block unsafe command
      ↓
emit safe command
```

对 500 Hz drive：

```text
response target ≤ 2 ms
```

Non-RT Recovery：

```text
reset
reconfigure
reconnect
reinitialize
re-enable
```

Recovery 完成后不能自行进入 `Running`。

必须经过 System lifecycle。

---

### Error 与 Fault

基础错误必须保留 root cause。

建议：

```cpp
enum class ErrorDomain {
    Realtime,
    Can,
    Serial,
    Canopen,
    Device,
    System,
};

struct Error {
    ErrorDomain domain;
    ErrorCode code;
    int native_code;
    DeviceId device;
};
```

系统级故障：

```cpp
struct FaultRecord {
    SystemFault reason;
    Error root_cause;
    TimePoint occurred_at;
};
```

例如：

```text
System:
    state  = Faulted
    reason = CommunicationFailure

root cause:
    domain = CAN
    code   = BusOff
    target = can0
```

原则：

> 上层可以解释错误，但不能覆盖底层 root cause。

V1 不强制实现任意长度 `ErrorChain`。

若未来确实需要额外上下文，可扩展固定深度：

```text
SmallErrorChain<4>
```

但属于 MAY。

---

### Observability

V1 不创建 Telemetry Framework。

各模块提供：

```text
RealtimeStats
CanStats
SerialStats
DeviceStatus
SystemStatus
FaultRecord
```

System 聚合：

```text
SystemStatus
```

Application 决定如何输出：

```text
console
CSV
ROS2
Prometheus
InfluxDB
custom UI
```

RT 中禁止直接：

```text
printf
file I/O
network export
synchronous formatted logging
```

高频统计通过 bounded RT → non-RT 通道导出。

---

## 5. V1 范围与扩展边界

为了避免过度设计，所有能力分为：

```text
MUST
SHOULD
MAY
```

同时坚持：

> **接口预留，实现推迟。**

### MUST

V1 必须完成：

```text
System
    lifecycle
    config
    topology
    clock
    state/status
    graceful stop

Minimal Device contract

Runtime Storage
HardwareStateView
HardwareCommandView
Global Runtime Snapshot

Realtime basic runtime

CAN / SocketCAN

CANopen Master
CANopen Slave

CiA402 Master
CiA402 Slave

Simulation / Virtual Motor

Real / Simulation device boundary

vcan end-to-end closed loop

500 Hz cyclic drive control

Basic safety

Root-cause-preserving fault reporting
```

第一阶段验收链路：

```text
Application
    ↓
System
    ↓
CANopen Master
    ↓
vcan
    ↓
CANopen Slave
    ↓
CiA402
    ↓
Virtual Motor
```

能够稳定闭环。

---

### SHOULD

建议 V1 后期完成：

```text
Physical CAN validation
7-node integration
Communication health
Runtime statistics
Engineering-unit conversion
Mixed Real / Simulation
PREEMPT_RT validation
Basic HIL
1 h / 24 h soak
```

---

### MAY

后续按真实需求增加：

```text
Serial concrete device drivers
IMU / LiDAR

EDS / DCF
LSS

Dynamic PDO remapping

Advanced telemetry exporters
Advanced recovery policies

ROS2
ros2_control

CANopen FD
EtherCAT

Runtime plugin loading
Driver Registry
Device Factory
YAML / JSON config parser
Full ErrorChain
```

---

### V1 扩展点预留原则

V1 只定义稳定接口，不提前实现未来机制。

例如：

| 能力                       | V1   | 后续                            |
| ------------------------ | ---- | ----------------------------- |
| `Device` interface       | MUST | 保持兼容                          |
| Device list/topology     | MUST | 动态发现 MAY                      |
| Real/Simulation boundary | MUST | runtime backend switching MAY |
| SystemConfig C++ API     | MUST | YAML/JSON parser MAY          |
| Structured status        | MUST | exporters MAY                 |
| Serial basic port        | 基础能力 | concrete device protocol MAY  |
| CANopen core             | MUST | EDS/LSS/dynamic remap MAY     |
| Error root cause         | MUST | ErrorChain MAY                |
| Static linking           | MUST | dynamic plugin loading MAY    |

设计规则：

```text
Define the boundary now.
Implement the mechanism only when needed.
```

禁止为了 MAY 能力提前引入：

```text
Factory Framework
Driver Registry
Plugin Manager
Protocol Registry
General Device Graph
Reflection Framework
Generic Runtime Framework
```

---

### 实施顺序

推荐：

```text
Phase 1
Realtime
CAN / SocketCAN
vcan

Phase 2
CANopen Master / Slave
basic protocol closed loop

Phase 3
CiA402
Virtual Motor

Phase 4
Minimal Device
System lifecycle
Config / Topology
Clock / Status
Runtime Storage

Phase 5
State / Command boundary
Global Snapshot
500 Hz end-to-end loop
Basic Safety
Graceful Stop

Phase 6
Physical CAN
7 nodes
PREEMPT_RT
HIL

Phase 7
Real / Simulation mixed topology

Phase 8+
Serial concrete devices
EDS / DCF / LSS
ROS2 / ros2_control
```

核心原则：

> 先证明真实数据路径和生命周期正确，再扩展框架能力。

---

## 6. 参考项目与架构不变量

主要参考：

| 领域               | 参考项目                            | 主要借鉴                                    |
| ---------------- | ------------------------------- | --------------------------------------- |
| Realtime         | cactus-rt                       | RT thread、scheduler、affinity、statistics |
| RT data exchange | realtime_tools                  | RT/non-RT communication                 |
| Linux RT         | ros2-realtime-examples          | POSIX realtime 实践                       |
| CAN              | ros_canopen/socketcan_interface | SocketCAN abstraction                   |
| CAN              | CANopenNode                     | CAN hardware boundary                   |
| Serial           | wjwwood/serial                  | C++ API、timeout、configuration           |
| Serial           | libserialport                   | port lifecycle、I/O                      |
| Serial           | LibSerial                       | Linux termios                           |
| CANopen          | CANopenNode                     | CiA301 协议行为                             |
| CANopen          | Lely                            | Linux/C++ CANopen architecture          |
| CiA402           | ros_canopen                     | Master-side drive control               |
| CiA402           | ros2_canopen                    | modern integration / fake slave         |
| System runtime   | ros2_control                    | hardware runtime、read/update/write 思想   |
| Lifecycle        | ROS 2 Lifecycle                 | lifecycle 状态设计思想                        |

借鉴：

```text
标准行为
模块边界
生命周期思想
设备组织方式
实时工程实践
```

不复制：

```text
ROS dependency
Boost-heavy legacy
large async runtime
plugin framework
complex lifecycle framework
general controller framework
```

整个 RoboHardware 必须长期满足以下架构不变量：

1. Application 只依赖 RoboHardware 的 System、State、Command 和 Status。
2. Application 不直接依赖 CANopen、SocketCAN、Serial 细节。
3. System 负责配置、拓扑、生命周期、时钟、安全、状态和设备编排。
4. System 不成为机器人 Controller Framework。
5. System 只通过 Device 契约管理具体设备。
6. Device 契约不强迫所有设备具有相同执行周期。
7. Device 可以是 cyclic、asynchronous 或 event-driven。
8. Device 具体协议不得泄漏到 Application 数据接口。
9. Runtime topology 在初始化阶段确定，Runtime storage 在运行前冻结。
10. Application 只读取 System Runtime Snapshot，不直接读取 Device 私有 buffer。
11. 一个 cycle 内的 Runtime Snapshot 必须保持稳定，不被并发修改。
12. 不同 Device 可以拥有不同采样时间，必须保留 timestamp/freshness 语义。
13. Realtime 可以脱离 RoboHardware 单独使用。
14. CAN 可以脱离 CANopen 单独使用。
15. Serial 可以脱离 CAN 单独使用。
16. CANopen 只能依赖 CAN abstraction。
17. CiA402 属于 CANopen。
18. Real 与 Simulation 必须共享相同的 System/Device 边界。
19. 一个 System 可以同时运行 Real 与 Simulated Device。
20. Real/Simulation 是 per-device backend，不是全局开关。
21. 所有 control、timeout、freshness、Safety 时间统一使用 monotonic time domain。
22. wall clock 不参与实时控制计算。
23. Safety 位于 System 层。
24. Fast RT Safety 与 Non-RT Recovery 必须分离。
25. Error 可以逐层提升语义，但 root cause 不得丢失。
26. `System::stop()` 必须遵守 signal → safe exit → join → release。
27. 所有 execution context 完全退出后，才能释放其依赖资源。
28. 所有 RT 依赖资源必须在 `start()` 前构建完成，并在 RT thread 退出前保持有效。
29. System lifecycle 状态转换必须线程安全且 ownership 明确。
30. 每个模块必须提供结构化 Status/Stats。
31. RT 路径禁止阻塞日志、文件 I/O 和网络导出。
32. ROS2/ros2_control 只能作为上层适配，不反向污染核心。
33. V1 通过接口预留扩展点，但不提前实现 Factory/Registry/Plugin Framework。
34. 所有新增抽象必须由真实需求驱动。
35. V1 最高优先级始终是稳定的端到端硬件闭环。

最终整体架构：

```text
                 Application
         Robot / Controller / ROS2
                     │
          HardwareState / Command
                     │
                     ▼
              RoboHardware
                  System
       Config / Topology / Lifecycle
       Clock / Snapshot / Safety
       Status / Fault / Orchestration
                     │
                     ▼
                   Device
       lifecycle / capability / status
        Real / Simulation backend
      CANopen / CiA402 / Future Device
                     │
                     ▼
              Infrastructure
         Realtime / CAN / Serial
                     │
                     ▼
              Linux / Hardware
```

RoboHardware 的最终目标是：

> **构建一个克制、清晰、实时可控、支持异构设备、真实/仿真混合运行、运行时拓扑冻结、生命周期安全，并能够自然接入机器人应用与 ROS2/ros2_control 的 Linux C++ 硬件基础架构。**

