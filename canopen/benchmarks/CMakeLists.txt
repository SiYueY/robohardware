add_executable(canopen_500hz_benchmark cyclic_benchmark.cpp)
target_link_libraries(canopen_500hz_benchmark PRIVATE canopen::canopen)
target_compile_options(canopen_500hz_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror)
