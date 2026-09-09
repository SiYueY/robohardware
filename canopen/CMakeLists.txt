add_library(canopen
  object_dictionary.cpp
  pdo.cpp
  node.cpp
  network.cpp
  sdo.cpp
  cia402/cia402.cpp
  cia402/virtual_motor.cpp)
add_library(canopen::canopen ALIAS canopen)
target_compile_features(canopen PUBLIC cxx_std_17)
target_include_directories(canopen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(canopen PUBLIC can::can)
target_compile_options(canopen PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(ROBOHARDWARE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
