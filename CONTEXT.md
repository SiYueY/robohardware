# Project Context

## Repository

`robo-hardware` is a source and development-orchestration repository. It is not a public
SDK identity and does not own a shared public C++ namespace.

## Component

A component is an independently consumable library with its own source tree, build, tests,
installation, documentation, version, and public identity. Components may share this
repository without becoming submodules of a common runtime or framework.

## Public identity

A component's public identity is the stable combination of its namespace, include root,
CMake target, and CMake package. It belongs to the component rather than to repository
topology.

## Realtime

`realtime` is an independent Linux C++17 realtime-primitives library. Its public identity is
the `realtime` namespace, `<realtime/...>` include root, `realtime::realtime` CMake target,
and `realtime` package.

## Serial

`serial` is an independent Linux C++17 TTY byte-stream transport library. Its public identity
is the `serial` namespace, `<serial/...>` include root, `serial::serial` CMake target, and
`serial` package. It has no production dependency on `realtime`; applications compose the two
components while retaining independent ownership and timing contracts.

## SPI

`spi` is an independent Linux C++17 synchronous SPI-transaction transport library. Its public
identity is the `spi` namespace, `<spi/...>` include root, `spi::spi` CMake target, and `spi`
package. It has no production dependency on the other components.

**SPI Device**:
A move-only owner of one Linux spidev file descriptor, configured when opened and used for
synchronous SPI transactions.

**SPI transaction**:
One caller-initiated synchronous transfer through a single spidev message, with a single
configured SPI Device.
