# RoboHardware 设计文档

## 1. 项目定位与原则

`robohardware` 是一个面向 Linux 机器人硬件开发的 C++17 基础项目，当前重点实现：

```text
Realtime
CAN
Serial
CANopen
CiA402
Simulation
```

第一阶段核心闭环为：

```text
CANopen Master
      ↓
    vcan
      ↓
CANopen Slave
      ↓
 CiA402 Slave
      ↓
Virtual Motor
```

项目不从零发明协议和实时框架，而采用：

> 阅读成熟开源实现 → 理解协议和工程语义 → 提取适合本项目的设计 → 使用现代 C++17 重新实现。

参考项目原则上是**设计和实现参考**，而不是运行时依赖。

公共 API 不绑定仓库名称，使用模块自己的 namespace：

```cpp
realtime::
can::
serial::
canopen::
canopen::cia402::
simulation::
```

这样各模块未来可以独立拆分和复用。

核心依赖方向：

```text
                 realtime
                    │
        ┌───────────┼───────────┐
        │           │           │
       CAN        Serial    Application
        │
        ▼
     CANopen
        │
        ▼
      CiA402
```

必须保持：

```text
CANopen → CAN
CiA402  → CANopen

Realtime ↛ CAN
Realtime ↛ Serial
Realtime ↛ CANopen

Serial ↛ CAN
CAN ↛ CANopen
```

目录层级保持克制，不增加 `io/`、`transport/`、`protocol/`、`runtime/` 等额外中间层。

---

## 2. 项目结构

```text
robohardware/
├── include/
│   ├── realtime/
│   │   ├── thread.hpp
│   │   ├── periodic_task.hpp
│   │   ├── scheduler.hpp
│   │   ├── affinity.hpp
│   │   ├── memory.hpp
│   │   ├── buffer.hpp
│   │   ├── queue.hpp
│   │   ├── clock.hpp
│   │   ├── statistics.hpp
│   │   └── error.hpp
│   │
│   ├── can/
│   │   ├── frame.hpp
│   │   ├── interface.hpp
│   │   ├── filter.hpp
│   │   └── socketcan.hpp
│   │
│   ├── serial/
│   │   ├── config.hpp
│   │   └── port.hpp
│   │
│   ├── canopen/
│   │   ├── types.hpp
│   │   ├── cob_id.hpp
│   │   ├── object.hpp
│   │   ├── object_dictionary.hpp
│   │   ├── device.hpp
│   │   ├── nmt.hpp
│   │   ├── sdo.hpp
│   │   ├── pdo.hpp
│   │   ├── heartbeat.hpp
│   │   ├── emcy.hpp
│   │   ├── sync.hpp
│   │   ├── master.hpp
│   │   ├── slave.hpp
│   │   │
│   │   └── cia402/
│   │       ├── objects.hpp
│   │       ├── state.hpp
│   │       ├── controlword.hpp
│   │       ├── statusword.hpp
│   │       ├── mode.hpp
│   │       ├── scaling.hpp
│   │       ├── master.hpp
│   │       └── slave.hpp
│   │
│   └── simulation/
│       ├── motor.hpp
│       └── fault.hpp
│
├── src/
│   ├── realtime/
│   ├── can/
│   ├── serial/
│   ├── canopen/
│   │   └── cia402/
│   └── simulation/
│
├── apps/
│   ├── canopen_master/
│   └── canopen_slave/
│
├── examples/
│   ├── realtime/
│   ├── can/
│   ├── serial/
│   └── canopen/
│
├── tests/
│   ├── unit/
│   ├── integration/
│   └── realtime/
│
├── config/
│   └── canopen/
├── tools/
├── scripts/
├── docs/
├── CMakeLists.txt
├── LICENSE
└── README.md
```

模块目录不代表一定要拆成独立动态库。

V1 应优先保持构建简单，避免一开始产生大量：

```text
libcan
libserial
libcanopen
libcia402
libsimulation
...
```

只要源码和公共 API 保持模块解耦，后续需要时再拆 CMake target。

---

## 3. 基础模块设计

### Realtime

`realtime` 是通用 Linux C++ 实时模块。

虽然当前位于 `robohardware` 仓库中，但设计目标是：

> 将 `include/realtime` 和 `src/realtime` 单独移动到其他 Linux C++ 项目中，也可以独立使用。

主要参考：

* `cactus-rt`
* `ros-controls/realtime_tools`
* `ros2-realtime-examples`

分别重点参考：

```text
cactus-rt
    → realtime thread
    → SCHED_FIFO
    → SCHED_DEADLINE
    → CPU affinity
    → runtime statistics

realtime_tools
    → RT / non-RT 数据交换
    → latest-value buffer
    → lock-free queue

ros2-realtime-examples
    → mlockall
    → scheduling
    → affinity
    → POSIX realtime 最小正确实现
```

