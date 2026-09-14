# SPI 文档草案

状态：草案

阶段：SPI Design

最后更新：2026-09-15

- [API Design](api.md)：公开类型、资源生命周期、配置、传输、错误与线程契约。
- [Implementation Design](implementation.md)：Linux spidev 实现、private syscall seam、配置序列与验证策略。

## 定位

`spi` 是独立的 Linux C++17 synchronous SPI-transaction transport component。它提供对一个
`/dev/spidevB.C` 节点的 lifecycle、open-time default configuration 和单段同步 transaction；它不是 HAL、SPI
controller driver、peripheral driver 或 protocol library。

```text
Application
    |
spi::Device
    |
/dev/spidevB.C
    |
Linux SPI controller driver
    |
Peripheral
```

公共身份拟定为：

```text
namespace:    spi
include root: <spi/...>
CMake target: spi::spi
package:      spi
```

它与 `serial`、`can` 一样是独立 distribution component；`realtime` 是可由应用组合的
独立调度组件，不是 `spi` 的依赖。

## V1 范围

V1 只支持 Linux userspace SPI initiator 通过 Linux spidev 使用已由系统配置好的设备节点：

- `Device` 的 RAII fd 生命周期；
- 在 `open()` 时设置和回读 mode、maximum speed、bits per word、bit order；
- 一个 `transfer()` 对应一次 `SPI_IOC_MESSAGE(1)`；
- TX-only 或 full-duplex buffer transfer；
- Linux spidev 的同步 ioctl backend。

V1 不支持：

- peripheral/slave 模式、SPI controller driver、device tree、pinmux、kernel module；
- sensor、motor、Flash、寄存器或任何设备协议；
- `read()` / `write()` API、隐式 MOSI dummy 数据、帧解析；
- 多段 message、跨调用保持 CS、外部 GPIO CS、CS 极性控制、bus manager 或 transaction lock；
- per-transfer speed、bits-per-word、delay、CS 或 multi-lane override；
- timeout、cancel、async I/O、callback、future、coroutine、后台线程或自动重试；
- runtime reconfiguration、device discovery、auto recovery、actual SCLK 查询；
- public backend abstraction、mock API 或跨平台 backend。

每个 V1 transaction 都由调用者提供 TX buffer。设备协议所需的 command、address 和 dummy
byte 均属于调用者；`spi` 不决定 dummy 值。

## 与项目基线的关系

当前 `docs/project-charter.md` 和 `docs/architecture.md` 已基线化，尚未把 `spi` 列入其
V1 production component map。本草案记录已确认的 SPI 设计结论；进入实现前，必须单独更新
这些上位文档及根构建编排，使 SPI 的 component identity 和依赖图成为正式基线。
