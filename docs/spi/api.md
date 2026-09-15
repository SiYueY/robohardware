# SPI Interface

`Device` is a move-only configured spidev owner. `open`, `close` and `transfer` return
`hardware::Result<void, spi::Error>`. Open has strong failure semantics, close commits to closed,
and transfer failure may follow external peripheral activity.

Native `errno` values from `open(2)`, `close(2)` and spidev `ioctl(2)` are mapped one-to-one to
the correspondingly named `spi::Error` enumerator. `AlreadyOpen`, `NotOpen`,
`ConfigurationMismatch` and `TransferMismatch` are local `Device` semantics rather than native
errors.
