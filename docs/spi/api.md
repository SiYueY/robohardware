# SPI Interface Design

状态：已冻结

阶段：SPI API Design

最后更新：2026-09-15

## 1. 目的与身份

`spi` 是独立的 Linux C++17 synchronous SPI-transaction transport library。它只对 Linux
spidev device 提供明确的配置、生命周期和同步传输契约；它不定义设备或协议语义。

```text
include root: <spi/...>
namespace:    spi
CMake target: spi::spi
package:      spi
```

## 2. 公共约定

所有 public constructors 和 operations 均为 `noexcept`。预期失败通过 `std::error_code`
返回；不以 exception、日志、abort、hidden allocation 或自动恢复替代错误报告。

generic category 用于 `invalid_argument`、`value_too_large`、
`inappropriate_io_control_operation` 和 `io_error`；Linux syscall/ioctl failure 保留
`std::system_category()`；component-local lifecycle 和 readback mismatch 使用 `spi`
category。

```cpp
namespace spi {

enum class Error {
  DeviceAlreadyOpen = 1,
  DeviceNotOpen,
  ConfigurationMismatch,
};

[[nodiscard]] std::error_code make_error_code(Error error) noexcept;

}  // namespace spi

namespace std {

template <>
struct is_error_code_enum<spi::Error> : true_type {};

}  // namespace std
```

`spi` 不定义 `UnsupportedConfiguration`、`DeviceDisconnected`、`PeripheralNotFound` 或
`NoResponse`。controller 拒绝配置时保留 native system error；SPI 没有能可靠区分
不存在 peripheral、浮空线路和有效数据的 low-level acknowledgement。

## 3. Options

```cpp
namespace spi {

enum class Mode { Mode0, Mode1, Mode2, Mode3 };
enum class BitOrder { MsbFirst, LsbFirst };

class Options final {
 public:
  Options(
      Mode mode,
      std::uint32_t frequency_hz,
      std::uint8_t bits_per_word,
      BitOrder bit_order) noexcept;

 private:
  Mode mode_;
  std::uint32_t frequency_hz_;
  std::uint8_t bits_per_word_;
  BitOrder bit_order_;

  friend class Device;
};

}  // namespace spi
```

`Options` 是只供 `open()` 消费的显式请求值：无默认构造、无 public accessor、无 aggregate
初始化或 public member。private member 使存储字段不成为 source-level API，但不表示 Pimpl
或 ABI-hidden layout。

构造函数只保存请求。`open()` 负责验证：`Mode`、`BitOrder` 必须是枚举的已定义值，
`frequency_hz` 与 `bits_per_word` 必须非零。V1 不硬编码控制器特定的合法字宽或频率表；
Linux/controller 决定其余支持范围。Linux 的 `bits_per_word == 0` 表示 8 bit，但该
特殊值不属于本 API。

## 4. Device 生命周期与并发

```cpp
namespace spi {

class Device final {
 public:
  Device() noexcept;
  ~Device() noexcept;

  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&& other) noexcept;
  Device& operator=(Device&& other) = delete;

  [[nodiscard]] std::error_code open(
      const std::string& path,
      const Options& options) noexcept;
  [[nodiscard]] std::error_code close() noexcept;
  [[nodiscard]] bool is_open() const noexcept;

  [[nodiscard]] std::error_code transfer(
      const std::byte* tx,
      std::byte* rx,
      std::size_t size) noexcept;

 private:
  int fd_;
};

}  // namespace spi
```

`fd_ == -1` 表示 closed，`fd_ >= 0` 表示 open。default-constructed 和 moved-from
对象均 closed；`open()` 只可从 closed 状态调用，已 open 时返回 `DeviceAlreadyOpen` 且对象
不变。空 path 或含 embedded NUL 的 path 返回 `invalid_argument`。

`close()` 对 closed device 成功且无副作用。对 open device，它先放弃对 fd 的所有权，再只
尝试一次 native close；即使 close 失败也保持 closed，不重试。析构执行 best-effort close
并忽略错误。Device 不提供 move assignment，避免向已持有 fd 的对象转移资源时的错误语义。

一个 `Device` 由一个线程独占。同一对象的 `open`、`close`、move、`is_open` 和
`transfer` 不保证并发安全；库不加 hidden mutex、不创建 worker thread。对同一
`/dev/spidevB.C` 的其他 fd 或进程不提供 configuration/transfer 一致性保证；应用如需
复合互斥，必须自行同步。

## 5. open() 配置契约

`open()` 以 `O_RDWR | O_CLOEXEC` 打开 path，不使用 `O_NONBLOCK` 或 `O_NOCTTY`。它不
验证 peripheral 存在、可应答或返回的数据有效。

配置顺序为：验证 Options；打开 local fd；capture current native state；apply requested
owned fields；readback verify；全部成功后才把 local fd 提交给 `Device`。失败时 best-effort
恢复已捕获的 native state、关闭 local fd，Device 保持 closed。该序列是 Device 对象的
commit-on-success sequence，不是底层共享 `spi_device` 的原子 transaction。

