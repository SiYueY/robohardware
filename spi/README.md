# spi

`spi` is a Linux C++17 synchronous SPI-transaction library for configured
spidev devices. `spi::Config` describes open-time settings and `spi::Device`
owns one spidev file descriptor; it does not implement device protocols, GPIO
chip-select control, asynchronous I/O or background execution.

Public headers are included as `<spi/...>`. Consumers link `spi::spi` after
`find_package(spi CONFIG REQUIRED)`.

See the [V1 API contract](../docs/spi/api.md) and
[implementation design](../docs/spi/implementation.md).
