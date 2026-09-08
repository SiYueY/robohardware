add_executable(realtime_500hz_benchmark realtime_500hz_benchmark.cpp)
target_link_libraries(realtime_500hz_benchmark PRIVATE realtime::realtime)
target_compile_options(realtime_500hz_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror)
