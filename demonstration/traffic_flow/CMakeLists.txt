cmake_minimum_required(VERSION 3.19.1)
project(traffic-ext VERSION 0.9 DESCRIPTION "Traffic flow generation")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(ext)
add_subdirectory(gravity)
