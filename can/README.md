# CAN

`can` is a Linux/C++17 SocketCAN module for Classic CAN. Phase 1 establishes
the CAN domain model and SocketCAN conversion logic; Socket I/O follows in
Phase 2.

Its documented public API uses headers such as:

```cpp
#include <can/frame.hpp>
#include <can/socket.hpp>
```

See [the CAN design documentation](../docs/can.md) for API contracts and
runtime semantics.
