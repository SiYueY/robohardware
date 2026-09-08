if(NOT DEFINED ROBOHARDWARE_BUILD_TESTS)
  option(ROBOHARDWARE_BUILD_TESTS "Build RoboHardware tests" OFF)
endif()

add_library(can event.cpp socket.cpp)
add_library(can::can ALIAS can)
target_compile_features(can PUBLIC cxx_std_17)
target_include_directories(can PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(can PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(ROBOHARDWARE_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
