# robo-hardware V1 需求规格

状态：草案，需求边界已基线化，公共 API 尚未设计

最后更新：2026-09-12

## 1. 文档目的

本文定义 robo-hardware 第一阶段可交付范围和验收要求。本文中的“V1”表示
第一阶段能力集合，不表示首个发布版本必须命名为 `1.0.0`。

需求使用以下关键词：

- **必须**：V1 验收所必需；
- **应该**：正常情况下需要满足，偏离时必须记录理由；
- **可以**：允许但不是 V1 验收条件；
- **不得**：明确排除或禁止。

## 2. V1 范围

V1 包含三个独立分发组件，每个组件对应一个 production library：

1. `realtime`：确定性执行基础和 RT/NRT 数据交换原语；
2. `serial`：Linux UART/RS-485 transport；
3. `can`：Linux SocketCAN RAW transport。

V1 不包含 CANopen。CANopen 必须作为 transport 稳定后的独立里程碑设计和
验证。

## 3. 跨组件要求

### 3.1 构建与交付

每个组件必须：

- 能够独立构建；
- 能够独立安装；
- 导出独立的 CMake target；
- 被安装树之外的最小消费者项目发现并链接；
- 不强制链接其他 robo-hardware component；
- 提供最小可运行示例；
- 提供组件级 Interface 契约文档。

V1 不得建立所有组件共同依赖的 `core`、`common` 或 `platform` 库。

### 3.2 平台

- 所有公共代码必须符合 C++17；
- V1 必须支持 Linux；
- CI 必须至少覆盖 GCC 和 Clang；
- 首个 CI 建立时必须冻结最低编译器、Linux 和 CMake 版本；
- V1 不得因跨平台目标增加 backend 抽象。

### 3.3 错误处理

- 可预期运行时失败必须通过值返回表达；
- 系统错误必须尽可能保留为 `std::error_code`；
- 超时、设备断开、总线错误和“不支持”不得使用异常作为正常控制流；
- 实时线程可调用的 API 必须保证不抛异常；
- V1 不得引入公共 `Result<T>` 或第三方 `expected` 类型；
- 当操作需要同时返回数据和结束原因时，必须定义组件专用的窄小结果结构。

是否允许异常表达编程错误或初始化阶段不可恢复错误，仍需在 API 设计前决定。

### 3.4 时间与超时

- 所有等待超时必须基于 monotonic clock；
- 超时必须是单次操作参数，不得保存为连接对象的可变全局状态；
- API 必须明确区分无限等待、立即尝试和有限等待；
- 有限等待必须采用整体 deadline 语义；
- `EINTR`、虚假唤醒和部分传输不得重新开始完整超时；
- API 不得使用含糊的默认 timeout 或多个相互作用的对象级 timeout 字段。

### 3.5 线程与资源所有权

- Serial/CAN 连接句柄必须是 move-only；
- 一个连接句柄必须由一个线程独占使用；
- 同一句柄不保证并发调用安全；
- 库不得通过隐藏 mutex 提供表面线程安全；
- transport 不得启动后台线程；
- move、close 和 destroy 必须在无并发访问并符合所有权契约时执行；
- 多个独立句柄可以由不同线程使用；
- 多句柄访问同一底层设备的行为由 OS driver 和硬件决定。

## 4. Realtime V1

### 4.1 定位

`realtime` 提供确定性执行基础和用途受限的 RT/NRT 数据交换原语。它不是
executor、runtime 或通用并发容器库。

### 4.2 确定性执行基础

V1 必须提供：

- monotonic clock 访问；
- 基于绝对时间的周期等待；
- 周期计划及 missed-period 检测；
- 明确且可选择的 missed-period 处理语义；
- 当前线程调度策略和优先级配置；
- CPU affinity 配置；
- 进程内存锁定及失败诊断；
- 周期执行时间、延迟、抖动和超期统计；
- 每个公共操作的 realtime-safety 契约。

V1 不保证应用满足 deadline，也不对不同机器给出统一最大延迟承诺。

