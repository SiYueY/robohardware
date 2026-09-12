# Serial Interface Design

状态：已冻结

阶段：Serial API Design

最后更新：2026-09-12

## 1. 目的与身份

本文冻结 `serial` V1 的公开 Interface 及调用契约：资源所有权、生命周期、TTY 配置、
RS-485、字节传输、timeout、错误和线程边界。

`serial` 是独立的 Linux C++17 TTY byte-stream transport library。它不设计协议、设备
行为、异步运行时或硬件抽象。

```text
include root: <serial/...>
namespace:    serial
CMake target: serial::serial
package:      serial
```

`serial` 不依赖 `realtime`、`can` 或未来的 `canopen`。

## 2. 范围与排除项

V1 只负责 Linux TTY 的 fd 生命周期、raw binary UART 配置、Linux UART driver 支持的
RS-485 配置及验证、同步 byte-stream I/O、操作级 timeout、flush 与 drain。

V1 不提供：

- Modbus、私有协议、frame/packet 解析、编解码、消息路由或 `read_exactly()`；
- 自动重试、自动重连、设备发现、热插拔监控或设备能力数据库；
- callback、future、coroutine、后台 I/O 线程或其他 async runtime；
- external GPIO DE/RE 控制、手动方向控制、USB-RS-485 workaround；
- fd adopt/borrow、`native_handle()`、任意 fd transport 或跨平台 backend；
- 任意 termios/ioctl flag、modem-control API、runtime reconfiguration；
- 隐式 flush、恢复、重定相或 device policy。

## 3. 公共约定

### 3.1 无异常和错误

全部 public constructors 与 public operations 都是 `noexcept`。可预期失败通过
`std::error_code` 或包含它的领域结果返回；库不以 exception、log、abort 或 hidden
allocation 替代错误报告。

所有错误以 `std::error_code` 表达：

- `std::errc` / generic category：`invalid_argument`、`timed_out`、
  `inappropriate_io_control_operation`（non-TTY）、
  `operation_not_supported` 等通用契约错误；
- system category：实际 Linux syscall/driver 错误；
- `serial` category：仅表达 component-local 状态和配置错误。

```cpp
namespace serial {

enum class Error {
  PortAlreadyOpen,
  PortNotOpen,
  DeviceDisconnected,
  UnsupportedConfiguration,
  ConfigurationMismatch,
};

std::error_code make_error_code(Error error) noexcept;

}  // namespace serial
```

`Error` 不替代 `std::error_code`，且不得收录 timeout、permission、not-a-TTY 或
invalid argument。`DeviceDisconnected` 只表示 TTY 已无法继续提供有效 byte stream；
它不表示 timeout、parity error、framing error 或 break event。

### 3.2 所有权与并发

一个 `Port` 由一个线程独占。同一对象的 `open`、`close`、move、`read`、`write`、
`flush`、`drain` 和 `is_open` 均不提供并发调用安全保证。调用者必须在 move、close
或 destroy 前建立外部同步。

库不使用 hidden mutex，不创建 worker thread。不同 `Port` 可由不同线程使用；多个
Port 指向同一底层设备时的行为由 Linux driver 和硬件决定。

## 4. Timeout

```cpp
namespace serial {

class Timeout final {
 public:
  Timeout() = delete;

  static Timeout immediate() noexcept;
  static Timeout infinite() noexcept;
  static Timeout after(std::chrono::nanoseconds duration) noexcept;

  [[nodiscard]] bool is_valid() const noexcept;
};

}  // namespace serial
```

`Timeout` 是可复制、无动态资源的小型 value type。它不公开 clock、time point 或
deadline state，也不保存于 `Port`。

| 构造 | 语义 |
|---|---|
| `immediate()` | 不等待资源。 |
| `infinite()` | 等待至完成或系统错误。 |
| `after(duration > 0)` | 在调用入口建立整体 monotonic deadline。 |
| `after(duration == 0)` | 等价于 `immediate()`。 |
| `after(duration < 0)` | 创建 invalid timeout。 |

invalid timeout 的 `is_valid()` 返回 `false`；传给 I/O 或 `drain` 时返回
`std::errc::invalid_argument`，不执行 I/O。有限 timeout 的 `EINTR`、部分传输和内部
等待不得重置完整时长。

## 5. Configuration

