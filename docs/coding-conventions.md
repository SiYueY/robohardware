# Coding Conventions

本规范适用于项目自有的 public interface 和 private implementation。

## Physical units

项目自有标识符不使用 `_hz`、`_ms`、`_us`、`_ns` 等物理单位后缀。

当普通整数或浮点类型的字段、参数或常量表示物理量时，必须在定义或声明处以
`// Unit: <unit>.` 标注单位。例如：

```cpp
// Unit: Hz.
std::uint32_t max_speed;
```

当函数的多个参数或其语义需要共同说明时，优先在函数声明上方记录契约和单位，避免将
注释碎片化：

```cpp
// Writes the maximum SPI clock speed.
// max_speed unit: Hz.
[[nodiscard]] int write_max_speed(int fd, std::uint32_t max_speed) noexcept;
```

类型本身已经表达单位时不重复注释，例如 `std::chrono::nanoseconds` 和其他
`std::chrono::duration` 特化。

Linux UAPI、第三方 API 或协议规定的原始名称保持不变，例如
`SPI_IOC_WR_MAX_SPEED_HZ` 和 `spi_ioc_transfer::speed_hz`。

本规则立即适用于新代码和本轮 SPI 迁移。其他模块中既有的单位后缀将在后续专门迁移中
处理。