### 4.3 `Queue<T, Capacity>`

`Queue<T, Capacity>` 必须满足：

- 单生产者、单消费者；
- 有界 FIFO；
- 容量在编译期固定；
- 对象构造及运行期均不进行动态内存分配；
- push 和 pop 不等待另一端；
- 满时写入失败；
- 空时读取失败；
- 不隐藏 mutex；
- 顺序、可见性和所有权转移语义必须在 C++ memory model 下明确记录。

其目标用途是 command、event、frame 等顺序敏感数据。

### 4.4 `Buffer<T>`

`Buffer<T>` 必须满足：

- 单写者、单读者；
- latest-value 语义；
- 读取获得一个完整一致的数据视图；
- 允许跳过中间更新；
- 写入不等待读取；
- 读取不等待写入；
- 构造及运行期均不进行动态内存分配；
- 不隐藏 mutex；
- 公共 API 不限定 double buffer、triple buffer、ring buffer 或原子交换等实现。

其目标用途是 state、status、telemetry 等只关心最新值的数据。

### 4.5 数据类型与并发保证

- V1 不支持动态大小 payload；
- payload 必须具有固定存储和可证明的有界复制行为；
- 库不保证用户类型自身的复制成本；
- 允许的 `T` 的精确类型约束必须在 API 设计前冻结；
- 使用的原子类型必须在支持平台上证明为 lock-free，必要时通过编译期或运行期
  平台约束拒绝不满足要求的实现；
- 在完成算法审查和验证前，不得宣传 `Queue` 或 `Buffer` 为 wait-free、
  lock-free 或 realtime-safe；
- 测试通过不得被当作并发算法正确性的唯一证明。

### 4.6 Realtime 排除项

V1 不提供：

- executor、thread pool、task graph；
- 通用 lock-free 容器集合；
- realtime publisher；
- 实时日志或 tracing runtime；
- 自定义动态内存分配器；
- 自动系统或内核调优；
- SCHED_DEADLINE 等未经真实需求驱动的高级调度框架。

## 5. Serial V1

### 5.1 定位

`serial` 是 Linux UART/RS-485 字节 transport。它负责链路配置和字节传输，
不负责数据帧、设备协议或业务语义。

### 5.2 生命周期与配置

V1 必须支持：

- 按设备路径打开；
- 显式关闭和 RAII 清理；
- move-only ownership；
- 波特率、数据位、停止位、校验和流控配置；
- 清晰报告无效配置、权限错误、设备不存在和 driver 不支持。

### 5.3 通信

V1 必须支持：

- blocking read/write；
- immediate/non-blocking attempt；
- 带整体 deadline 的 read/write；
- 部分读取和部分写入；
- 同时表达已传输字节数和停止原因；
- flush；
- drain/send-complete；
- 设备断开及系统调用失败的明确错误。

`write` 被内核接受不得被描述为物理发送完成。物理发送完成语义必须通过
独立且明确的 drain/send-complete 操作表达。

### 5.4 RS-485

V1 必须支持 Linux UART driver 提供的：

- RS-485 mode configuration；
- 内核/driver 管理的半双工方向控制；
- RTS/DE 相关配置能力的封装；
- 检测 RS-485 配置 ioctl 是否受支持；
- 设置后返回值或配置回读验证；
- 识别 driver 清理或拒绝的不支持配置；
- 不支持配置的明确错误；
- 发送完成后的 drain 语义。

V1 不得：

- 在 Serial 内部管理外部 GPIO DE/RE；
- 提供应用层手动切换方向的统一抽象；
- 封装特定 USB-RS-485 芯片的私有行为；
- 静默忽略或降级 driver 不支持的配置。

### 5.5 Serial 排除项

V1 不提供：

- Modbus RTU 或私有设备协议；
- 数据帧解析、编解码器或消息路由；
- 自动重试和自动重连；
- 设备发现、枚举和热插拔监控；
- 后台 I/O 线程；
- callback、future 或 coroutine API；
- 跨平台串口；
- 厂商硬件 workaround。

## 6. CAN V1

