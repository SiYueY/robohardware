cmake_minimum_required(VERSION 3.20)

project(serial VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "serial requires a Linux target")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
  serial
    src/configuration.cpp
    src/error.cpp
    src/port.cpp
    src/timeout.cpp
    src/tty_adapter_linux.cpp
)
add_library(serial::serial ALIAS serial)
target_compile_features(serial PUBLIC cxx_std_17)
target_compile_definitions(serial PRIVATE _GNU_SOURCE)
target_include_directories(
  serial
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(serial PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME serial)

if(BUILD_TESTING)
  add_library(
    serial_public_headers_compile_test
    OBJECT
      tests/public_headers/configuration.cpp
      tests/public_headers/error.cpp
      tests/public_headers/port.cpp
      tests/public_headers/timeout.cpp
      tests/public_headers/transfer.cpp
  )
  target_compile_features(serial_public_headers_compile_test PRIVATE cxx_std_17)
  target_include_directories(serial_public_headers_compile_test PRIVATE include)

  add_executable(
    serial_unit_test
      tests/unit/port_test.cpp
      tests/support/controlled_tty_adapter.cpp
      src/configuration.cpp
      src/error.cpp
      src/port.cpp
      src/timeout.cpp
  )
  target_compile_features(serial_unit_test PRIVATE cxx_std_17)
  target_compile_definitions(serial_unit_test PRIVATE _GNU_SOURCE)
  target_compile_options(serial_unit_test PRIVATE -UNDEBUG)
  target_include_directories(
    serial_unit_test
    PRIVATE
      include
      src
      tests/support
  )
  add_test(NAME serial.unit.port COMMAND serial_unit_test)

  add_executable(serial_integration_test tests/integration/pty_test.cpp)
  target_link_libraries(serial_integration_test PRIVATE serial::serial)
  target_compile_options(serial_integration_test PRIVATE -UNDEBUG)
  add_test(NAME serial.integration.pty COMMAND serial_integration_test)

  add_executable(serial_simulator_test tests/simulators/device_simulator_test.cpp)
  target_link_libraries(serial_simulator_test PRIVATE serial::serial)
  target_compile_options(serial_simulator_test PRIVATE -UNDEBUG)
  add_test(NAME serial.simulator.device_behavior COMMAND serial_simulator_test)

  configure_file(
    tests/consumer/consumer_test.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/consumer_test.cmake
    @ONLY
  )
  add_test(
    NAME serial.consumer.install
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/consumer_test.cmake
  )
endif()

install(
  TARGETS serial
  EXPORT serial_targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(
  cmake/serialConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/serialConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/serial
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/serialConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(
  EXPORT serial_targets
  FILE serialTargets.cmake
  NAMESPACE serial::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/serial
)
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/serialConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/serialConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/serial
)
