cmake_minimum_required(VERSION 3.20)

project(autosar_dependency_studio VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

add_executable(studio-engine
    engine/src/app/main.cpp
    engine/src/app/json.cpp
    engine/src/dbc/parser.cpp
    engine/src/dbc/diff.cpp
    engine/src/project/project.cpp
)

target_include_directories(studio-engine PRIVATE engine/include)
target_compile_features(studio-engine PRIVATE cxx_std_20)
target_link_libraries(studio-engine PRIVATE Threads::Threads)

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

include(CTest)
if(BUILD_TESTING)
    add_executable(studio-engine-tests
        engine/tests/test_main.cpp
        engine/src/app/json.cpp
        engine/src/dbc/parser.cpp
        engine/src/dbc/diff.cpp
        engine/src/project/project.cpp
    )
    target_include_directories(studio-engine-tests PRIVATE engine/include)
    target_compile_features(studio-engine-tests PRIVATE cxx_std_20)
    target_link_libraries(studio-engine-tests PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(studio-engine-tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(studio-engine-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME studio-engine-unit COMMAND studio-engine-tests)
endif()
