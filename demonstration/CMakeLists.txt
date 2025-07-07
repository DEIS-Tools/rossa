cmake_minimum_required(VERSION 3.19.1)
project(rossa VERSION 0.9 DESCRIPTION "Simulator")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(schedulers)

add_executable(sim sim.cpp)
target_compile_features(sim PRIVATE cxx_std_20)
target_compile_options(sim PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(sim m rotor_lb)
#target_link_libraries(sim m valiant)
