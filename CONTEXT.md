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
