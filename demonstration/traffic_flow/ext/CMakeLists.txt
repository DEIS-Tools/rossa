cmake_minimum_required(VERSION 3.19.1)

add_library(traffic_extobjs OBJECT traffic.cpp)
target_compile_features(traffic_extobjs PUBLIC cxx_std_17)
target_compile_options(traffic_extobjs PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(traffic_extobjs INTERFACE .)
