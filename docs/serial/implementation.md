# Serial Implementation Design

状态：已冻结

阶段：Serial Implementation Design

最后更新：2026-09-12

## 1. 目的

本文定义 `serial` V1 的 Linux TTY implementation：源码组织、private syscall seam、fd 与 Port 状态、termios/RS-485 transaction、deadline I/O engine、错误映射与验证。

本文不修改已冻结的 [API Design](api.md)。若任何 implementation 选择无法履行公开契约，必须回到 API Design 重新评审；不得通过 public backend、隐藏 fallback、测试 hook 或弱化错误语义规避。

## 2. 上位约束

Implementation 必须满足项目章程、V1 需求规格、Architecture Design 与 `api.md`。V1 只使用 C++17 standard library 和 Linux/POSIX system interface；不引入 production component dependency、跨平台 backend、HAL、device framework、runtime 或通用 platform 层。

## 3. Production Source Layout

```text
serial/
├── CMakeLists.txt
├── README.md
├── CHANGELOG.md
├── cmake/
├── docs/
├── include/serial/
│   ├── configuration.hpp
│   ├── error.hpp
│   ├── port.hpp
│   ├── timeout.hpp
│   └── transfer.hpp
├── src/
│   ├── port.cpp
│   ├── configuration.cpp
│   ├── timeout.cpp
│   ├── error.cpp
│   ├── tty_adapter.hpp
│   └── tty_adapter_linux.cpp
└── tests/
    ├── unit/
    ├── integration/
    ├── simulators/
    ├── consumer/
    └── support/
        └── controlled_tty_adapter.cpp
```

只在产生实际内容时创建文件与目录。private header 不安装；不创建 `core`、`platform`、`backend`、`detail`、`runtime` 或同义层。

## 4. Private Linux TTY Seam

`tty_adapter` 是 link-time private syscall seam，不是 runtime adapter object、virtual backend 或 function table。

production target 将 orchestration code 链接至 `tty_adapter_linux.cpp`；Level 1 test target 编译同一份 production orchestration code，但链接 `tests/support/controlled_tty_adapter.cpp`：

```text
production:  Port -> Linux tty adapter
unit test:   Port -> controlled tty adapter
```

adapter 仅覆盖真实 syscall 边界：

- fd：open、close、isatty；
- termios：get、set；
- RS-485：get、set；
- time/wait：`CLOCK_MONOTONIC` now、`ppoll` readiness；
- stream：read、write；
- control：flush、output-queue query、infinite drain。

它由 private free functions 组成，立即捕获 native return value 与 errno。adapter 不构造 `std::error_code`，不理解 timeout、enum、raw 配置、Port state、rollback 或 `serial::Error`；`port.cpp` 是唯一的领域映射点。

## 5. Port Storage And Lifecycle

`Port` 仅保存：

```text
fd_ >= 0  -> open
fd_ == -1 -> closed
```

它不保存 path、`PortConfig`、Timeout、原始 termios、原始 RS-485、native-handle cache，也不保存 receive/pending-write buffer。

`open()` 在局部 fd 上完成 validation、configuration、verification 与 rollback；只有完整成功后才 commit 给 `fd_`。失败时 Port 始终保持 closed。

`close()` 先将 `fd_` 置为 `-1`，再对捕获的旧 fd 调用一次 close。close error 不得导致 fd 重新归 Port 所有，也不得 retry。move constructor 转移 fd 并立即将 source 置为 closed。

## 6. Open And Configuration Transaction

### 6.1 Sequencing

local fd 使用：

```text
O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC
```

`O_NONBLOCK` 是私有实现细节；public API 保持同步 `read/write(..., Timeout)`。打开后按以下顺序：

