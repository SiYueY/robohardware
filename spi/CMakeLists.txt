cmake_minimum_required(VERSION 3.20)

project(spi VERSION 0.2.0 LANGUAGES CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "spi requires a Linux target")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
  spi
    src/device.cpp
    src/spidev_adapter_linux.cpp
)
add_library(spi::spi ALIAS spi)
target_compile_features(spi PUBLIC cxx_std_17)
target_compile_definitions(spi PRIVATE _GNU_SOURCE)
target_include_directories(
  spi
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(spi PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME spi)

if(BUILD_TESTING)
  function(spi_enable_test_assertions target)
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endfunction()

  add_library(
    spi_public_headers_compile_test
    OBJECT
      tests/public_headers/device.cpp
      tests/public_headers/config.cpp
  )
  target_compile_features(spi_public_headers_compile_test PRIVATE cxx_std_17)
  target_include_directories(spi_public_headers_compile_test PRIVATE include)

  add_executable(
    spi_unit_test
      tests/unit/device_test.cpp
      tests/support/controlled_spidev_adapter.cpp
      src/device.cpp
  )
  target_compile_features(spi_unit_test PRIVATE cxx_std_17)
  target_compile_definitions(spi_unit_test PRIVATE _GNU_SOURCE)
  spi_enable_test_assertions(spi_unit_test)
  target_include_directories(
    spi_unit_test
    PRIVATE
      include
      src
      tests/support
  )
  add_test(NAME spi.unit.device COMMAND spi_unit_test)

  configure_file(
    tests/consumer/consumer_test.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/consumer_test.cmake
    @ONLY
  )
  add_test(
    NAME spi.consumer.install
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/consumer_test.cmake
  )
endif()

install(
  TARGETS spi
  EXPORT spi_targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(
  cmake/spiConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/spiConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spi
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/spiConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(
  EXPORT spi_targets
  FILE spiTargets.cmake
  NAMESPACE spi::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spi
)
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/spiConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/spiConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spi
)
