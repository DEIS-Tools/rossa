cmake_minimum_required(VERSION 3.19.1)
project(rossa VERSION 0.9 DESCRIPTION "Simulator")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_executable(sim sim.cpp)
target_compile_features(sim PRIVATE cxx_std_20)
target_compile_options(sim PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(sim PRIVATE m)

if (DEFINED ENV{scheduler_lib_path})
    ## Used by rnetwork python module
    add_library(scheduler SHARED IMPORTED)
    set_target_properties(scheduler PROPERTIES IMPORTED_LOCATION $ENV{scheduler_lib_path})
    target_link_libraries(sim PRIVATE scheduler)
    if (DEFINED ENV{sim_model_path})
        target_compile_definitions(sim PRIVATE SIM_MODEL_PATH=$ENV{sim_model_path})
    endif ()
else ()
    ## For working with it independent of the python module
    add_subdirectory(schedulers)

    target_link_libraries(sim PRIVATE fixed)
#    target_link_libraries(sim PRIVATE rotor_lb)
#    target_link_libraries(sim PRIVATE valiant)
endif ()