模块职责：

```text
thread
periodic_task
scheduler
affinity
memory
buffer
queue
clock
statistics
error
```

`PeriodicTask` 是主要组合对象：

```cpp
realtime::PeriodicTask task({
    .period = 2ms,
    .priority = 80,
    .cpu = 3,
    .lock_memory = true,
});
```

内部基于：

```text
CLOCK_MONOTONIC
clock_nanosleep(TIMER_ABSTIME)
SCHED_FIFO
pthread_setaffinity_np
mlockall
```

`Buffer<T>` 表示 latest-value exchange：

```text
non-RT → RT
RT → non-RT
```

`Queue<T, N>` 表示不能丢失中间顺序的事件流。

Realtime 模块禁止依赖：

```text
CAN
Serial
CANopen
CiA402
ROS
ROS2
```

---

### CAN

`can` 是独立通用 CAN 模块。

纯 CAN 应用可以直接使用：

```cpp
#include <can/socketcan.hpp>
```

而完全不知道 CANopen 的存在。

主要参考：

* `ros-industrial/ros_canopen` 中的 `socketcan_interface`
* CANopenNode 的 CAN driver abstraction
* Linux SocketCAN API

重点借鉴：

```text
CAN Frame 与 Linux struct can_frame 解耦
SocketCAN 与 CANopen 解耦
CAN filter
error frame
CAN interface abstraction
```

但不继续沿用老式：

```text
Boost.Asio
Boost thread
strand
复杂 callback framework
```

Frame 建议：

```cpp
namespace can {

enum class FrameFormat {
    Classic,
    FD,
};

struct Frame {
    uint32_t id{};
    FrameFormat format{FrameFormat::Classic};
    uint8_t size{};
    std::array<uint8_t, 64> data{};
};

}
```

V1 只正式支持：

```text
Classic CAN
payload <= 8 bytes
```

但数据模型不阻塞未来 CAN FD。

CAN FD transport 与 CANopen FD 必须视为两个不同问题。

---

### Serial

`serial` 与 `can` 平级，是独立通用串口模块。

主要参考三个成熟项目：

* Serial
  `https://github.com/wjwwood/serial`
* libserialport
  `https://github.com/sigrokproject/libserialport`
* LibSerial
  `https://github.com/crayzeewulf/libserial`

参考重点不同。

`wjwwood/serial` 主要参考：

```text
现代 C++ Serial API
Port configuration
Timeout abstraction
read / write API
portable class design
```

它非常适合作为 C++ API 设计参考。

`libserialport` 主要参考：

```text
底层 serial port 生命周期
port enumeration
configuration
blocking/non-blocking I/O
error handling
跨平台底层实现方式
```

本项目当前只做 Linux，因此不需要照搬完整跨平台 abstraction，但可以借鉴其底层边界设计。

`LibSerial` 主要参考：

```text
Linux C++ serial abstraction
termios
baudrate
parity
character size
stop bits
flow control
stream-oriented API
```

RoboHardware 的 Serial V1 应保持很薄：

```text
serial/
├── config.hpp
└── port.hpp
```

例如：

```cpp
namespace serial {

enum class Parity {
    None,
    Odd,
    Even,
};

struct Config {
    uint32_t baud_rate{115200};
    uint8_t data_bits{8};
    uint8_t stop_bits{1};
    Parity parity{Parity::None};
};

class Port {
public:
    void open(const std::string& device, const Config& config);
    void close();

    std::size_t read(std::span<std::byte> buffer);
    std::size_t write(std::span<const std::byte> data);

    bool is_open() const noexcept;
    int native_handle() const noexcept;
};

}
```

底层优先采用：

```text
open
termios / termios2
read
write
poll / epoll
```

暂时不要增加：

```text
SerialManager
SerialDevice
SerialSession
SerialExecutor
SerialFactory
```

CAN 和 Serial 也不要强行继承统一：

```cpp
Transport
```

因为两者天然不同：

```text
CAN
    frame
    CAN ID
    DLC
    filter

Serial
    byte stream
    baudrate
    parity
    stop bits
    flow control
```

原则是：

> 复用 Linux 和 realtime 基础机制，不强行统一通信语义。

---

## 4. CANopen 与 CiA402

### CANopen

CANopen 实现 CiA301 核心协议。

依赖：

```text
CANopen
   ↓
 CAN
```

CANopen 层不直接接触：

```text
socket()
bind()
recvmsg()
termios
struct can_frame
```

主要参考：

### CANopenNode

作为**协议语义的第一参考**。

重点参考：

