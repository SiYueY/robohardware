# robo-hardware

robo-hardware 是面向 Linux 机器人与工业设备驱动开发的现代 C++17
低层基础组件库族。

项目计划提供确定性执行基础、RT/NRT 数据交换原语、UART/RS-485
transport 和 SocketCAN RAW transport。各组件独立构建、独立安装、独立使用，
不依赖 ROS，也不包含设备业务逻辑。

当前 repository 是组件组合与开发编排容器。每个 component 使用自己的 public identity，
不绑定 repository topology；Realtime 使用 `realtime` namespace、`<realtime/...>` include
root、`realtime::realtime` target 和 `realtime` package；Serial 使用 `serial` namespace、
`<serial/...>` include root、`serial::serial` target 和 `serial` package。

Realtime 的 Interface 和 Implementation Design 已冻结；Serial 的 Interface 和
Implementation Design 已冻结，尚未进入 production implementation。CAN 尚未进入
Interface Design。

## 设计文档

- [项目级文档](docs/README.md)
- [Realtime 组件文档](docs/realtime/README.md)
- [Serial 组件文档](docs/serial/README.md)

## License

Apache License 2.0。详见 [LICENSE](LICENSE)。
