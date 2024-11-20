cmake_minimum_required(VERSION 3.19.1)

add_library(capacity SHARED 
    ext.cpp
)
target_compile_features(capacity PRIVATE cxx_std_17)
target_compile_options(capacity PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(capacity PRIVATE extobjs tgraph)