```text
Object Dictionary
NMT
SDO Client / Server
PDO
Heartbeat
EMCY
SYNC
LSS
```

尤其借鉴其按协议对象组织源码的方式。

因此 RoboHardware 采用：

```text
canopen/
├── object_dictionary
├── nmt
├── sdo
├── pdo
├── heartbeat
├── emcy
├── sync
├── master
└── slave
```

而不是：

```text
master/sdo
slave/sdo
master/pdo
slave/pdo
```

两套目录机械复制。

### Lely CANopen

作为 Linux/C++ CANopen 架构的重要参考。

重点参考：

```text
liblely-can
    ↓
liblely-co
    ↓
liblely-coapp
```

即：

```text
CAN transport
CANopen protocol
CANopen application abstraction
```

同时重点参考：

* Device / Object Dictionary 中心模型；
* Client SDO / Server SDO 分离；
* RPDO / TPDO；
* protocol core 与 application orchestration 分离。

但不复制 Lely 完整的：

```text
ev
io2
executor
future
async framework
```

### ros_canopen

主要参考 Master 侧：

```text
socketcan_interface
        ↓
canopen_master
        ↓
canopen_402
```

这种分层关系。

---

### Object Dictionary

Object Dictionary 是 CANopen 的唯一核心数据事实来源。

推荐：

```text
               ObjectDictionary
                     │
          ┌──────────┼──────────┐
          ↓          ↓          ↓
         SDO        PDO      Application
```

Slave 中：

```text
CAN frame
    ↓
SDO / PDO
    ↓
ObjectDictionary
    ↓
CiA402
```

不能出现：

```text
SDO 保存一份值
PDO 保存一份值
CiA402 又保存一份值
```

这部分主要参考：

```text
CANopenNode OD interface
Lely device / object model
```

---

### Master / Slave

`Master` 和 `Slave` 是角色聚合器，不重新实现第二套协议。

例如：

```cpp
namespace canopen {

class Master {
public:
    NmtMaster& nmt();
    SdoClient& sdo();
    HeartbeatConsumer& heartbeat();
    EmergencyConsumer& emcy();
    SyncProducer& sync();
};

class Slave {
public:
    ObjectDictionary& dictionary();

    NmtSlave& nmt();
    SdoServer& sdo();
    HeartbeatProducer& heartbeat();
    EmergencyProducer& emcy();
    SyncConsumer& sync();
};

}
```

---

### CiA402

CiA402 属于 CANopen：

```text
canopen/
└── cia402/
```

namespace：

```cpp
canopen::cia402
```

主要参考：

* `ros_canopen/canopen_402`
* `ros2_canopen/canopen_402_driver`
* `ros2_canopen/canopen_fake_slaves`

Master 重点参考：

```text
Statusword decoding
PDS state interpretation
Controlword transition
operation mode
drive abstraction
```

Slave 重点参考：

```text
Controlword
    ↓
PDS FSA
    ↓
Statusword
```

CiA402 Slave 是状态机真实所有者。

Master 不能散落：

```cpp
0x06
0x07
0x0F
0x80
```

这类 magic controlword。

统一通过：

```text
State
Transition
Controlword
```

表达。

目录：

```text
canopen/cia402/
├── objects.hpp
├── state.hpp
├── controlword.hpp
├── statusword.hpp
├── mode.hpp
├── scaling.hpp
├── master.hpp
└── slave.hpp
```

---

## 5. Simulation 与实时运行

`simulation` 只负责模拟物理设备行为。

关系：

```text
canopen::Slave
      ↓
canopen::cia402::Slave
      ↓
simulation::Motor
```

`simulation::Motor` 不知道：

```text
CAN
COB-ID
PDO
SDO
NMT
0x6040
0x6041
```

只处理：

```text
position
velocity
torque
limits
fault
```

例如：

```cpp
namespace simulation {

class Motor {
public:
    void set_position_target(double value);
    void set_velocity_target(double value);
    void set_torque_target(double value);

    void update(std::chrono::nanoseconds dt);

    MotorState state() const;
};

}
```

未来可以扩展：

```text
IdealMotor
FirstOrderMotor
LimitedMotor
FaultMotor
MuJoCoMotor
```

而不修改 CANopen。

---

目标 CANopen 场景：

```text
7 joints
500 Hz control loop
Classic CAN first
```

推荐运行模型：

```text
CAN RX thread
    ↓
TPDO decode
    ↓
Realtime Buffer
```

```text
500 Hz realtime thread
    ↓
latest command
latest state
    ↓
CiA402 cyclic update
    ↓
RPDO
SYNC
```

```text
non-RT control thread
    ↓
NMT
SDO
Heartbeat
EMCY
configuration
fault recovery
```

