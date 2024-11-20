cmake_minimum_required(VERSION 3.19.1)

find_package(Boost 1.83 REQUIRED)

add_library(fixed SHARED
  graph_ext.cpp
)
target_compile_features(fixed PUBLIC cxx_std_17)
target_compile_options(fixed PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(fixed PRIVATE extobjs tgraph)

