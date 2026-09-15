# robo-hardware 项目章程

状态：已基线化

阶段：项目定位与 V1 需求定义

最后更新：2026-09-15

## 1. 项目定位

robo-hardware 是面向 Linux 机器人与工业设备驱动开发的现代 C++17
低层基础组件库族，提供：

- 确定性执行基础；
- RT/NRT 数据交换原语；
- UART/RS-485 transport；
- Linux spidev SPI transport；
- SocketCAN RAW transport；
- 未来建立在 transport 之上的协议组件。

项目使用单一 root build、install 和 CMake package（`hardware`），并生成多个可独立
链接的动态库。它不依赖 ROS，也不包含设备业务逻辑。

“库族”表示统一发布的多个职责模块，不表示共享 runtime、HAL 或必须整体链接的单体库。
`robo-hardware` 仅是 repository 身份；共享 public vocabulary 为 `hardware`，而模块保留
自己的 namespace、include root 和 CMake target。例如 Realtime 为 `realtime`、
`<realtime/...>`、`realtime::realtime`，但消费者统一使用 `find_package(hardware)`。

本章程 supersedes 早期的 independent-component / standalone-package 约束；模块不得再
维护独立 build、install、package 或 version 边界。

## 2. 项目不是什么

robo-hardware 不是：

- HAL（Hardware Abstraction Layer）；
- Driver Framework；
- Hardware Runtime；
- 完整机器人框架；
- 设备驱动集合；
- 跨平台硬件兼容层；
- ROS 或 ROS 2 的替代品。

项目的准确描述是：**低层驱动开发基础组件集合**。

## 3. 要解决的问题

机器人和工业控制项目经常重复实现实时线程配置、UART/CAN 通信、超时、
错误处理和测试设施。这些实现通常与业务逻辑、ROS 或具体设备绑定，造成：

- 底层通信代码难以复用；
- 生命周期和错误语义不一致；
- 实时行为缺少明确契约和测量证据；
- 软件仿真和硬件验证体系薄弱；
- 不同项目反复承担相同的系统编程成本。

robo-hardware 只解决这些可复用的低层问题，不接管设备策略和应用架构。

## 4. 目标用户

首要用户是在普通 Linux、嵌入式 Linux 或 PREEMPT_RT 环境中编写机器人及
工业设备驱动的 C++ 工程师。

典型使用模型：

```text
NRT application thread
        |
        | realtime::Queue<T, Capacity>
        | realtime::Buffer<T>
        v
RT driver thread
        |
        | exclusive ownership
        v
Serial / SPI / CAN handle
        |
        v
Hardware
```

NRT 线程通过数据交换原语发送 command、event 或配置，并读取最新 state、
status 或 telemetry。RT 驱动线程独占硬件 I/O 句柄。

## 5. 长期目标

1. 提供稳定、清晰、可复用的低层驱动开发组件。
2. 使组件之间保持低耦合，并可独立采用。
3. 用明确契约代替隐藏线程、隐藏状态和隐式策略。
4. 在 Linux 和 PREEMPT_RT 上提供可测量的确定性执行基础。
5. 建立从单元测试、行为仿真到真实硬件验证的证据链。
6. 在 transport 稳定后，以独立里程碑逐步增加 CANopen 能力。

## 6. 设计原则

### 6.1 明确而窄小的职责

- transport 负责链路配置和数据传输；
- protocol 负责协议语义；
- device driver 负责具体设备行为；
- application 负责业务策略。

上层职责不得下沉到下层组件。

### 6.2 最小抽象

不因多个组件位于同一仓库，就创建统一 `Device`、`Transport`、`Manager`
继承体系或公共运行时。只有经过真实重复需求证明后，才提取公共组件。

### 6.3 显式资源和线程所有权

- 资源使用 RAII 和 move-only ownership；
- 一个 Serial/SPI/CAN 连接句柄由一个线程独占使用；
- 同一句柄不保证并发调用安全；
- 库内部不使用隐藏 mutex 或后台线程；
- move、close 和 destroy 必须满足线程所有权约束。

### 6.4 可验证的实时声明

项目提供构建确定性任务所需的机制、契约和测量工具，但不保证应用必然满足
deadline，也不对不同硬件和内核环境承诺统一延迟上限。

`lock-free` 不等于 `realtime-safe`。项目只声明已经由实现、目标平台约束和
验证证据共同支持的性质。

### 6.5 无隐藏策略