Realtime thread 禁止执行：

```text
SDO
EDS parsing
filesystem I/O
formatted logging
dynamic allocation
blocking mutex
NMT reset
configuration parsing
```

500 Hz 表示控制周期：

```text
2 ms
```

不代表每个关节所有 PDO 都必须 500 Hz。

对于 7 关节 Classic CAN：

```text
7 × 2 × 500
=
7000 frames/s
```

如果每个关节每周期都发送 1 RPDO + 1 TPDO，总线利用率会非常高。

因此必须进一步设计：

```text
PDO packing
command frequency
feedback frequency
SYNC strategy
bus load measurement
```

---

## 6. 参考基线、实现阶段与边界

各模块参考关系统一如下：

| 模块                       | 主要参考                            | 重点借鉴                          |
| ------------------------ | ------------------------------- | ----------------------------- |
| `realtime`               | cactus-rt                       | realtime thread、调度、统计         |
| `realtime::Buffer/Queue` | realtime_tools                  | RT/non-RT 数据交换                |
| POSIX realtime           | ros2-realtime-examples          | memory、scheduler、affinity     |
| `can`                    | ros_canopen/socketcan_interface | SocketCAN abstraction         |
| `can`                    | CANopenNode driver              | CAN hardware boundary         |
| `serial`                 | wjwwood/serial                  | C++ API、timeout、配置            |
| `serial`                 | libserialport                   | port lifecycle、底层 I/O         |
| `serial`                 | LibSerial                       | Linux termios C++ abstraction |
| Object Dictionary        | CANopenNode                     | OD interface                  |
| CANopen protocol         | CANopenNode                     | NMT/SDO/PDO/HB/EMCY/SYNC      |
| CANopen architecture     | Lely                            | Device、OD、core/app 分层         |
| CANopen Master           | Lely / ros_canopen              | Master orchestration          |
| CANopen Slave            | CANopenNode / Lely              | Device-side protocol          |
| CiA402 Master            | ros_canopen                     | PDS transition                |
| CiA402 modern design     | ros2_canopen                    | modern driver structure       |
| CiA402 Slave             | ros2_canopen fake slaves        | Slave behavior                |
| Simulation               | fake slaves + 自研                | 虚拟设备行为                        |

开发时原则不是：

```text
看文档后自己重新设计
```

而是：

```text
找到对应成熟实现
        ↓
分析其协议行为和边界
        ↓
确认哪些设计仍适合现代 Linux/C++17
        ↓
去除历史依赖和过度框架
        ↓
在 RoboHardware 中重新实现
        ↓
使用测试验证行为等价
```

推荐实现顺序：

```text
Phase 1
Realtime basic + CAN + vcan

Phase 2
NMT Master / Slave

Phase 3
Object Dictionary + SDO Client / Server

Phase 4
Heartbeat

Phase 5
CiA402 common + Master / Slave state machine

Phase 6
PDO

Phase 7
Profile Position / Velocity / Torque

Phase 8
500 Hz realtime + SYNC + CSP / CSV / CST

Phase 9
EMCY + fault / recovery

Phase 10
Serial + EDS / DCF / configuration tooling
```

Serial 如果实际业务提前需要，可以独立于 CANopen 提前实现，不需要等待 Phase 10。

测试始终围绕：

```text
Master
  ↓
vcan0
  ↓
Slave
```

逐步覆盖：

```text
boot-up
NMT
SDO
PDO
Heartbeat
CiA402 enable
position
velocity
torque
fault
recovery
```

Realtime 单独验证：

```text
500 Hz
2 ms period
deadline miss
wake-up jitter
execution time
```

普通 Ubuntu 用于功能验证。

PREEMPT_RT 用于正式实时指标验证。

V1 明确不做：

```text
完整 ROS2 integration
ros2_control plugin
通用 event framework
plugin framework
跨平台硬件抽象
MCU portability
CANopen FD
完整 EDS/XDD editor
复杂 DCF generator
大型 async runtime
```

RoboHardware 当前最终目标应保持为：

```text
模块独立
边界清晰
协议正确
实时可测
Master / Slave 可闭环
CAN / Serial 可独立使用
后续容易接入 ROS2 / ros2_control
```

核心开发方法总结为：

> **CANopenNode 学协议，Lely 学 CANopen 架构，ros_canopen 学 Master/CiA402 分层，ros2_canopen 学现代机器人集成，cactus-rt / realtime_tools / ros2-realtime-examples 学实时工程，wjwwood/serial / libserialport / LibSerial 学串口设计。RoboHardware 在这些成熟实现基础上进行轻量化、现代 C++17 化和机器人场景适配，而不是从头重新设计。**
