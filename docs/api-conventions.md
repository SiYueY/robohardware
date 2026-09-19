# Public API Conventions

状态：V1 基线

本文定义所有模块共享的 public interface 约定；模块文档只记录自身具体的错误值和
operation contract。

所有 project-owned 标识符的命名与物理单位标注均遵循
[Coding Conventions](coding-conventions.md)。该规范同时适用于 public interface 和 private
implementation。

## Return semantics

| 情形 | Return type |
|---|---|
| 无可恢复失败且有值 | `T` |
| 无可恢复失败且无值 | `void` |
| 正常 flow-control | `bool` |
| 可恢复失败且有成功值 | `hardware::Result<T, E>` |
| 可恢复失败且无成功值 | `hardware::Result<void, E>` |

Queue full/empty、Buffer 尚无 publication 与 `is_configured()` 都是 normal flow-control，
不使用 Result。

## Result and errors

`hardware::Result<T, E>` 严格表示 value XOR error，使用 inline storage，不分配、不加锁、
不执行 syscall、不记录日志且不隐藏 retry。它是 move-only；`T` 和 `E` 必须是 non-array
object type，且 nothrow move constructible、nothrow destructible。copy factories 仅通过
C++17 SFINAE 对实际 `T` 或 `E` 为 nothrow-copy 类型时参与 overload resolution；调用方不能
通过显式模板实参放宽这一约束。Result move 后 source 保持原有 success/error logical state，
但 active payload 遵循其自身的 moved-from 语义。

错误类型属于模块：`realtime::Error`、`serial::Error`、`spi::Error` 和 `can::Error`。
它们是固定大小 `enum class`，不包含 success 值，也不保存 native errno、category 或 message。
Linux errno、pthread 返回值和 ioctl/socket 错误在 implementation 内映射为稳定领域错误。

错误 accessor 的前置条件是 Result 不含 value；value accessor 的前置条件是 Result 含 value。
违反前置条件是 programmer error，debug build 可用 assertion 检测。

## Failure semantics

每个 Result-returning operation 必须说明 success、failure、失败后 object state、external
side effect 与 retry responsibility。`open` 和 `configure` 使用 strong failure guarantee；
native `close` 提交后使用 committed-state semantics；SPI transfer 和 CAN send 的 failure
不否认外部硬件已观察到部分 activity。

## Transport conventions

Serial read/write 是一次 low-level transfer attempt：获得 readiness 后尝试 native transfer。
第一次 positive transfer 立即成为 `Success(n)`，包括 partial transfer；实现允许一次有限的
readiness-race retry，但不会为填满请求 buffer 执行 loop-until-complete。连续第二次
ready-but-no-progress 返回 `Io`。write 的 `n` 是 kernel/TTY driver accepted bytes，不是
physical transmission completion；后者由 `drain()` 明确表达。

CAN `ReceivedFrame` 是成功接收的 tagged value，区分 Classical、FD 与 ErrorFrame。ErrorFrame
是 bus diagnostic event，不是 `can::Error`；后者仅表示 transport operation failure。

## Realtime classification

Result 不会自动令 operation RT-safe。每个 interface 必须标注 RT-callable、Setup-only 或
Non-RT，并基于 payload、implementation、platform evidence、allocation、locking、blocking、
exception、thread ownership 和 external synchronization 给出依据。
