cmake_minimum_required(VERSION 3.20)

project(spi_consumer LANGUAGES CXX)

find_package(spi CONFIG REQUIRED)

add_executable(spi_consumer main.cpp)
target_link_libraries(spi_consumer PRIVATE spi::spi)