V1 只拥有以下 native configuration fields：

- mode 的 `SPI_CPOL`、`SPI_CPHA`；
- mode 的 `SPI_LSB_FIRST`；
- `bits_per_word`；
- maximum `speed_hz`。

mode 从读回的 mode32 派生：只改写 CPOL、CPHA、LSB-first，保留例如 `CS_HIGH`、`3WIRE`、
`NO_CS`、`READY` 等未拥有 flag。readback 也只比较 owned mode fields。bits-per-word 和
maximum speed 分别写入、回读和精确比较。successful write 后的 owned-field mismatch 返回
`ConfigurationMismatch`；其它 native failure 保留 system error。initial `SPI_IOC_RD_MODE32`
报告 `ENOTTY` 时返回 `inappropriate_io_control_operation`；其后的 native configuration ioctl
failure 保留 system error。

frequency 的 readback 相等只说明 spidev 保存了所请求的 maximum speed setting。controller
不一定产生精确的该频率，V1 也不查询或声称实际 SCLK。

## 6. transfer() 契约

`transfer()` 是唯一 I/O API，映射为恰好一次 `SPI_IOC_MESSAGE(1)`。它不提供 `read()` 或
`write()`；SPI transaction 不是独立的单向字节流操作。

| 输入 | 语义 |
|---|---|
| `tx != nullptr`, `rx != nullptr` | full-duplex；两 buffer 均为 `size` bytes。 |
| `tx != nullptr`, `rx == nullptr` | TX-only；丢弃同步移入的数据。 |
| `tx == nullptr`, `rx != nullptr`, `size > 0` | `invalid_argument`；V1 不定义隐式 MOSI dummy data。 |
| `size == 0` | success；不执行 ioctl。 |

TX 与 RX 可以指向同一 buffer；调用者拥有 buffer 的存储和整个调用期间的生命周期。
所谓“读寄存器”由 caller 的 full-duplex buffer 显式表达 command、address、dummy byte 和
接收数据位置。

`size` 表示 TX/RX buffer 的 byte count，而不是 SPI word count。调用者必须确保 buffer
包含整数个、按 Linux SPI word-storage 规则表示的完整 SPI word。对于 non-8-bit word，
`spi` 不缓存 `bits_per_word`，也不在 userspace 重现 Linux SPI core 的 word-size/alignment
判断；partial-word request 由 Linux SPI core/controller 拒绝，其 native system error 原样
返回。V1 不承诺该 failure 的特定 `errno`。

validation order 固定为：

1. closed Device 返回 `DeviceNotOpen`；
2. `size == 0` 成功且不调用 ioctl；
3. `tx == nullptr` 返回 `invalid_argument`；
4. `size > min(UINT32_MAX, INT_MAX)` 返回 `value_too_large`；
5. 执行一次 ioctl。

V1 不预检查可由内核 module/runtime 改变的 spidev `bufsiz` 或 DMA bounce-buffer 限制；
它们导致的失败保留 ioctl system error。ioctl 负返回时立即捕获 `errno`；成功必须恰好返回
`size`，任何非负但不等于 `size` 的返回均为 `io_error`。

每个 transfer descriptor 只设置 TX pointer、可选 RX pointer 和 length。它不提交
per-transfer configuration override：speed、bits-per-word、delay、CS change、word delay、
dual/quad/octal lane 均保持为零或不使用。在不存在组件外并发重配置时，transfer 依赖
`open()` 已建立的 spidev default configuration。

不自动 retry、不续传 partial result，也不重发整个 transaction；只有理解 peripheral
协议幂等性和状态的上层 driver 才能决定恢复策略。

## 7. Realtime 边界

`spi` 不依赖也不属于 `realtime`。它不配置 `SCHED_FIFO`、CPU affinity、memory locking 或
cyclic thread。`transfer()` 在 userspace component 内不分配 dynamic staging buffer，也不
执行 mutex、sleep、poll、thread、retry、log、callback、deadline management、timeout、cancel
或 async I/O。

这不意味着完整 Linux SPI path 不会 allocation 或 blocking：spidev、controller driver、
DMA/PIO、kernel scheduling 和 peripheral 都可能造成不可预测延迟。因此 `spi` 不声称
realtime-safe、bounded-time、wait-free、non-blocking 或 deterministic。应用负责独占、
调度以及将 synchronous ioctl 纳入自己的 deadline budget。

## 8. Public header layout

```text
spi/include/spi/
|-- device.hpp
|-- error.hpp
`-- options.hpp
```

V1 不提供 umbrella `spi.hpp`、`transfer.hpp`、public `SpiBackend`、`ISpi`、factory、manager
或 generic interface。每个 public header 自包含，并只暴露 component domain 的 C++17 types。
