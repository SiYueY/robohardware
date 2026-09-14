# SPI Interface Design

状态：已冻结，software-validated

阶段：SPI API Design

最后更新：2026-09-15

## 1. 目的与身份

`spi` 是独立的 Linux C++17 synchronous SPI-transaction transport library。它为 Linux
spidev device 提供配置、生命周期和同步传输契约；不定义 peripheral、协议或 controller 语义。

```text
include root: <spi/...>
namespace:    spi
CMake target: spi::spi
package:      spi
```

## 2. Config

```cpp
namespace spi {

enum class Mode : std::uint8_t { Mode0, Mode1, Mode2, Mode3 };
enum class BitOrder : std::uint8_t { MsbFirst, LsbFirst };

struct Config {
  Mode mode;
  std::uint32_t max_speed_hz;
  std::uint8_t bits_per_word;
  BitOrder bit_order;
};

}  // namespace spi
```

`Config` 是普通 aggregate，不保存 invariant、不拥有资源，也不执行 Linux ioctl。`open()` 消费
它时验证定义的 `Mode`、`BitOrder` 以及非零的 `max_speed_hz` 和 `bits_per_word`。controller
特定的速率和 word-size 支持由 Linux/controller 决定。

`max_speed_hz` 表示写入 `SPI_IOC_WR_MAX_SPEED_HZ` 的 maximum requested speed，不表示可观测
或保证的物理 SCLK。

## 3. Device

```cpp
class Device final {
 public:
  Device() noexcept = default;
  ~Device() noexcept;
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&& other) noexcept;
  Device& operator=(Device&& other) = delete;

  [[nodiscard]] std::error_code open(
      const std::string& path, const Config& config) noexcept;
  [[nodiscard]] std::error_code close() noexcept;
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] std::error_code transfer(
      const std::byte* tx, std::byte* rx, std::size_t size) noexcept;
};
```

`Device` 是一个 spidev fd 的 move-only owner。default-constructed 和 moved-from 对象 closed；
`close()` 对 closed device 成功且无副作用。它先放弃 fd 所有权、只尝试一次 native close，失败
也保持 closed；析构 best-effort close 并忽略错误。

一个 `Device` 由一个线程独占。同一对象的操作不保证并发安全，组件不创建 mutex 或 worker。
对同一 device 的其他 fd/process 的配置或 transfer 不受本组件互斥保证。

## 4. Error contract

预期失败只通过 `std::error_code` 返回：

| 情况 | 返回 |
|---|---|
| 已 open 时调用 `open()` | `std::errc::device_or_resource_busy` |
| closed device 调用 `transfer()` | `std::errc::bad_file_descriptor` |
| 无效 path、Config 或 nonzero transfer 的双方空 buffer | `std::errc::invalid_argument` |
| size 超过 `min(UINT32_MAX, INT_MAX)` | `std::errc::value_too_large` |
| successful configuration write 后 readback mismatch | `std::errc::io_error` |
| nonnegative transfer result 不等于 size | `std::errc::io_error` |
| native open/close/ioctl failure | 捕获的 `std::system_category()` error |

initial `SPI_IOC_RD_MODE32` 的 `ENOTTY` 映射为
`std::errc::inappropriate_io_control_operation`。SPI 不定义 component-local error category、
`Error` enum 或 `make_error_code()`。

## 5. open() configuration contract

`open()` 以 `O_RDWR | O_CLOEXEC` 打开 local fd，先 capture mode32、bits-per-word 和 maximum
speed；然后依次 apply/readback mode、bits-per-word、maximum speed；仅在全部成功后提交 fd。
失败时 reverse best-effort restore 已尝试字段，再关闭 local fd，且 restore failure 不覆盖 primary
error。

SPI 只拥有 mode 的 `SPI_CPOL`、`SPI_CPHA`、`SPI_LSB_FIRST`。它 read-modify-write these bits，
保留 `CS_HIGH`、`3WIRE`、`NO_CS`、`READY` 等 unowned native mode bits，并只回读比较 owned bits。
configuration sequence 不是跨 ioctl 的原子 transaction。

## 6. transfer() contract

每个 nonzero transfer 恰好调用一次 `SPI_IOC_MESSAGE(1)`；没有 retry、partial continuation 或
per-transfer configuration override。descriptor 只设置 TX address、RX address 和 length，其余字段为零。

| 输入 | 语义 |
|---|---|
| TX 与 RX 均非空 | full-duplex |
| 仅 TX 非空 | TX-only，丢弃同步移入的数据 |
| 仅 RX 非空 | RX-only；遵循 Linux spidev 的 zero MOSI shift semantics |
| 非零 size 且双方为空 | `invalid_argument` |
| size 为零 | success，不执行 ioctl |

`size` 是 byte count 而非 SPI word count。组件不缓存 word size、不检查 partial-word alignment，
也不预判可变的 spidev `bufsiz` 或 controller 限制；它们的失败保留 Linux error。TX/RX alias 合法，
caller 负责 buffer 生命周期和协议所需的 command、address 与 dummy byte。

## 7. Public header layout

```text
spi/include/spi/
|-- config.hpp
`-- device.hpp
```

V1 不提供 umbrella header、Bus、Controller、Initiator、Transaction、multi-segment API、timeout、
async I/O、runtime configuration、public backend 或 generic HAL。
