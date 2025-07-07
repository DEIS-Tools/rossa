cmake_minimum_required(VERSION 3.19.1)

add_library(valiant SHARED 
    ext.cpp
)
target_compile_features(valiant PRIVATE cxx_std_17)
target_compile_options(valiant PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(valiant PRIVATE extobjs tgraph)
