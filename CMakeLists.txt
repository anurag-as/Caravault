cmake_minimum_required(VERSION 3.20)
project(Caravault VERSION 1.0.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# macOS specific: Ensure proper SDK is used
if(APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# Compiler flags for warnings and optimizations
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_options($<$<CONFIG:Release>:/O2>)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    add_compile_options($<$<CONFIG:Release>:-O3>)
    add_compile_options($<$<CONFIG:Debug>:-g>)
endif()

# Find required dependencies
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

# CLI11 (header-only library)
include(FetchContent)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

# RapidCheck for property-based testing
FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG master
)
FetchContent_MakeAvailable(rapidcheck)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Main executable
add_executable(caravault
    src/main.cpp
    src/version_vector.cpp
    src/manifest_store.cpp
    src/merkle_engine.cpp
    src/bloom_filter.cpp
)

target_link_libraries(caravault
    PRIVATE
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    CLI11::CLI11
)

# Enable testing
enable_testing()

# Unit tests
add_executable(unit_tests
    tests/unit/version_vector_test.cpp
    tests/unit/manifest_store_test.cpp
    tests/unit/bloom_filter_test.cpp
    src/version_vector.cpp
    src/manifest_store.cpp
    src/merkle_engine.cpp
    src/bloom_filter.cpp
)

target_link_libraries(unit_tests
    PRIVATE
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    GTest::gtest_main
)

# Property-based tests
add_executable(property_tests
    tests/property/version_vector_property_test.cpp
    tests/property/manifest_store_property_test.cpp
    tests/property/merkle_engine_property_test.cpp
    src/version_vector.cpp
    src/manifest_store.cpp
    src/merkle_engine.cpp
    src/bloom_filter.cpp
)

target_include_directories(property_tests
    PRIVATE
    ${rapidcheck_SOURCE_DIR}/include
    ${rapidcheck_SOURCE_DIR}/extras/gtest/include
)

target_link_libraries(property_tests
    PRIVATE
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    rapidcheck
    GTest::gtest_main
)

# Integration tests
add_executable(integration_tests
    tests/integration/placeholder_test.cpp
    src/version_vector.cpp
    src/manifest_store.cpp
    src/merkle_engine.cpp
    src/bloom_filter.cpp
)

target_link_libraries(integration_tests
    PRIVATE
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    GTest::gtest_main
)

# Register tests with CTest
add_test(NAME UnitTests COMMAND unit_tests)
add_test(NAME PropertyTests COMMAND property_tests)
add_test(NAME IntegrationTests COMMAND integration_tests)

# clang-format targets
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
        "${PROJECT_SOURCE_DIR}/include/*.hpp"
        "${PROJECT_SOURCE_DIR}/tests/*.cpp"
        "${PROJECT_SOURCE_DIR}/examples/*.cpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Formatting source files with clang-format"
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Checking source formatting with clang-format"
    )
endif()

# Installation
install(TARGETS caravault DESTINATION bin)