```cpp
namespace serial {

enum class DataBits { Five, Six, Seven, Eight };
enum class Parity { None, Even, Odd };
enum class StopBits { One, Two };
enum class FlowControl { None, Software, Hardware };
enum class RtsLevel { Asserted, Deasserted };

class Rs485Config final {
 public:
  Rs485Config() = delete;

  static Rs485Config disabled() noexcept;
  static Rs485Config enabled(
      RtsLevel rts_during_send,
      RtsLevel rts_after_send,
      std::chrono::milliseconds delay_before_send,
      std::chrono::milliseconds delay_after_send) noexcept;
};

class PortConfig final {
 public:
  PortConfig(
      std::uint32_t baud_rate,
      DataBits data_bits,
      Parity parity,
      StopBits stop_bits,
      FlowControl flow_control,
      Rs485Config rs485) noexcept;

 private:
  // Representation is private.
};

}  // namespace serial
```

`PortConfig` 无默认构造，不存在隐式 9600/8N1 或其他默认链路配置。构造只保存调用者
请求；enum 值、baud rate、RS-485 delay 的可表示性以及目标 TTY/driver 是否支持，均由
`Port::open()` 验证并返回错误。V1 没有 `current_config()` 或 `configure()`。

`Rs485Config` 不暴露 Linux raw ioctl flags。`enabled()` 只描述请求，不推测 driver
能力，也不静默修正参数；延迟使用 Linux `serial_rs485` 的毫秒语义。

`Rs485Config::disabled()` 表示此 Port 不请求 RS-485 driver mode：不执行 RS-485 ioctl，
不验证或清除可能已有的 driver RS-485 状态。只有 `enabled()` 执行 RS-485 state capture、
apply 与严格 readback verification。

每个成功打开的 Port 都是 raw binary byte stream：不使用 canonical line discipline、
echo、signal generation 或输入/输出字符转换。raw mode 不是配置选项。

## 6. Port 生命周期

```cpp
namespace serial {

class Port final {
 public:
  Port() noexcept;
  ~Port() noexcept;

  Port(const Port&) = delete;
  Port& operator=(const Port&) = delete;

  Port(Port&& other) noexcept;
  Port& operator=(Port&& other) = delete;

  [[nodiscard]] std::error_code open(
      const std::string& path,
      const PortConfig& config) noexcept;
  [[nodiscard]] std::error_code close() noexcept;
  [[nodiscard]] bool is_open() const noexcept;
};

}  // namespace serial
```

`Port` 只有 closed 与 open 两种状态。default-constructed 和 moved-from Port 都是
closed；没有 half-open、partially configured、recovering 或 reconfiguring 状态。

`open()` 是唯一资源获取操作，依次验证 path、打开 fd、验证 Linux TTY、应用 raw UART
配置；只有请求 `Rs485Config::enabled()` 时才应用并回读验证 RS-485 配置。全部成功后才
转为 open。任一步失败均关闭 fd 并保持 closed。

空路径或嵌入 NUL 的路径返回 `std::errc::invalid_argument`；非 TTY 返回 C++17 中对应
`ENOTTY` 的 `std::errc::inappropriate_io_control_operation`。已 open 的 Port 再调用 `open()` 返回 `Error::PortAlreadyOpen`
且对象不变。`open()` 不自动 flush，也不请求 `TIOCEXCL` 或任何排他访问策略。

RS-485 请求被 driver 拒绝、清理或改写，或者回读与请求不一致时，`open()` 返回
`Error::UnsupportedConfiguration` 或 `Error::ConfigurationMismatch` 并保持 closed。

`close()` 对 closed Port 是成功的无副作用操作。对 open Port，调用底层 close 后立即
将对象转为 closed；底层 close 报错也不得重试同一 fd。析构 best-effort close，不记录
或报告错误。

## 7. Byte Transfer

```cpp
namespace serial {

struct TransferResult final {
  std::size_t bytes_transferred;
  std::error_code error;
};

class Port final {
 public:
  [[nodiscard]] TransferResult read(
      std::byte* data,
      std::size_t capacity,
      Timeout timeout) noexcept;

  [[nodiscard]] TransferResult write(
      const std::byte* data,
      std::size_t size,
      Timeout timeout) noexcept;
};

}  // namespace serial
```

调用者拥有 buffer 的存储和生命周期。V1 不以 `std::vector<std::byte>`、C++20
`std::span` 或 component-owned buffer 作为核心 I/O 参数。

### 7.1 Validation order

`read()` 与 `write()` 固定按以下顺序验证：

1. Port resource state；
2. operation argument；
3. timeout；
4. I/O。

closed Port 的任何 transfer 都返回：

