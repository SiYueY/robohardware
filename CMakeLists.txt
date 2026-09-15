cmake_minimum_required(VERSION 3.20)

project(hardware VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "hardware requires a Linux target")
endif()

find_package(Threads REQUIRED)

function(hardware_module target export_set namespace)
  add_library(${namespace}::${target} ALIAS ${target})
  target_compile_features(${target} PUBLIC cxx_std_17)
  target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  set_target_properties(${target} PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME ${target})
  install(TARGETS ${target} EXPORT ${export_set}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT ${export_set} FILE ${target}Targets.cmake NAMESPACE ${namespace}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hardware)
endfunction()

add_library(realtime SHARED
  src/realtime/clock.cpp src/realtime/current_thread.cpp
  src/realtime/monotonic_time_linux.cpp src/realtime/periodic_schedule.cpp
  src/realtime/process_memory.cpp src/realtime/timing_statistics.cpp)
target_link_libraries(realtime PUBLIC Threads::Threads)
hardware_module(realtime realtimeTargets realtime)

add_library(serial SHARED
  src/serial/configuration.cpp src/serial/port.cpp
  src/serial/timeout.cpp src/serial/tty_adapter_linux.cpp)
hardware_module(serial serialTargets serial)

add_library(spi SHARED src/spi/device.cpp src/spi/spidev_adapter_linux.cpp)
hardware_module(spi spiTargets spi)

add_library(can SHARED src/can/can.cpp)
hardware_module(can canTargets can)

if(BUILD_TESTING)
  add_executable(hardware_result_test tests/hardware/result_test.cpp)
  target_compile_features(hardware_result_test PRIVATE cxx_std_17)
  target_include_directories(hardware_result_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME hardware.result COMMAND hardware_result_test)
  foreach(module IN ITEMS realtime serial spi can)
    add_executable(${module}_api_test tests/${module}/api_test.cpp)
    target_compile_features(${module}_api_test PRIVATE cxx_std_17)
    target_link_libraries(${module}_api_test PRIVATE ${module}::${module})
    add_test(NAME ${module}.api COMMAND ${module}_api_test)
  endforeach()
  foreach(module IN ITEMS realtime serial spi)
    add_executable(${module}_result_behavior_test tests/${module}/result_behavior_test.cpp)
    target_link_libraries(${module}_result_behavior_test PRIVATE ${module}::${module})
    add_test(NAME ${module}.result_behavior COMMAND ${module}_result_behavior_test)
  endforeach()
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(cmake/hardwareConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/hardwareConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hardware)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/hardwareConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hardwareConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/hardwareConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hardware)