### 6.1 定位

`can` 是 Linux SocketCAN RAW frame transport。它负责 CAN frame transport，
不负责系统网络配置、上层协议和设备恢复策略。

### 6.2 Frame model

V1 必须显式表示并验证：

- Classical CAN；
- CAN FD；
- standard identifier；
- extended identifier；
- Classical CAN RTR frame；
- error frame；
- CAN FD flags；
- payload length；
- Classical CAN 与 CAN FD 各自的非法组合。

公共 frame 类型采用分离模型还是带类别的统一模型，必须在 API 原型阶段决定。

### 6.3 生命周期与通信

V1 必须支持：

- 按 Linux network interface 打开和 bind；
- 显式关闭和 RAII 清理；
- move-only ownership；
- blocking send/receive；
- immediate/non-blocking attempt；
- 带整体 deadline 的 send/receive；
- 完整 frame 语义；
- CAN FD 能力检测和明确的“不支持”错误。

CAN frame 必须完整收发。不得向用户暴露有效的“部分 CAN frame”结果。

### 6.4 Filtering

V1 必须支持：

- 单个 identifier/mask filter；
- 多个 filter；
- SocketCAN 默认的多 filter OR 语义；
- 原生 inverted filter；
- 禁用接收过滤。

V1 不提供通用 `FilterExpression`。`CAN_RAW_JOIN_FILTERS` 的 AND 语义是已知但
有意延后的 Linux 原生能力。

### 6.5 Socket options 与接收元数据

V1 必须支持：

- local loopback；
- receive-own-messages；
- error frame reception 和 error mask；
- 带明确时钟来源的接收软件时间戳；
- 内核返回的本地来源和发送确认标志。

“timestamp”不得作为未指定来源的单一布尔能力。V1 不包含硬件 timestamp、
transmit timestamp 或 completion timestamp。

### 6.6 Diagnostics

V1 必须能够接收并解析 Linux CAN error message frames，并保留原始错误类别和
诊断数据。

错误帧是驱动可选上报的事件，不是当前 controller/bus state 的同步查询接口。
V1 不得声称可以通过 RAW socket 完整查询控制器状态，也不得自动执行恢复。

### 6.7 CAN 排除项

V1 不提供：

- bitrate、sample point、interface up/down 配置；
- netlink 管理和 controller state 查询；
- 自动 bus-off recovery；
- CAN BCM；
- ISO-TP、J1939、CANopen；
- gateway/router；
- 厂商 CAN SDK；
- 后台接收线程或 dispatcher；
- 跨平台 CAN backend；
- `CAN_RAW_JOIN_FILTERS` 和过滤表达式系统；
- 硬件、发送或 completion timestamp。

## 7. 测试与验证需求

### 7.1 Level 1：确定性单元测试

每个组件必须覆盖：

- 内部状态逻辑；
- 生命周期和 move 行为；
- 超时和 deadline 计算；
- 错误映射；
- 数据结构及边界条件；
- 无效输入和失败路径。

Realtime 并发原语还必须进行 wrap-around、满/空竞争、长时间压力和内存顺序
验证。测试必须配合算法审查，不能替代正确性论证。

### 7.2 Level 2：行为仿真测试

#### Serial

必须提供通过 PTY 与生产 Serial 实现通信的模拟固件，覆盖：

- 正常数据收发；
- 分包和粘包对上层测试协议的影响；
- 部分传输；
- 超时；
- 设备断开；
- 异常响应；
- 不支持的配置和错误路径。

模拟固件可以使用测试协议构造设备行为，但该协议不得进入 Serial 生产 API。

#### CAN

必须提供通过 `vcan` 与生产 SocketCAN 实现通信的软件 CAN 节点，覆盖：

- 正常 frame 收发；
- 周期发送；
- request/response；
- Classical CAN 与 CAN FD；
- filter、loopback 和 receive-own-messages；
- 可在虚拟环境可靠构造的错误路径。

`can-utils` 可以作为交叉验证工具，但不得成为生产运行时依赖。

