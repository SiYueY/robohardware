# SPI Interface

`Device` is a move-only configured spidev owner. `open`, `close` and `transfer` return
`hardware::Result<void, spi::Error>`. Open has strong failure semantics, close commits to closed,
and transfer failure may follow external peripheral activity.
