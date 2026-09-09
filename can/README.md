# CAN

`can` is a Linux/C++17 SocketCAN module for Classic CAN with non-blocking
SocketCAN I/O, Socket-scoped filters, monotonic receive timestamps, and
bounded error-event storage.

Its documented public API uses headers such as:

```cpp
#include <can/frame.hpp>
#include <can/socket.hpp>

auto socket = can::Socket::open({"can0"});
```

Run the SocketCAN integration tests with an enabled `vcan0` interface.

See [the CAN design documentation](../docs/can.md) for API contracts and
runtime semantics.
