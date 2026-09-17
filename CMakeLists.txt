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
  src/serial/port.cpp src/serial/tool.cpp src/serial/tty.cpp)
hardware_module(serial serialTargets serial)
target_compile_options(serial PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_library(spi SHARED src/spi/device.cpp src/spi/spidev.cpp)
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
  add_executable(serial_port_behavior_test
    src/serial/port.cpp
    tests/serial/port_behavior_test.cpp
    tests/serial/tty_fake.cpp)
  target_compile_features(serial_port_behavior_test PRIVATE cxx_std_17)
  target_compile_definitions(serial_port_behavior_test PRIVATE _GNU_SOURCE)
  target_compile_options(serial_port_behavior_test PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  target_include_directories(serial_port_behavior_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/serial
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/serial)
  add_test(NAME serial.port_behavior COMMAND serial_port_behavior_test)
  add_executable(serial_pty_integration_test tests/serial/pty_integration_test.cpp)
  target_link_libraries(serial_pty_integration_test PRIVATE serial::serial)
  add_test(NAME serial.pty_integration COMMAND serial_pty_integration_test)
  add_executable(spi_device_behavior_test
    src/spi/device.cpp
    tests/spi/device_behavior_test.cpp
    tests/spi/spidev_fake.cpp)
  target_compile_features(spi_device_behavior_test PRIVATE cxx_std_17)
  target_include_directories(spi_device_behavior_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/spi
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/spi)
  add_test(NAME spi.device_behavior COMMAND spi_device_behavior_test)
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
