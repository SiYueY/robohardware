cmake_minimum_required(VERSION 3.20)

project(realtime_consumer LANGUAGES CXX)

find_package(realtime CONFIG REQUIRED)

add_executable(realtime_consumer main.cpp)
target_link_libraries(realtime_consumer PRIVATE realtime::realtime)
target_compile_features(realtime_consumer PRIVATE cxx_std_17)
