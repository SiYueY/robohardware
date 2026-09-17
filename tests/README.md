# Test status

The root `CMakeLists.txt` is the active test manifest.

Serial uses two complementary test layers: deterministic behavior tests compile `port.cpp` against
`tests/serial/tty_fake.cpp` to inject syscall errors and state changes, while the PTY integration
test exercises the production Linux TTY wrapper. This mirrors the SPI `spidev`/`spidev_fake`
structure and keeps test seams private to the implementation.