自动重连、自动 bus-off 恢复、自动重试、设备发现和协议状态机等策略不属于
transport。调用者必须能够观察失败并显式决定恢复方式。

## 7. 概念结构与依赖

```text
Foundation
|-- deterministic execution
`-- RT/NRT data exchange primitives
    |-- Queue<T, Capacity>
    `-- Buffer<T>

Transport Components
|-- serial
|   `-- UART / RS-485 transport
|-- spi
|   `-- Linux spidev transport
`-- can
    `-- SocketCAN RAW transport

Protocol Components (future)
`-- canopen
    `-- depends on can
```

V1 项目内依赖必须保持为：

```text
realtime    no project dependency
serial      no project dependency
spi         no project dependency
can         no project dependency
```

未来允许：

```text
canopen --> can
```

`serial`、`spi` 和 `can` 不得依赖 `realtime`。应用可以组合组件，但组合使用不构成
库之间的依赖。

V1 不建立 `core`、`common`、`platform` 或 `hardware` runtime library。`hardware` 只提供
header-only 的共享 API vocabulary；这不改变模块之间没有非必要 runtime dependency 的原则。

## 8. 平台与兼容性方向

- 语言标准：C++17；
- 操作系统：Linux；
- 目标环境：普通 Linux、嵌入式 Linux、PREEMPT_RT；
- 目标架构：首先覆盖 x86_64 和 aarch64；
- 编译器：GCC 和 Clang；
- 不支持 Windows、macOS、裸机和 MCU RTOS。
- 项目许可证：Apache License 2.0。

最低 Linux、编译器和 CMake 版本由首个 CI 矩阵冻结。

项目首先通过多个 `0.x` 版本验证设计。`1.0` 才开始公共源码 API 兼容承诺。
`1.0` 前不承诺 ABI 稳定，`1.0` 后是否承诺 ABI 稳定需要单独决策。

## 9. 验证等级

### Level 1：确定性单元测试

验证内部状态逻辑、生命周期、超时计算、错误映射和数据结构。

### Level 2：协议和设备行为仿真测试

每个通信组件尽可能拥有独立仿真端。仿真端模拟设备行为，并通过生产 I/O
路径连接被测 component；PTY 和 `vcan` 是传输媒介，不是仿真本身。

仿真端是一等测试资产，但不是 V1 对外承诺的通用仿真框架或产品功能。

### Level 3：真实环境验证

使用 PREEMPT_RT 机器、真实 UART/RS-485 设备、USB-CAN/CAN FD 设备和未来的
工业设备，验证性能、长时间稳定性和硬件兼容性。

仿真可以证明指定软件行为正确，但不能替代真实硬件验证。

## 10. V1 成功标准

V1 成功由可验证工程结果定义，而不是组件数量、协议覆盖率或社区指标。

必须满足：

- root configure/build/install、`hardware` package 和模块独立链接均通过；
- 每个组件具备公开 Interface 契约、最小示例和 Level 1/2 测试；
- 在 PREEMPT_RT 上建立可重复测量流程并输出延迟、抖动和执行时间报告；
- 至少一种真实 Serial/RS-485 设备完成 Level 3 验证；
- 至少一种真实 CAN/CAN FD 设备完成 Level 3 验证；
- 至少一个真实驱动组合使用 `realtime + serial` 或 `realtime + can`；
- 安装后的消费者测试可通过 `find_package(hardware)` 使用每个模块 target；
- 真实项目使用期间无未解决的数据竞争、资源泄漏或生命周期缺陷。

CANopen 不属于 V1 成功条件。

## 11. 长期方向

CANopen 是 transport 稳定后的独立协议里程碑。开发顺序应由仿真和真实设备
驱动，候选顺序为 NMT、boot-up、heartbeat、EMCY、SDO、PDO、SYNC、EDS/DCF
和 CiA 402。

CANopen 组件不得在实现和验证能力不足时创建只有接口的占位组件，也不得在
真实设备互操作测试前标记为 production-ready。

UDP、EtherCAT、EtherNet/IP、Modbus 或其他能力不因概念结构中存在可放置位置而自动进入
路线图。每项扩展都需要独立的真实需求和范围评审。

## 12. 当前非目标

- ROS/ROS 2 集成；
- 完整机器人框架和应用运行时；
- 具体厂商设备驱动；
- UI 和高层控制算法；
- 设备业务逻辑；
- 通用异步 I/O runtime；
- 通用 lock-free 容器库；
- 跨平台 HAL；
- 安全认证或硬实时认证。
