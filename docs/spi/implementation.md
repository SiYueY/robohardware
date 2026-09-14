# SPI Implementation Design

状态：草案

阶段：SPI Implementation Design

最后更新：2026-09-15

## 1. 目的与上位约束

本文定义 `spi` V1 的 Linux spidev implementation。它必须满足 `docs/spi/api.md` 所定义的
public contract、项目 component-first architecture 和无 hidden policy 原则。implementation
不得为测试或未来可能的 platform 扩展增加 public backend、runtime injection、HAL 或其他
production component dependency。

在 SPI 尚未写入项目的已基线化 architecture 和 charter 前，本文只作为设计草案；编码前须先
完成该上位范围变更。

参考输入及取舍：

- [Linux SPI userspace API](https://www.kernel.org/doc/html/latest/spi/spidev.html) 定义
  spidev 的 synchronous ioctl、配置和 full-duplex message 模型；
- [Linux spidev UAPI header](https://github.com/torvalds/linux/blob/master/include/uapi/linux/spi/spidev.h)
  定义 `spi_ioc_transfer`、`SPI_IOC_MESSAGE` 与 configuration ioctl；
- [c-periphery](https://github.com/vsergeev/c-periphery) 参考轻量的 Linux peripheral
  lifecycle；本组件采用其直接性，不采用其 C handle/error-string API；
- [Pigweed pw_spi](https://pigweed.dev/pw_spi/) 参考“transfer 是明确 transaction”的
  表达；其 injected Initiator、ChipSelector、mutex 与多段 transaction 属于跨目标 HAL，
  不进入此 Linux-only V1。

## 2. Source layout

```text
spi/
├── CMakeLists.txt
├── README.md
├── CHANGELOG.md
├── cmake/
│   └── spiConfig.cmake.in
├── include/spi/
│   ├── device.hpp
│   ├── error.hpp
│   └── options.hpp
├── src/
│   ├── device.cpp
│   ├── error.cpp
│   ├── options.cpp
│   ├── spidev_adapter.hpp
│   └── spidev_adapter_linux.cpp
└── tests/
    ├── unit/
    ├── integration/
    ├── consumer/
    └── support/
        └── controlled_spidev_adapter.cpp
```

只在产生实际内容时创建目录或文件。`options.cpp` 与 `serial/configuration.cpp` 一样承载
公开构造函数定义；不是为抽象而存在的空层。private header 不安装，且不引入
`detail`、`backend`、`core`、`platform`、`manager` 或 `interface` 目录/层。

## 3. Private Linux spidev seam

`spidev_adapter` 是 private、link-time syscall seam，由 free functions 组成：

```text
production: Device -> spidev_adapter_linux
unit test:  Device -> controlled_spidev_adapter
```

它只封装 native open/close 和所需 ioctl：read/write mode32、bits-per-word、maximum speed，
以及 `SPI_IOC_MESSAGE(1)`。每个 adapter 函数只返回 native result 和立即捕获的 `errno`；它
不理解 `Options`、Device state、validation order、`std::error_code`、rollback 或
`ConfigurationMismatch`。`device.cpp` 是唯一的领域映射和状态提交点。

production object 不保存 virtual backend、function table、`std::function` 或 runtime-selected
adapter；test adapter 不编译、链接或安装进 production component。禁止 public `SpiBackend`、
`ISpi`、mock API 和 runtime backend registry。未来出现第二个真实 production backend 时，
必须重新评审 architecture 与 public interface，而非把 test seam 提升为 API。

## 4. Device storage and configuration sequence

`Device` 只保存 `fd_`：`-1` 为 closed，非负值为 open。它不保存 path、Options、native
configuration cache、transfer buffer、deadline、mutex 或 background state。

`open()` 使用 local fd，并严格按下列顺序执行：

1. 验证 Device state、path 和 Options；
2. 使用 `O_RDWR | O_CLOEXEC` 打开 local fd；
3. capture 全部原始 state：`SPI_IOC_RD_MODE32`、`SPI_IOC_RD_BITS_PER_WORD`、
   `SPI_IOC_RD_MAX_SPEED_HZ`；
4. apply and verify mode：`SPI_IOC_WR_MODE32`、`SPI_IOC_RD_MODE32`；
5. apply and verify bits-per-word：`SPI_IOC_WR_BITS_PER_WORD`、
   `SPI_IOC_RD_BITS_PER_WORD`；
6. apply and verify maximum speed：`SPI_IOC_WR_MAX_SPEED_HZ`、
   `SPI_IOC_RD_MAX_SPEED_HZ`；
7. 一次性提交 `fd_`。

所有 capture 都必须在第一个 write 前成功，避免 capture failure 污染外部 state。配置失败时，
对每个已尝试 write 的 field 按 speed、bits-per-word、mode32 逆序 best-effort restore
captured state，再 close local fd；write failure 也不得被假定为无副作用。restore failure 不
覆盖 primary error。successful `close()` 不恢复打开前配置。

spidev 设置属于底层共享 state。每个 ioctl 可由 Linux 内核独立串行化，但 capture/apply/
readback/rollback 不是跨 ioctl 的原子 transaction。其他 fd/process 修改同一 device 时，
readback 或后续 transfer 的配置不能得到本组件保证；此类互斥由应用负责。

mode 使用 read-modify-write：从 captured mode32 生成 candidate，仅修改 `SPI_CPOL`、
`SPI_CPHA` 和 `SPI_LSB_FIRST`，并保留所有 unowned mode bits。readback 仅比较这三个 owned
fields。bits-per-word 与 maximum speed 逐项 apply/readback。frequency readback 只验证
spidev 保存的 request，绝不推断物理 SCLK。

initial `SPI_IOC_RD_MODE32` 的 `ENOTTY` 映射为 generic
`inappropriate_io_control_operation`，因为该 fd 不满足 V1 所需的 spidev ioctl contract；
后续 configuration ioctl 的 native failure 保留 system error。

## 5. Transfer implementation

`transfer()` 不做 allocation 或 retry。它使用 zero-initialized `spi_ioc_transfer`，只填入
TX address、可选 RX address 和 checked length；其余字段为零。`size` 必须先同时满足
`__u32` length 和 signed `int` success-result 的表示范围。不可表示时返回
`value_too_large`，不进入 kernel。

`size` 是 buffer byte count，不是 SPI word count。Device 不保存 `bits_per_word`，也不复制
Linux SPI core 的 word-storage 或 partial-word validation；caller 必须传入由完整 SPI word
构成的 buffer。non-8-bit word 的 partial transfer 由 Linux SPI core/controller 拒绝，native
system error 原样返回；implementation 不假定具体 `errno`。

调用 `SPI_IOC_MESSAGE(1)` 一次。negative native result 立即映射捕获的 `errno`；nonnegative
result 必须等于原始 size，否则返回 `io_error`。没有 partial continuation、retry 或 command
replay。TX/RX pointer alias 保持允许，adapter 不复制或暂存 caller buffer。

kernel-side buffer allocation、controller queueing、DMA/PIO 和实际 wire timing 均不属于本
component 的 allocation 或 latency contract。

## 6. Error and resource rules

- validation errors 使用 generic `std::errc`；
- syscall/ioctl errors 在失败点立即保存 `errno`，使用 system category；
- initial `SPI_IOC_RD_MODE32` 的 `ENOTTY` 映射为
  `inappropriate_io_control_operation`；
- only successful native configuration write followed by owned-field readback mismatch maps to
  `spi::Error::ConfigurationMismatch`；
- `close()` 先清除 `fd_` 再 close captured fd；close error 不触发 retry 或 re-ownership；
- destructor ignores close error and performs no logging。

## 7. Verification strategy

### Level 1 — deterministic unit evidence

controlled adapter 必须通过同一 public `Device` interface 覆盖 lifecycle、validation order、
path/Options validation、native errors、capture/RMW/readback、每个 configuration stage 的
primary failure、reverse rollback、rollback-error preservation、close error、all transfer forms、
`size_t` range、ioctl result mapping、alias、zero-size no-ioctl 和 no-retry。另有 self-contained
public-header compile test。

### Consumer evidence

component 必须独立 configure/build/install；consumer test 只从安装树以 `find_package(spi)` 与
`spi::spi` 链接，不依赖 repository root。

### Level 2 — reproducible integration evidence

V1 不虚构通用 virtual SPI device。只有测试实际经过 `spidev_adapter_linux.cpp`、Linux open/ioctl
和 Linux SPI subsystem，才是 Level 2。经过 controlled adapter 的任何 fake 都仍是 Level 1。
controller-specific loopback（如 SPI_LOOP）可作为未来环境专用证据，但不是通用前提。

### Level 3 — hardware evidence

真实验证必须使用 Linux host、spidev、controller 和 peripheral；至少一次应借助 logic analyzer
或 oscilloscope 留下线上证据。报告记录 requested versus observed CPOL/CPHA、SCLK、CS window、
bit order、bits-per-word、full-duplex、TX-only、command/address/dummy/data read、large and
repeated transfers。报告也必须记录 controller 未支持的 Options，而不把所有 controller 都
支持所有配置组合当作 component 的通过条件。
