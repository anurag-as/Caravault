cmake_minimum_required(VERSION 3.20)
project(Caravault VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_options($<$<CONFIG:Release>:/O2>)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    add_compile_options($<$<CONFIG:Release>:-O3>)
    add_compile_options($<$<CONFIG:Debug>:-g>)
endif()

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

include(FetchContent)

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG master
)
FetchContent_MakeAvailable(rapidcheck)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_executable(caravault src/main.cpp ${LIB_SOURCES})
target_link_libraries(caravault PRIVATE SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto CLI11::CLI11)

enable_testing()

file(GLOB_RECURSE UNIT_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
add_executable(unit_tests ${UNIT_TEST_SOURCES} ${LIB_SOURCES})
target_link_libraries(unit_tests PRIVATE SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto GTest::gtest_main)

file(GLOB_RECURSE PROPERTY_TEST_SOURCES CONFIGURE_DEPENDS tests/property/*.cpp)
add_executable(property_tests ${PROPERTY_TEST_SOURCES} ${LIB_SOURCES})
target_include_directories(property_tests PRIVATE
    ${rapidcheck_SOURCE_DIR}/include
    ${rapidcheck_SOURCE_DIR}/extras/gtest/include
)
target_link_libraries(property_tests PRIVATE
    SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto rapidcheck GTest::gtest_main
)

file(GLOB_RECURSE INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS tests/integration/*.cpp)
add_executable(integration_tests ${INTEGRATION_TEST_SOURCES} ${LIB_SOURCES})
target_link_libraries(integration_tests PRIVATE SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto GTest::gtest_main)

add_test(NAME UnitTests COMMAND unit_tests)
add_test(NAME PropertyTests COMMAND property_tests)
add_test(NAME IntegrationTests COMMAND integration_tests)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES CONFIGURE_DEPENDS
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

install(TARGETS caravault DESTINATION bin)
