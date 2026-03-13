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
    tests/unit/placeholder_test.cpp
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
    tests/property/placeholder_test.cpp
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

# Installation
install(TARGETS caravault DESTINATION bin)