### 7.3 Level 3：真实环境验证

必须建立可重复的验证记录，至少覆盖：

- 一台 PREEMPT_RT 机器；
- 一种真实 Serial/RS-485 设备；
- 一种真实 CAN/CAN FD 设备；
- 一个组合使用 `realtime + serial` 或 `realtime + can` 的真实驱动。

验证记录必须注明内核、硬件、driver、连接方式、负载、测试时长和结果。

未经 Level 3 验证的能力不得标记为 `hardware-validated`。Level 3 通过也只证明
记录环境中的结果，不构成跨环境性能或兼容性保证。

## 8. V1 Definition of Done

V1 在以下条件全部满足时完成：

1. 三个组件满足独立构建、安装、链接和消费者测试；
2. 所有必须能力均有公共契约和最小示例；
3. Level 1 和 Level 2 测试稳定通过；
4. Realtime 建立 PREEMPT_RT 可重复测量流程和报告；
5. Serial/RS-485 与 CAN/CAN FD 各完成至少一个 Level 3 验证；
6. 至少一个真实驱动完成双组件集成验证；
7. 无已知未解决的数据竞争、资源泄漏或严重生命周期缺陷；
8. 所有未完成或未验证能力均被明确标注，而不是隐含宣称支持。

## 9. 路线图

### Phase 0：章程与需求

冻结定位、术语、组件边界、非目标、验证等级和 V1 Definition of Done。

### Phase 1：工程基线

建立独立 CMake targets、安装导出、消费者测试、CI、静态检查、sanitizer、
文档和示例结构。

### Phase 2：Realtime

先实现时钟、周期、线程配置和测量，再实现并严格验证 `Queue` 与 `Buffer`。

### Phase 3：首个通信纵向切片

根据最先可用的真实硬件选择 `realtime + serial` 或 `realtime + can`，同时建立
对应 Level 2 仿真端和最小真实驱动。

### Phase 4：第二个通信组件

完成剩余 transport、Level 2 仿真和 Level 3 硬件验证。

### Phase 5：稳定化

通过真实项目反馈收敛 API。先发布 `0.x`，满足兼容承诺条件后再发布 `1.0`。

### Phase 6：CANopen

CANopen 先建立 slave/CiA 402 仿真，再按独立需求规格逐步实现和验证。不得把
CANopen 接口骨架作为 V1 完整性的替代品。

## 10. API 设计前的开放决策

以下事项必须在具体公共类和函数设计前解决：

- 最低 Linux、GCC、Clang 和 CMake 版本；
- 编程错误与初始化失败是否允许异常；
- `Queue`/`Buffer` 的精确类型约束；
- `Buffer` 第一次写入前的读取语义；
- 并发原语的算法、原子能力门槛和验证方法；
- duration 与 absolute time point 的公共 deadline 表达；
- Serial 部分传输结果结构和停止原因集合；
- drain/send-complete 的有限等待与取消语义；
- RS-485 首批验证 driver 和硬件；
- Classical CAN/CAN FD 公共 frame model；
- 接收软件时间戳的 clock domain 和返回表示；
- 首批 CAN/CAN FD 硬件和 PREEMPT_RT 验证环境。

这些开放决策不得通过实现细节被静默冻结。

## 11. 参考边界

- Linux SocketCAN：<https://www.kernel.org/doc/html/latest/networking/can.html>
- Linux timestamping：<https://docs.kernel.org/networking/timestamping.html>
- Linux RS-485：<https://docs.kernel.org/driver-api/serial/serial-rs485.html>
- cactus-rt：<https://github.com/cactusdynamics/cactus-rt>
- ros-controls/realtime_tools：<https://github.com/ros-controls/realtime_tools>
- can-utils：<https://github.com/linux-can/can-utils>
- Lely CANopen：<https://opensource.lely.com/canopen/>
- CANopenNode：<https://github.com/CANopenNode/CANopenNode>
- python-canopen：<https://github.com/canopen-python/canopen>

参考项目用于理解行业实践和建立测试依据，不构成复制其架构的要求。
