cmake_minimum_required(VERSION 3.19.1)

add_library(traffic_gravity_model SHARED gravity_model.cpp)
target_compile_features(traffic_gravity_model PUBLIC cxx_std_23)
target_compile_options(traffic_gravity_model PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(traffic_gravity_model PRIVATE traffic_extobjs)
