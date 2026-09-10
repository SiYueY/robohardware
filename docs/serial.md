# RoboHardware Serial 模块

`serial` 是面向 Linux TTY/termios 的 C++17 原始字节流模块。它负责以明确、可组合的
方式打开、配置和操作串口设备；它不理解任何设备或协议语义。

```text
Device / protocol
        |
        v
 serial::Port / serial::Interface
        |
        v
 Linux TTY, termios, ioctl
        |
        v
 UART / USB-UART / RS-232 / RS-485 adapter
```

适用对象包括 `/dev/ttyS*`、`/dev/ttyUSB*`、`/dev/ttyACM*`、UART 传感器、电机驱动、
显示设备，以及 DMX512 或 Modbus 等协议的底层传输。模块本身不提供 Modbus、DMX、
COBS、CRC、分帧、设备发现、自动重连、后台收发线程或 RS-485 方向调度。

## 平台和设计边界

当前实现只支持 Linux，使用 POSIX file descriptor、`termios`、`poll` 与 Linux TTY ioctl。
`termios`、`termios2`、ioctl 常量和 native baud 值都是实现细节，不会出现在公共 API 中。

每个成功构造的 `Port` 都已经完成以下操作：验证配置、以
`O_RDWR | O_NOCTTY | O_NONBLOCK` 打开设备、配置 raw mode、数据格式、流控和波特率。
任何失败都会关闭临时 fd 并返回结构化错误；不会把半初始化的 `Port` 返回给调用方。若
arbitrary baud 的 `termios2` 回退失败，模块会尽力恢复本次打开前读取到的 `termios` 设置后再关闭。

`Port` 使用 RAII 管理 fd，不可复制、可以 `noexcept` 移动。移动后的源对象不再拥有 fd，
其 `fd()` 为 `-1`；不要再对它执行 I/O。`fd()` 仅用于交给调用方的 `poll`、`epoll` 或其他
Linux 集成代码，不转移所有权，且仅在 `Port` 存活且未移动期间有效。

通信参数在 `open()` 成功后不可变。若需更改波特率、校验或流控，应在协调好上层 I/O 后
销毁旧端口并创建新的 `Port`。

## 公共数据模型

```cpp
enum class DataBits : std::uint8_t { Five = 5, Six = 6, Seven = 7, Eight = 8 };
enum class Parity : std::uint8_t { None, Odd, Even };
enum class StopBits : std::uint8_t { One, Two };
enum class FlowControl : std::uint8_t { None, Software, Hardware };

struct Config {
    std::string device;
    std::uint32_t baud_rate{115200};
    DataBits data_bits{DataBits::Eight};
    Parity parity{Parity::None};
    StopBits stop_bits{StopBits::One};
    FlowControl flow_control{FlowControl::None};
};

struct Signals { bool cts; bool dsr; bool dcd; bool ri; };
struct Stats {
    std::uint64_t rx_bytes, tx_bytes;
    std::uint64_t read_calls, write_calls;
    std::uint64_t read_errors, write_errors;
};
```

默认配置是 `115200 8N1` 且无流控。`Software` 表示 XON/XOFF；`Hardware` 表示 RTS/CTS。
不支持 Mark/Space parity、1.5 stop bit，或独立的输入/输出波特率。`device` 不能为空，
`baud_rate` 必须大于零；枚举值也必须属于上表的受支持集合。

标准速率为 9600、19200、38400、57600、115200、230400、460800、500000、576000、
921600 和 1000000。其他正整数速率会尝试通过 Linux `termios2` 的私有 `BOTHER` 支持。
这包含常见的 250000 DMX512 速率，但能否成功取决于内核和具体驱动；不能支持时返回
`ErrorCode::Unsupported`，而不是静默替换为相近速率。

## 错误和 `Result`

所有可失败操作使用 `Result<T>`，而不是异常：成功时可通过 `value()` 取值；失败时通过
`error()` 取得 `Error`。调用 `value()` 或 `error()` 前应先使用 `if (result)` 或 `ok()` 判断。

```cpp
enum class ErrorCode : std::uint8_t {
    InvalidArgument, InvalidState, Unsupported, OpenFailed, ConfigureFailed, IoFailed,
};

struct Error {
    ErrorCode code;
    int native_code;  // 有 native 原因时为 errno，否则为 0
};
```

