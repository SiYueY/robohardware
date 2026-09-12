cmake_minimum_required(VERSION 3.20)

project(realtime VERSION 0.1.0 LANGUAGES CXX)

include(CheckCXXSourceCompiles)
include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "realtime requires a Linux target")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    message(FATAL_ERROR "realtime requires GCC 10 or newer")
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
    message(FATAL_ERROR "realtime requires Clang 12 or newer")
  endif()
else()
  message(FATAL_ERROR "realtime requires GCC or Clang")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" REALTIME_PROCESSOR)
if(NOT REALTIME_PROCESSOR STREQUAL "x86_64" AND
   NOT REALTIME_PROCESSOR STREQUAL "aarch64")
  message(FATAL_ERROR
          "realtime supports only little-endian x86_64 or aarch64 LP64 targets")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "realtime requires an LP64 target")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
check_cxx_source_compiles(
  "
    #include <atomic>
    #include <chrono>
    #include <cstddef>
    #include <cstdint>
    #include <memory>

    static_assert(sizeof(void*) == 8, \"LP64 pointer size is required\");
    static_assert(sizeof(std::size_t) == 8, \"LP64 size_t is required\");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  \"always-lock-free size_t atomic is required\");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  \"always-lock-free uint32_t atomic is required\");
    static_assert(std::chrono::steady_clock::is_steady,
                  \"C++ chrono steady clock support is required\");

    int main() { return 0; }
  "
  REALTIME_PLATFORM_GATE_PASSES
)
if(NOT REALTIME_PLATFORM_GATE_PASSES)
  message(FATAL_ERROR
          "realtime requires C++17 chrono/object-lifetime facilities and "
          "always-lock-free size_t and uint32_t atomics")
endif()

find_package(Threads REQUIRED)

option(REALTIME_ENABLE_STRESS_TESTS
       "Build long-running realtime concurrency stress tests"
       OFF)

add_library(
  realtime
    src/clock.cpp
    src/current_thread.cpp
    src/monotonic_time_linux.cpp
    src/periodic_schedule.cpp
    src/process_memory.cpp
    src/timing_statistics.cpp
)
add_library(realtime::realtime ALIAS realtime)

target_compile_features(realtime PUBLIC cxx_std_17)
target_compile_definitions(realtime PRIVATE _GNU_SOURCE)
target_link_libraries(realtime PUBLIC Threads::Threads)
set_target_properties(
  realtime
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME realtime
)
target_include_directories(
  realtime
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(BUILD_TESTING)
  configure_file(
    tests/consumer/consumer_test.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/consumer_test.cmake
    @ONLY
  )
  add_test(
    NAME realtime_consumer_install_test
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/consumer_test.cmake
  )
  add_library(
    realtime_controlled_time
    STATIC
      src/clock.cpp
      src/periodic_schedule.cpp
      tests/support/controlled_monotonic_time.cpp
  )
  target_compile_features(realtime_controlled_time PRIVATE cxx_std_17)
  target_include_directories(
    realtime_controlled_time
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
  )

  add_executable(realtime_clock_test tests/unit/clock_test.cpp)
  target_link_libraries(realtime_clock_test PRIVATE realtime::realtime)
  add_test(NAME realtime_clock_test COMMAND realtime_clock_test)
  set_tests_properties(realtime_clock_test PROPERTIES LABELS unit)

  add_executable(
    realtime_periodic_schedule_configure_test
    tests/unit/periodic_schedule_configure_test.cpp
  )
  target_link_libraries(realtime_periodic_schedule_configure_test
                        PRIVATE realtime_controlled_time)
  add_test(
    NAME realtime_periodic_schedule_configure_test
    COMMAND realtime_periodic_schedule_configure_test
  )
  set_tests_properties(realtime_periodic_schedule_configure_test PROPERTIES LABELS unit)

  add_executable(
    realtime_periodic_schedule_unconfigured_test
    tests/unit/periodic_schedule_unconfigured_test.cpp
  )
  target_link_libraries(realtime_periodic_schedule_unconfigured_test
                        PRIVATE realtime_controlled_time)
  add_test(
    NAME realtime_periodic_schedule_unconfigured_test
    COMMAND realtime_periodic_schedule_unconfigured_test
  )
  set_tests_properties(realtime_periodic_schedule_unconfigured_test PROPERTIES LABELS unit)

  add_executable(
    realtime_periodic_schedule_wait_test
    tests/unit/periodic_schedule_wait_test.cpp
  )
  target_include_directories(
    realtime_periodic_schedule_wait_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
  )
  target_link_libraries(realtime_periodic_schedule_wait_test
                        PRIVATE realtime_controlled_time)
  add_test(NAME realtime_periodic_schedule_wait_test COMMAND realtime_periodic_schedule_wait_test)
  set_tests_properties(realtime_periodic_schedule_wait_test PROPERTIES LABELS unit)

  add_executable(realtime_queue_test tests/unit/queue_test.cpp)
  target_link_libraries(realtime_queue_test PRIVATE realtime::realtime)
  add_test(NAME realtime_queue_test COMMAND realtime_queue_test)
  set_tests_properties(realtime_queue_test PROPERTIES LABELS unit)

  add_executable(realtime_buffer_test tests/unit/buffer_test.cpp)
  target_link_libraries(realtime_buffer_test PRIVATE realtime::realtime)
  add_test(NAME realtime_buffer_test COMMAND realtime_buffer_test)
  set_tests_properties(realtime_buffer_test PROPERTIES LABELS unit)

  add_executable(realtime_current_thread_test tests/unit/current_thread_test.cpp)
  target_link_libraries(realtime_current_thread_test PRIVATE realtime::realtime)
  add_test(NAME realtime_current_thread_test COMMAND realtime_current_thread_test)
  set_tests_properties(realtime_current_thread_test PROPERTIES LABELS unit)

  add_executable(realtime_timing_statistics_test tests/unit/timing_statistics_test.cpp)
  target_link_libraries(realtime_timing_statistics_test PRIVATE realtime::realtime)
  add_test(NAME realtime_timing_statistics_test COMMAND realtime_timing_statistics_test)
  set_tests_properties(realtime_timing_statistics_test PROPERTIES LABELS unit)

  add_executable(
    realtime_periodic_schedule_linux_integration_test
    tests/integration/periodic_schedule_linux_integration_test.cpp
  )
  target_link_libraries(realtime_periodic_schedule_linux_integration_test
                        PRIVATE realtime::realtime)
  add_test(
    NAME realtime_periodic_schedule_linux_integration_test
    COMMAND realtime_periodic_schedule_linux_integration_test
  )
  set_tests_properties(realtime_periodic_schedule_linux_integration_test PROPERTIES LABELS integration)

  add_executable(
    realtime_process_memory_integration_test
    tests/integration/process_memory_integration_test.cpp
  )
  target_link_libraries(realtime_process_memory_integration_test PRIVATE realtime::realtime)
  add_test(
    NAME realtime_process_memory_integration_test
    COMMAND realtime_process_memory_integration_test
  )
  set_tests_properties(realtime_process_memory_integration_test PROPERTIES LABELS integration)

  add_executable(
    realtime_current_thread_linux_integration_test
    tests/integration/current_thread_linux_integration_test.cpp
  )
  target_compile_definitions(realtime_current_thread_linux_integration_test PRIVATE _GNU_SOURCE)
  target_link_libraries(realtime_current_thread_linux_integration_test PRIVATE realtime::realtime)
  add_test(
    NAME realtime_current_thread_linux_integration_test
    COMMAND realtime_current_thread_linux_integration_test
  )
  set_tests_properties(realtime_current_thread_linux_integration_test PROPERTIES LABELS integration)

  add_executable(realtime_queue_state_model_test tests/model/queue_state_model_test.cpp)
  add_test(NAME realtime_queue_state_model_test COMMAND realtime_queue_state_model_test)
  set_tests_properties(realtime_queue_state_model_test PROPERTIES LABELS model)

  # The former four-slot Buffer model is intentionally not built: Buffer now
  # uses a different three-slot exchange protocol and requires a new model.

  add_library(
    realtime_public_headers_compile_test
    OBJECT
      tests/public_headers/buffer.cpp
      tests/public_headers/clock.cpp
      tests/public_headers/current_thread.cpp
      tests/public_headers/periodic_schedule.cpp
      tests/public_headers/process_memory.cpp
      tests/public_headers/queue.cpp
      tests/public_headers/timing_statistics.cpp
  )
  target_compile_features(realtime_public_headers_compile_test PRIVATE cxx_std_17)
  target_include_directories(
    realtime_public_headers_compile_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

  if(REALTIME_ENABLE_STRESS_TESTS)
    add_executable(realtime_queue_stress_test tests/integration/queue_stress_test.cpp)
    target_link_libraries(realtime_queue_stress_test PRIVATE realtime::realtime)
    add_test(NAME realtime_queue_stress_test COMMAND realtime_queue_stress_test)
    set_tests_properties(realtime_queue_stress_test PROPERTIES LABELS stress)

    add_executable(realtime_buffer_stress_test tests/integration/buffer_stress_test.cpp)
    target_link_libraries(realtime_buffer_stress_test PRIVATE realtime::realtime)
    add_test(NAME realtime_buffer_stress_test COMMAND realtime_buffer_stress_test)
    set_tests_properties(realtime_buffer_stress_test PROPERTIES LABELS stress)
  endif()
endif()

install(
  TARGETS realtime
  EXPORT realtime_targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
  cmake/realtimeConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/realtimeConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/realtime
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/realtimeConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(
  EXPORT realtime_targets
  FILE realtimeTargets.cmake
  NAMESPACE realtime::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/realtime
)
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/realtimeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/realtimeConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/realtime
)