1. 验证 path 与完整 `PortConfig`；
2. 打开 local fd 并验证 Linux TTY；
3. 读取并保存 current termios；
4. 仅在请求 `Rs485Config::enabled()` 时读取并保存 current RS-485 state；
5. 构建并以 `tcsetattr(..., TCSANOW, ...)` 应用 candidate termios；
6. 仅在 enabled 时应用 RS-485 request；
7. 回读并验证 owned termios fields；
8. 仅在 enabled 时回读并验证 owned RS-485 fields；
9. commit local fd。

`TCSADRAIN` 不可用于打开，因为它引入未声明等待；`TCSAFLUSH` 不可用，因为它隐式丢弃输入。打开不自动 flush，也不执行 `TIOCEXCL`。

### 6.2 Failure cleanup

configuration 或 verification 失败后：若已修改 RS-485，先 best-effort 恢复已捕获的 RS-485 state；再 best-effort 恢复 captured termios；最后关闭 local fd。每项 restore 只尝试一次，不 retry；restore error 不覆盖触发 cleanup 的主错误。

这不是“恢复完整 Port 状态”：Port 从未 commit。该流程只 best-effort 降低外部 TTY state 污染。成功 `close()` 不恢复打开前配置。

### 6.3 Termios ownership

candidate 从 current termios 派生。Serial 拥有并验证：

- input raw：清除 `IGNBRK`、`BRKINT`、`PARMRK`、`ISTRIP`、`INLCR`、`IGNCR`、`ICRNL`；
- output raw：清除 `OPOST`；
- local raw：清除 `ECHO`、`ECHONL`、`ICANON`、`ISIG`、`IEXTEN`；
- receiver/local link：设置 `CREAD`、`CLOCAL`；
- timing：`VMIN = 0`、`VTIME = 0`；
- UART format：baud rate、data bits、parity、stop bits；
- flow control：公开 `FlowControl` 的完整映射。

`HUPCL`、未知 driver bits 与未声明 modem policy 从 current termios 继承。可以把 `cfmakeraw()` 作为位操作参考，但不得把它作为重置整个 termios 的语义。

Parity 只控制 wire format 的 `PARENB` / `PARODD`。raw baseline 清除 `INPCK`、`IGNPAR`、`PARMRK`、`ISTRIP` 与 `BRKINT`；V1 不报告 parity、framing 或 break error，也不注入 marker bytes。

### 6.4 Baud, flow control and RS-485

V1 只将 `uint32_t` baud rate 映射至当前 Linux headers 中存在的标准 termios `B*` constant；不使用 `termios2`、`BOTHER` 或 driver-private baud ioctl。零或不可映射的值在 syscall 前返回 `std::errc::invalid_argument`。

`FlowControl` 总是先清除全部 owned flow-control bits 再应用请求：

| 模式 | termios 语义 |
|---|---|
| `None` | 清除 `IXON`、`IXOFF`、`IXANY` 及 hardware-flow bit。 |
| `Software` | 设置 `IXON`、`IXOFF`；清除 `IXANY` 及 hardware-flow bit。 |
| `Hardware` | 设置 hardware-flow bit；清除 `IXON`、`IXOFF`、`IXANY`。 |

编译环境无法表达 hardware flow 时，Hardware 请求返回 `Error::UnsupportedConfiguration`；driver 运行时拒绝则保留 system error。V1 不支持 `IXANY`、自定义 XON/XOFF 或软硬混合。

`Rs485Config::disabled()` 不执行 `TIOCGRS485` / `TIOCSRS485`，不验证或强制清除既有 driver RS-485 state。只有 `enabled()` 执行 capture、apply 与 verification。

enabled RS-485 回读只比较 Serial 拥有的 enable state、RTS during/after send 与两个 delay；未知、reserved 或未请求 driver flags 不参与比较。RS-485 ioctl 缺失或明确不支持映射为 `Error::UnsupportedConfiguration`；其他 ioctl failure 保留 system error；apply 成功但 owned readback 不一致返回 `Error::ConfigurationMismatch`。

## 7. Deadline I/O Engine