| 错误 | 含义 |
| --- | --- |
| `InvalidArgument` | 空设备名、零/非法配置、负 wait timeout，或非零长度 I/O 的空指针。 |
| `InvalidState` | 已移动/不再拥有 fd 的端口，或硬件 RTS/CTS 已开启时手动设置 RTS。 |
| `Unsupported` | 平台或驱动不支持请求的 arbitrary baud 或 modem/BREAK 能力。 |
| `OpenFailed` | `open()` 无法打开设备。 |
| `ConfigureFailed` | TTY 属性、流控或波特率无法配置。 |
| `IoFailed` | 正常运行期的 read/write/poll/flush/drain/ioctl 失败。 |

以下不是错误：非阻塞 `read()`/`write()` 的 `EAGAIN` 或 `EWOULDBLOCK`、零长度 I/O，
以及 `wait_readable()` / `wait_writable()` 的超时。它们分别返回成功的 `0`、`0` 与 `false`。

## `Port` 与 `Interface`

协议层若只需要原始收发，可依赖很小的 `Interface`，从而在测试中注入 fake transport：

```cpp
class Interface {
public:
    virtual ~Interface() = default;
    virtual Result<std::size_t> read(std::uint8_t* data, std::size_t size) noexcept = 0;
    virtual Result<std::size_t> write(const std::uint8_t* data, std::size_t size) noexcept = 0;
};
```

`Port final` 实现该接口，并额外提供生命周期、等待、控制线和诊断能力。`Interface` 刻意
不包含配置、fd、flush、BREAK、stats 或打开/关闭语义；它是 protocol test boundary，
不是跨平台 backend 框架。

典型用法：

```cpp
#include <serial/port.hpp>

serial::Config config;
config.device = "/dev/ttyUSB0";
config.baud_rate = 115200;

auto opened = serial::Port::open(config);
if (!opened) {
    // opened.error().code 和 opened.error().native_code 描述失败原因。
    return;
}
serial::Port port = std::move(opened.value());

std::uint8_t buffer[128];
if (auto ready = port.wait_readable(std::chrono::milliseconds(10)); ready && ready.value()) {
    auto received = port.read(buffer, sizeof(buffer));
    // received.value() 可以是 0 到 sizeof(buffer) 的任意值。
}
```

## I/O 语义

fd 始终为 non-blocking，且 raw mode 关闭 canonical mode、echo、信号字符处理、换行/CR
转换和输出后处理。`VMIN = 0`、`VTIME = 0`；调用方可将 `0x00`、`0x03`、`0x0A`、`0x0D`、
`0x11`、`0x13` 等字节当作原始数据（当软件流控启用时，XON/XOFF 由 TTY 流控处理）。

### `read(data, size)`

- 一次调用只执行一次 `read(2)`；不会等待填满 buffer，也不会重试、sleep 或分配内存。
- `size == 0` 返回成功的 `0`，即使 `data == nullptr`。
- `size > 0 && data == nullptr` 返回 `InvalidArgument`。
- 无数据可读（`EAGAIN`/`EWOULDBLOCK`）返回成功的 `0`。
- 成功结果可为 `0` 到 `size`；返回的字节数是交付给调用方的 `rx_bytes`。

### `write(data, size)`

- 一次调用只执行一次 `write(2)`；不会隐式循环直至写完。
- 零长度、空指针和 would-block 规则与 `read()` 相同。
- 成功结果可为 `0` 到 `size`。部分写入是正常字节流语义，调用方决定是否、何时继续。
- `tx_bytes` 只表示已提交给本地 kernel/driver 的字节，不表示物理 UART 已发送、远端已接收，
  更不表示远端协议已处理。

`read_calls` / `write_calls` 记录实际执行的非零长度 syscall 尝试；`read_errors` /
`write_errors` 只记录非 would-block 的 I/O 失败。统计使用原子计数，`stats()` 返回一个快照。

## 等待、队列和发送完成

| API | 实现和结果 | 重要约束 |
| --- | --- | --- |
| `wait_readable(timeout)` | `poll(POLLIN)`；就绪返回 `true`，超时返回 `false`。 | 负 timeout 是 `InvalidArgument`；`EINTR` 会按原 deadline 重试。 |
| `wait_writable(timeout)` | `poll(POLLOUT)`；就绪返回 `true`，超时返回 `false`。 | 它是等待策略，不会让 `write()` 变成 write-all。 |
| `available()` | `TIOCINQ` 返回当前内核 RX 队列中的可读字节数。 | 不是协议帧数，也不保证未来仍可读取。 |
| `pending_write()` | `TIOCOUTQ` 返回本地输出队列中尚未完成 transmission 的字节数。 | 不代表远端接收状态。 |
| `flush_input()` | `tcflush(TCIFLUSH)`。 | **丢弃**尚未被应用读取的输入数据。 |
| `flush_output()` | `tcflush(TCOFLUSH)`。 | **丢弃**尚未完成发送的输出数据；它不是等待发送完成。 |
| `drain()` | `tcdrain()`，并在 `EINTR` 后继续。 | 会阻塞到驱动完成已排队输出；不得放在硬实时路径。 |

