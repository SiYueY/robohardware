# Test status

The root `CMakeLists.txt` is the only active test manifest. Its tests exercise the installed
public Result/Error interfaces and are the V1 validation surface.

The pre-rebaseline test fixtures targeted removed error and transfer interfaces. They were
deleted during the V1 rebaseline rather than being presented as current verification.
