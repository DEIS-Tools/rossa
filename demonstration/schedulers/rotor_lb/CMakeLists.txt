cmake_minimum_required(VERSION 3.19.1)

add_library(rotor_lb SHARED 
    ext.cpp
)
target_compile_features(rotor_lb PRIVATE cxx_std_20)
target_compile_options(rotor_lb PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rotor_lb PRIVATE extobjs tgraph)
