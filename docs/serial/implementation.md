# Serial V1 Implementation

`Port` keeps its descriptor non-blocking so `ppoll` and `CLOCK_MONOTONIC` exclusively control
waiting. A timed operation creates one absolute deadline; EINTR and readiness races reuse that
deadline. It submits at most one successful `read(2)` or `write(2)` transfer.

`open()` owns a temporary descriptor until it has validated the path, confirmed TTY status,
captured termios and available RS-485 state, applied raw UART settings, requested RS-485 mode,
and read every effective setting back. Any failure restores states that were changed and closes
the temporary descriptor. Linux drivers that sanitize a requested setting cause `Unsupported`.

`tool.cpp` enumerates `/sys/class/tty`, requires a device backing, maps each entry to `/dev`, and
walks sysfs parents for optional USB metadata. This is deliberately outside the `Port` fast path.

Tests use a real PTY for open/raw I/O/readiness/timeout/queue behavior. Hardware validation is
operator-driven: run the same test suite with a USB-UART or RS-485 adapter and verify modem lines,
RS-485 readback, disconnect, and driver sanitization on the target kernel.