finite read/write 在操作入口读取 `CLOCK_MONOTONIC`，以 checked arithmetic 形成 absolute deadline。不可表示的 deadline 或 native `timespec` conversion 返回 `std::errc::value_too_large`，不执行 I/O。不得 clamp、退化 infinite 或重置 deadline。

`ppoll()` 接收由 current monotonic time 到 absolute deadline 的 remaining duration：

- finite：在剩余时间内等待；
- infinite：无 timeout 等待；
- immediate：不调用 `ppoll()`，只执行一次 non-blocking operation。

finite/infinite `ppoll`、read 与 write 的 `EINTR` 使用同一原始 deadline 重试；immediate read/write 的 `EINTR` 保留 system error，不 retry。

read 在 readable readiness 后只执行一次 native read。任何正字节立即成功返回；它不聚合、预读、staging 或保存 receive state。

write 以同一 deadline 循环等待 writable readiness 并执行 non-blocking write，直到全部字节提交、timeout 或错误。Port 不保存 pending-write state。

`EAGAIN` / `EWOULDBLOCK`：immediate 返回 `std::errc::resource_unavailable_try_again`；finite 到达 deadline 返回 `std::errc::timed_out`；infinite 继续等待。

若 read readiness 同时含 `POLLIN | POLLHUP`，先尝试 read，保留最后可读字节。只有 HUP 或 read 返回 zero 才映射 `Error::DeviceDisconnected`。write 在 HUP 后无法提交下一批字节时映射该错误。`POLLNVAL` 映射 system `EBADF`；其他 syscall errno 原样保留，除非已有明确设备移除证据。

## 8. Flush And Drain

`flush()` 先验证 Port open，再验证 `FlushDirection`，最后调用一次 `tcflush()`；非法 enum 返回 `std::errc::invalid_argument`，`EINTR` 不 retry。

`drain()` 先验证 Port open，再验证 Timeout：

- infinite：调用 `tcdrain()`；`EINTR` retry；
- immediate：单次查询 `TIOCOUTQ`；empty 成功，non-empty 返回 `std::errc::resource_unavailable_try_again`；
- finite：直接返回 `std::errc::operation_not_supported`，不执行等待或查询。

immediate query 无法使用时返回 `std::errc::operation_not_supported`。V1 不采用 sleep polling、estimated transmission time、worker thread 或“假 deadline”实现 finite drain。

## 9. Error And Validation Order

所有 Port operation 先验证 resource state。transfer 的顺序为 state、buffer 参数、Timeout、I/O；flush 为 state、direction、I/O；drain 为 state、Timeout、mode-specific operation。

| 情况 | 结果 |
|---|---|
| 无效 enum、baud 为零/不可映射、不可表示 delay | `std::errc::invalid_argument` |
| 无 hardware-flow 编译能力、RS-485 enabled ioctl 不支持 | `Error::UnsupportedConfiguration` |
| `tcgetattr`、`tcsetattr`、ioctl 实际失败 | system error |
| owned termios/RS-485 readback 不一致 | `Error::ConfigurationMismatch` |

## 10. Verification Plan

- **Level 1 / unit**：controlled adapter 验证 state machine、错误优先级、open transaction、rollback 顺序、native-to-domain mapping、deadline、partial write、EINTR、HUP/EOF 与 finite-drain unsupported。
- **Level 2a / integration**：production target + PTY 验证 raw termios、真实 byte stream、timeout、flush 与 close。
- **Level 2b / simulators**：独立模拟固件/设备经 PTY 主动产生分段发送、延迟响应、部分消费、异常响应和断开；验证设备行为而不只是 fd 可读写。
- **Consumer**：安装后外部项目通过 `find_package(serial)` 和 `serial::serial` 链接。
- **Level 3**：未来真实 UART、USB-UART、RS-485 transceiver；当前不宣称完成。

PTY 或 simulator 不能证明真实 RS-485 ioctl、USB UART 或工业设备兼容性。
