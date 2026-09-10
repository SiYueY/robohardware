# RoboHardware

Linux/C++17 robot-hardware infrastructure modules:

- `realtime`: scheduling, periodic tasks, and related runtime utilities.
- `can`: non-blocking Classic SocketCAN transport.
- `serial`: non-blocking raw Linux TTY byte-stream transport.
- `canopen`: CANopen and CiA402 building blocks.

See [the serial API contract](docs/serial.md) for UART/RS-232/RS-485-adapter use.
