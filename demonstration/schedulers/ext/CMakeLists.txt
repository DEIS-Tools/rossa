cmake_minimum_required(VERSION 3.19.1)

add_library(extobjs OBJECT 
    ext.cpp
)
target_compile_features(extobjs PUBLIC cxx_std_17)
target_compile_options(extobjs PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(extobjs INTERFACE ".")
