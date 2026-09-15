# Realtime Interface

Public headers are in `<realtime/...>`. Recoverable setup failures return
`hardware::Result<void, realtime::Error>`; `PeriodicSchedule::wait_next()` returns
`hardware::Result<PeriodicWait, realtime::Error>`. `Queue` and `Buffer` retain boolean
flow-control operations. See `docs/api-conventions.md` and `docs/hardware.md`.
