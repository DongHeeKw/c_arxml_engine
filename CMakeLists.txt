cmake_minimum_required(VERSION 3.20)

project(autosar_dependency_studio VERSION 0.1.0 LANGUAGES CXX)

add_executable(studio-engine
    engine/src/app/main.cpp
    engine/src/app/json.cpp
)

target_include_directories(studio-engine PRIVATE engine/include)
target_compile_features(studio-engine PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(studio-engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(studio-engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(studio-engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engine"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    set_target_properties(studio-engine PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/engine"
    )
endforeach()
