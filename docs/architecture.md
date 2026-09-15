# robo-hardware V1 Architecture

状态：Architecture Rebaseline

最后更新：2026-09-15

## 1. Status and authority

This document supersedes Architecture Design v3 and its independent-component, standalone-build
and per-module-package model. The detailed API baseline is
[`hardware.md`](hardware.md); this document defines the repository and delivery architecture.

## 2. Project model

`robo-hardware` is one Linux C++17 project with one root configure/build/install operation and
one CMake package: `hardware`. It produces responsibility-local, independently linkable dynamic
libraries:

```text
realtime   serial   spi   can
```

The unified package does not create a unified runtime. `realtime`, `serial`, `spi` and `can`
have no project runtime dependency on one another. Future `canopen` may depend on `can`.

`robo-hardware` is not a public namespace. `hardware` is the header-only shared vocabulary and
package-discovery identity. It owns `<hardware/result.hpp>` and `hardware::Result<T, E>` but does
not produce `libhardware.so` or a `hardware::hardware` target.

## 3. Public identities

| Module | Namespace | Include root | Imported target | Runtime library |
|---|---|---|---|---|
| Realtime | `realtime` | `<realtime/...>` | `realtime::realtime` | `librealtime.so` |
| Serial | `serial` | `<serial/...>` | `serial::serial` | `libserial.so` |
| SPI | `spi` | `<spi/...>` | `spi::spi` | `libspi.so` |
| CAN | `can` | `<can/...>` | `can::can` | `libcan.so` |

Consumers always discover the library family with:

```cmake
find_package(hardware CONFIG REQUIRED)
target_link_libraries(application PRIVATE serial::serial realtime::realtime)
```

Discovery does not link unused modules.

## 4. Source and public/private layout

```text
include/hardware/result.hpp
include/realtime/...
include/serial/...
include/spi/...
include/can/...

src/realtime/...
src/serial/...
src/spi/...
src/can/...

tests/hardware/...
tests/realtime/...
tests/serial/...
tests/spi/...
tests/can/...
tests/integration/...
```

Only installed public headers belong below `include/`; private implementation helpers belong in
`src/`. Empty future directories are not created merely for symmetry.

## 5. CMake and installation

The root project owns targets, tests, install rules, versioning and package generation. Module
targets use normal target-scoped CMake usage requirements and export only their own public
headers and required system dependencies.

The installed package layout is:

```text
<prefix>/<libdir>/cmake/hardware/
  hardwareConfig.cmake
  hardwareConfigVersion.cmake
  realtimeTargets.cmake
  serialTargets.cmake
  spiTargets.cmake
  canTargets.cmake
```

Each module has its own export file and export namespace, for example:

```cmake
install(EXPORT serialTargets
  FILE serialTargets.cmake
  NAMESPACE serial::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hardware)
```

`hardwareConfig.cmake` includes the installed module export files. A single export file cannot
use different CMake namespaces for different targets; therefore `hardwareTargets.cmake` is not a
valid replacement for these module export files.

## 6. Shared error vocabulary

All recoverable public failures use `hardware::Result<T, E>` or `hardware::Result<void, E>`.
`E` is a module-local fixed-size enum; Linux errno and other native diagnostic details remain
private implementation data. `Result` has inline storage and is header-only, but an operation is
realtime-safe only when its payload, implementation and platform evidence support that claim.

The project does not introduce a HAL, common runtime, transport base class, plugin framework or
background worker as part of this consolidation.

## 7. Verification

V1 verification includes root configure/build/test/install, public-header self-containment,
sanitizers, installation-tree consumer tests through `find_package(hardware)`, and checks that
linking one module does not add unrelated project runtime libraries.

Module-level behavior, failure semantics, realtime classification and protocol boundaries are
specified by the API documentation and `hardware.md`.
