if(NOT DEFINED ROBOHARDWARE_BUILD_TESTS)
  option(ROBOHARDWARE_BUILD_TESTS "Build RoboHardware tests" OFF)
endif()
option(ROBOHARDWARE_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ROBOHARDWARE_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ROBOHARDWARE_REALTIME_NO_EXCEPTIONS "Compile only the realtime target without exceptions" OFF)

if(ROBOHARDWARE_ENABLE_ASAN_UBSAN AND ROBOHARDWARE_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan cannot be enabled together")
endif()

add_library(realtime
  affinity.cpp
  clock.cpp
  memory.cpp
  periodic_task.cpp
  scheduler.cpp)
add_library(realtime::realtime ALIAS realtime)

target_compile_features(realtime PUBLIC cxx_std_17)
# The parent directory preserves consumers' <realtime/*.hpp> include paths.
target_include_directories(realtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(realtime PUBLIC pthread)
target_compile_options(realtime PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(ROBOHARDWARE_REALTIME_NO_EXCEPTIONS)
  target_compile_options(realtime PRIVATE -fno-exceptions)
endif()

if(ROBOHARDWARE_ENABLE_ASAN_UBSAN)
  target_compile_options(realtime PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(realtime PUBLIC -fsanitize=address,undefined)
endif()
if(ROBOHARDWARE_ENABLE_TSAN)
  target_compile_options(realtime PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(realtime PUBLIC -fsanitize=thread)
endif()

add_subdirectory(benchmarks)

if(ROBOHARDWARE_BUILD_TESTS)
  include(CTest)
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14.0
    GIT_SHALLOW TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  add_subdirectory(tests)
endif()
