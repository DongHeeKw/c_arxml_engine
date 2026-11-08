cmake_minimum_required(VERSION 3.20)

project(autosar_dependency_studio VERSION 0.1.0 LANGUAGES C CXX)

find_package(Threads REQUIRED)

add_library(studio-sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(studio-sqlite PUBLIC third_party/sqlite)
target_compile_definitions(studio-sqlite PRIVATE SQLITE_THREADSAFE=1)
set_target_properties(studio-sqlite PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

add_library(studio-core STATIC
    engine/src/app/json.cpp
    engine/src/dbc/parser.cpp
    engine/src/dbc/diff.cpp
    engine/src/project/project.cpp
    engine/src/project/project_store.cpp
    engine/src/project/digest.cpp
    engine/src/project/project_inventory.cpp
)

target_include_directories(studio-core PUBLIC engine/include)
target_compile_features(studio-core PUBLIC cxx_std_20)
target_link_libraries(studio-core PUBLIC Threads::Threads studio-sqlite)

add_executable(studio-engine engine/src/app/main.cpp)
target_link_libraries(studio-engine PRIVATE studio-core)

if(MSVC)
    target_compile_options(studio-engine PRIVATE /W4 /permissive-)
    target_compile_options(studio-core PRIVATE /W4 /permissive-)
else()
    target_compile_options(studio-engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(studio-core PRIVATE -Wall -Wextra -Wpedantic)
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
    add_executable(studio-engine-tests engine/tests/test_main.cpp)
    target_link_libraries(studio-engine-tests PRIVATE studio-core)
    if(MSVC)
        target_compile_options(studio-engine-tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(studio-engine-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME studio-engine-unit COMMAND studio-engine-tests)
    add_executable(studio-acceptance-tests engine/tests/test_acceptance.cpp)
    target_link_libraries(studio-acceptance-tests PRIVATE studio-core)
    add_test(NAME studio-acceptance COMMAND studio-acceptance-tests)
endif()