```cpp
{0, serial::make_error_code(Error::PortNotOpen)}
```

即使 buffer 为零长度。对于 open Port：

- `data == nullptr && size/capacity > 0` 返回 `{0, std::errc::invalid_argument}`；
- 零长度 buffer（包括 `nullptr, 0`）返回 `{0, {}}`；
- invalid timeout 返回 `{0, std::errc::invalid_argument}`。

后三种情况均不执行 I/O。多个非资源参数同时无效时，V1 不承诺它们之间的错误优先级。

### 7.2 Read

`read()` 成功表示至少取得一个字节：

```text
bytes_transferred > 0
error.empty()
```

它不要求填满 caller buffer。尚未取得任何字节前 timeout 时返回
`{0, std::errc::timed_out}`。V1 不提供 read-exactly、framing 或 packet completion
语义。

### 7.3 Write

`write()` 尝试在 timeout 内将整个输入提交给 kernel TTY output queue。仅在
`bytes_transferred == size && error.empty()` 时成功。timeout 或 system error 保留已提交
字节数和停止原因。

write 成功不代表 UART 完成物理发送、对端已接收，或对端已处理数据。

## 8. Flush And Drain

```cpp
namespace serial {

enum class FlushDirection { Input, Output, Both };

class Port final {
 public:
  [[nodiscard]] std::error_code flush(FlushDirection direction) noexcept;
  [[nodiscard]] std::error_code drain(Timeout timeout) noexcept;
};

}  // namespace serial
```

`flush()` 丢弃指定方向的 kernel buffer，不表示读取直到空，也不接受 timeout。

`drain()` 等待 kernel output queue 排空：

- `infinite()` 可使用内核 drain 语义；
- `immediate()` 使用一次 output-queue 查询；queue 非空时返回
  `std::errc::resource_unavailable_try_again`；
- finite timeout 一律返回 `std::errc::operation_not_supported`，因为 Linux V1 没有可
  通用验证的 output-queue-empty wait event；
- immediate 查询不受支持时返回 `std::errc::operation_not_supported`，不得退化为无限等待、
  sleep polling 或伪成功；
- 成功不表示物理线路、远端设备或协议处理已经完成。

closed Port 的 `flush()` 与 `drain()` 返回 `Error::PortNotOpen`。invalid timeout 的
`drain()` 返回 `std::errc::invalid_argument` 且不执行 I/O。

### 8.1 Timeout capability matrix

| 操作 | Immediate | Finite | Infinite |
|---|---|---|---|
| `read()` / `write()` | 单次 non-blocking attempt | monotonic overall deadline | 等待至完成或错误 |
| `drain()` | 单次 output-queue 查询 | `operation_not_supported` | kernel drain |
| `flush()` | 不接受 Timeout | 不接受 Timeout | 不接受 Timeout |

该差异是 Linux TTY 可验证能力的结果：V1 没有通用且不依赖 polling 的
output-queue-empty deadline wait event。因此 `drain(Timeout::after(...))` 不得被理解为
会在 deadline 后返回 timeout。

## 9. Realtime Boundary

`serial` 可以被 realtime-scheduled driver thread 使用，但不依赖 `realtime`，也不对
`read`、`write`、`flush` 或 `drain` 声称 realtime-safe。即使 library 不分配或不加锁，
syscall、TTY driver、USB-UART 与设备仍可能引入不可预测延迟。

调用者负责线程创建、优先级、affinity、Port 独占所有权，以及将 Serial timeout 纳入
自身 deadline budget。RT/NRT 数据交换属于应用层或独立 `realtime` component。

## 10. Public Header Layout

```text
serial/include/serial/
|-- configuration.hpp  # PortConfig、UART enums、Rs485Config、RtsLevel
|-- error.hpp          # Error 与 make_error_code()
|-- port.hpp           # Port 与 FlushDirection
|-- timeout.hpp        # Timeout
`-- transfer.hpp       # TransferResult
```

V1 不提供 umbrella `serial.hpp`。每个 public header 必须自包含；public include path
不得包含 `detail/`、`internal/`、`common/` 或无明确领域含义的辅助层。

## 11. Verification Implications

实现必须以同一 public interface 验证 closed/open/move/close 状态机、配置映射、RS-485
拒绝和回读差异、各 timeout、partial transfer、disconnect、`EINTR`、flush/drain 与
finite-drain capability 缺失。测试应包含 PTY 和独立模拟固件/设备行为；仿真不能替代
真实 UART/RS-485 的 Level 3 验证。
