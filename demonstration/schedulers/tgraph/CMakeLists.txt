cmake_minimum_required(VERSION 3.19.1)

find_package(Boost 1.83 REQUIRED)

add_library(tgraph OBJECT temporal_graph.cpp)
target_compile_features(tgraph PUBLIC cxx_std_17)
target_compile_options(tgraph PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(tgraph PUBLIC "." $(Boost_INCLUDE_DIRS))
target_link_libraries(tgraph PUBLIC extobjs)
