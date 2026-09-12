# Realtime

`realtime` provides Linux C++17 primitives for caller-owned periodic
execution, current-thread setup, timing observations, and bounded RT/NRT data exchange.

The component builds independently:

```sh
cmake -S realtime -B build/realtime
cmake --build build/realtime
ctest --test-dir build/realtime
```

Its public headers use `<realtime/...>`; its public CMake package and target are
`realtime` and `realtime::realtime`.

The V1 Interface and Implementation contracts are maintained in the repository-level
[design documentation](../docs/realtime/api.md).
