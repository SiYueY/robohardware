cmake_minimum_required(VERSION 3.20)
project(serial_consumer LANGUAGES CXX)

find_package(serial CONFIG REQUIRED)

add_executable(serial_consumer main.cpp)
target_link_libraries(serial_consumer PRIVATE serial::serial)
