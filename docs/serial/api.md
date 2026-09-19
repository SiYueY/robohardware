# Serial V1 API Contract

The public headers are `config.hpp`, `error.hpp`, `port.hpp`, and `tool.hpp`. `Port` uniquely owns
a Linux TTY descriptor, is move-constructible only, and never exposes the descriptor.
`open(path, Config)` transactionally applies configuration once; successful open freezes it.
Configuration is read back before ownership is committed, and a failed open performs best-effort
rollback before leaving the `Port` closed. Successful opens request Linux `TIOCEXCL` ownership,
which rejects later ordinary opens with `Busy`; it cannot exclude an already-concurrent opener or
a process with `CAP_SYS_ADMIN`. `close()` commits the closed state even if `close(2)` reports
failure, and releases the exclusive request before closing the descriptor.

`read` and `write` use single-progress semantics: the first positive kernel transfer succeeds,
including partial transfers. Overloads without a timeout may wait indefinitely. Timed overloads
use one monotonic deadline covering the whole operation; a negative timeout is invalid, while zero
performs one immediate readiness check and reports `TimedOut` if not ready. `try_read` and
`try_write` never wait and return `WouldBlock` when no immediate progress is possible.
`wait_readable` and `wait_writable` only observe readiness. A read or write that observes two
consecutive ready-but-no-progress results (`read()==0` for reads, or `EAGAIN`/`EWOULDBLOCK`) fails
with `Io` after tolerating one readiness race.

Queue methods have distinct effects: `bytes_available` and `bytes_pending` are snapshots,
`discard_*` are destructive, and `drain()` waits for transmission of already accepted output.
RTS/DTR setters affect only their named line. Setting RTS is invalid while RS-485 or RTS/CTS flow
control owns RTS automatically. `set_break` only asserts or clears BREAK; it has no hidden timer.

`Config::baud_rate` is numeric, but V1 accepts only the implementation's explicit standard-baud
allowlist whose `termios` constants are present in target headers. A positive rate outside that
allowlist is reported as `Unsupported`, never silently substituted. Mark/Space parity, RTS/CTS
flow control, and RS-485 RX-during-TX likewise return `Unsupported` when the target Linux headers
or driver cannot express the requested capability. RS-485 mode and RTS/CTS flow control are
mutually exclusive and their combination is `InvalidArgument`.

All recoverable failures use `hardware::Result<T, serial::Error>`. Runtime methods neither
allocate, create threads, lock, retry until a requested buffer is complete, nor log. A `Port` is
not thread-safe; callers synchronize shared access. `list_ports()` is a non-real-time discovery
operation and may allocate; no ports is a successful empty vector and missing per-device metadata
is non-fatal.
