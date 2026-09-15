# SPI Interface

`Device` is a move-only configured spidev owner. `open`, `close` and `transfer` return
`hardware::Result<void, spi::Error>`. Open has strong failure semantics, close commits to closed,
and transfer failure may follow external peripheral activity.

`spi::Error` is a Linux errno carrier for native failures from `open(2)`, `close(2)` and spidev
`ioctl(2)`: each named native enumerator has the corresponding errno as its underlying value.
`AlreadyOpen`, `NotOpen`, `ConfigurationMismatch` and `TransferMismatch` are local `Device`
semantics with negative values, distinct from errno.
