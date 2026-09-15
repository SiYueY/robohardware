# Serial Interface

`Port` is a move-only TTY owner. Lifecycle, transfer, flush and drain failures use
`hardware::Result<..., serial::Error>`. A read/write performs one low-level transfer attempt;
write success means kernel/TTY acceptance, while `drain()` expresses output completion.
