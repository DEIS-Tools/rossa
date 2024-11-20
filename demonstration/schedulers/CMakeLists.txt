cmake_minimum_required(VERSION 3.19.1)
project(ext VERSION 0.9 DESCRIPTION "UPPAAL Scheduling logic")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(ext)
add_subdirectory(tgraph)
add_subdirectory(fixed)
add_subdirectory(rnd_choice)
add_subdirectory(capacity)
