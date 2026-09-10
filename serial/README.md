# Serial

`serial` is a small Linux/C++17 raw TTY byte-stream module. It owns a configured,
non-blocking file descriptor through `serial::Port` and exposes just enough of that
transport for device protocols and protocol-test fakes.

```cpp
#include <serial/port.hpp>

auto port = serial::Port::open({"/dev/ttyUSB0", 115200});
```

See [the serial design documentation](../docs/serial.md) for the complete contract.
