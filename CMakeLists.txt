cmake_minimum_required(VERSION 3.20)

project(robo_hardware LANGUAGES CXX)

include(CTest)

option(REALTIME_BUILD "Build the realtime component" ON)
option(SERIAL_BUILD "Build the serial component" OFF)
option(CAN_BUILD "Build the can component" OFF)

if(REALTIME_BUILD)
  add_subdirectory(realtime)
endif()
