# Project Context

## Repository

`robo-hardware` is one Linux C++17 hardware-library project. It has one root build, install
and CMake package (`hardware`), while producing multiple independently linkable runtime
libraries. It is not a public C++ namespace.

## Module

A module is a responsibility-local library within the unified project. Its public identity is
its namespace, include root and CMake target; all modules are discovered through
`find_package(hardware CONFIG REQUIRED)`. Module boundaries do not imply separate builds,
installs, packages or versions.

## Public identity

`hardware` is the shared public vocabulary and package-discovery identity. It owns
`<hardware/result.hpp>` and `hardware::Result<T, E>`; it is header-only and is not a runtime
library or a device framework.

## Realtime

`realtime` is the deterministic-execution and RT/NRT exchange module. Its public identity is
`realtime`, `<realtime/...>` and `realtime::realtime`.

## Serial

`serial` is the TTY byte-stream transport module. Its public identity is `serial`,
`<serial/...>` and `serial::serial`. A read/write is one low-level transfer attempt: a
successful write reports bytes accepted by the kernel/TTY driver, not physical transmission.

## SPI

`spi` is the synchronous Linux spidev-transaction module. Its public identity is `spi`,
`<spi/...>` and `spi::spi`.

**SPI Device**:
A move-only owner of one Linux spidev file descriptor, configured when opened and used for
synchronous SPI transactions.

**SPI transaction**:
One caller-initiated synchronous transfer through a single spidev message, with a single
configured SPI Device.

## CAN

`can` is the SocketCAN RAW transport module. Its public identity is `can`, `<can/...>` and
`can::can`. A received bus event is represented by `ReceivedFrame`, which distinguishes
Classical, FD and error frames; `can::Error` represents transport-operation failure only.
