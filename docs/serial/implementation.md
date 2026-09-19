# Serial V1 Implementation

The Serial implementation follows the same layering used by SPI:

```text
Port / serial domain semantics
        ↓
serial::tty thin Linux wrapper
        ↓
termios / ioctl / ppoll / read / write
```

`src/serial/tty.hpp` and `tty.cpp` are a private Linux UAPI seam. They preserve native return
values and `errno`; they do not introduce another Result or error model. `Port` owns all serial
semantics, state transitions, `serial::Error` mapping, deadlines, rollback, and configuration
validation. Tests replace `tty.cpp` with `tests/serial/tty_fake.cpp`, matching the SPI
`spidev`/`spidev_fake` pattern.

`Port` keeps its descriptor non-blocking so waiting is controlled explicitly with `ppoll` and
`CLOCK_MONOTONIC`. A bounded operation creates one absolute deadline; EINTR, EAGAIN, and
readiness races reuse the remaining time. A positive `read(2)` or `write(2)` result is returned
immediately, including partial transfers.

`open()` owns a temporary descriptor until TTY status, exclusive ownership, termios configuration,
optional RS-485 configuration, and readback validation have succeeded. It requests `TIOCEXCL`
after TTY validation; failed configuration attempts restore the previous state, clear that
temporary exclusive request, and close the descriptor on a best-effort basis. Unsupported RS-485
ioctls are tolerated only when RS-485 is disabled; other ioctl failures remain real open failures.
Drivers that sanitize requested settings are detected by readback and reported as `Unsupported`.

`tool.cpp` enumerates `/sys/class/tty`, requires both device backing and a `/dev` node, enriches
optional USB metadata by walking sysfs parents, and returns results sorted by device path. This is
a control-plane operation and is deliberately isolated from the Port fast path.

Verification has two layers. `serial_port_behavior_test` links `port.cpp` against a deterministic
TTY fake and exercises open rollback, errno context, configuration readback mismatch, RS-485
sanitization, partial I/O, EINTR deadline preservation, readiness errors, automatic RTS ownership,
and close failure state. `serial_pty_integration_test` exercises the production TTY wrapper
against a real Linux PTY for raw I/O, readiness, zero-timeout behavior, fragmentation/burst input,
disconnect handling, exclusive ownership, queue operations, and discovery smoke coverage.
Hardware-specific RS-485 and modem-line validation remains an operator-driven target
because PTYs do not implement those driver ioctls.
