# serial

`serial` is a Linux C++17 byte-stream library for configured UART and
driver-managed RS-485 TTY devices. It owns one TTY file descriptor through
`serial::Port`; it does not implement framing, protocols, device discovery or
background I/O.

Public headers are included as `<serial/...>`. Consumers link
`serial::serial` after `find_package(serial CONFIG REQUIRED)`.

See [the V1 API contract](../docs/serial/api.md) and
[implementation design](../docs/serial/implementation.md).
