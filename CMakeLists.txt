cmake_minimum_required(VERSION 3.22)
project(robohardware LANGUAGES CXX)

option(ROBOHARDWARE_BUILD_TESTS "Build RoboHardware tests" ON)

add_subdirectory(realtime)
