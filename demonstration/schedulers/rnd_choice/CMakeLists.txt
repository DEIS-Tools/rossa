cmake_minimum_required(VERSION 3.19.1)

add_library(rnd_choice SHARED
  graph_ext.cpp
)
target_compile_features(rnd_choice PUBLIC cxx_std_17)
target_compile_options(rnd_choice PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rnd_choice PRIVATE extobjs tgraph)