任何非零 timeout 的 wait 也是阻塞等待。底层 fd 为 non-blocking 并不意味着 `drain()` 或 wait
不会阻塞调用线程。

## BREAK、modem control 和流控

`set_break(true)` 通过 `TIOCSBRK` 进入持续 BREAK，`set_break(false)` 通过 `TIOCCBRK` 解除。
模块不提供带 duration 的 `send_break()`，以避免隐藏 sleep 和时序策略。调用方可以自行精确地
组合 BREAK 时间。

`set_rts()` 与 `set_dtr()` 设置输出控制线，`signals()` 通过一次 ioctl 返回 CTS、DSR、DCD、
RI 的快照。PTY、USB adapter 或硬件驱动不一定实现这些能力；这种失败返回结构化
`Unsupported` 或 `IoFailed`，不是测试或调用方可忽略的成功。

当 `Config::flow_control == FlowControl::Hardware` 时，内核管理 RTS/CTS。因此
`set_rts()` 返回 `InvalidState`，避免手动 RTS 和硬件流控之间的含糊冲突。软件流控使用
XON/XOFF；raw mode 下未启用软件流控时不会解释这些字节。

## 并发和实时边界

`Port` 默认是 single logical owner，不承诺任意并发调用安全。经调用方协调后，一个
read-side 与一个 write-side 可并行：

- read-side：`read`、`wait_readable`、`available`、`flush_input`；
- write-side：`write`、`wait_writable`、`pending_write`、`drain`、`flush_output`；
- control-side：`set_break`、`set_rts`、`set_dtr`、销毁和移动必须与活跃 I/O 协调。

`read`、`write`、`available`、`pending_write`、`signals`、BREAK/RTS/DTR 和 `stats` 没有
模块内部的 wait、retry、hidden queue 或 I/O hot-path 动态分配。它们仍然是 syscall，
因此“bounded”不等价于 hard real-time guarantee。Linux TTY 调度、USB host controller、
USB packet interval 和驱动缓冲都可能造成抖动；尤其 `/dev/ttyUSB*` 和 `/dev/ttyACM*`
不保证 deterministic bus latency。

## DMX512 示例

DMX 的帧结构、BREAK/MAB 时长和周期调度属于上层 DMX transport，不属于 `serial`：

```cpp
serial::Config dmx;
dmx.device = "/dev/ttyUSB0";
dmx.baud_rate = 250000;
dmx.data_bits = serial::DataBits::Eight;
dmx.parity = serial::Parity::None;
dmx.stop_bits = serial::StopBits::Two;

auto opened = serial::Port::open(dmx);
// 检查 opened；某些 adapter/driver 不支持 250000，结果为 Unsupported。

// 上层在自己的时序策略中执行：
// port.set_break(true) -> BREAK duration -> port.set_break(false) -> MAB duration -> write(frame)
```

RS-485 的 USB 自动方向 adapter 对该模块就是普通 TTY。Linux native `TIOCSRS485`、发送前后
RTS 延迟和方向调度不属于 V1。

## 测试和非目标

`serial/tests/port_test.cpp` 使用 Linux PTY master/slave，不要求真实 `/dev/ttyUSB*` 硬件。
它覆盖 RAII/move、配置校验、raw binary I/O（含 `0x00`、换行与 `0xFF`）、non-blocking empty
read、零长度 I/O、标准波特率、250000 的成功或 `Unsupported` 结果、wait、队列/flush、stats
和 capability-aware 的 BREAK/modem 调用。PTY 不保证支持真实调制解调器线，因此这些测试只
验证“成功或明确失败”的 contract。

V1 明确不包含 Windows/macOS backend、运行期 reconfigure、`read_exact`、`write_all`、文本或
`std::vector` read API、callback/future/coroutine、后台 RX/TX、自动 retry/reconnect、USB VID/PID
枚举、udev、native RS-485 模式、协议 framing/CRC 或日志/metrics framework。协议层可通过
`serial::Interface` 在这些基础原语之上实现自己的 timeout、帧、校验、retry 和状态机。
