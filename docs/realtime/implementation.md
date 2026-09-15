# Realtime Implementation

Linux and pthread return values are mapped privately to `realtime::Error`. Implementations do
not expose native diagnostics through the public interface.
