add_library(serial port.cpp)
add_library(serial::serial ALIAS serial)
target_compile_features(serial PUBLIC cxx_std_17)
target_include_directories(serial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(serial PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(ROBOHARDWARE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
