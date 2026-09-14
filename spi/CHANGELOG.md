# Changelog

## 0.2.0

- Replace `Options` with aggregate `Config` and rename `frequency_hz` to `max_speed_hz`.
- Remove SPI-specific error codes in favor of `std::errc` and system errors.
- Permit RX-only transfers using Linux spidev semantics.

## 0.1.0

- Initial Linux C++17 SPI V1 implementation.
