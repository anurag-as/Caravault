cmake_minimum_required(VERSION 3.20)
project(Caravault VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Platform-specific setup
# ---------------------------------------------------------------------------

if(APPLE)
    # Locate the active Xcode/CommandLineTools SDK so headers are found.
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    # Homebrew installs OpenSSL and SQLite under its prefix; add it as a hint.
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(HOMEBREW_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH
            "${HOMEBREW_PREFIX}/opt/openssl"
            "${HOMEBREW_PREFIX}/opt/sqlite"
        )
    endif()
endif()

if(WIN32)
    # Target Windows 10 (0x0A00) or later for full API coverage.
    add_compile_definitions(_WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------

if(MSVC)
    add_compile_options(/W4 /WX /utf-8)
    add_compile_options($<$<CONFIG:Release>:/O2>)
    # Ensure Google Test and our code use the same CRT (dynamic by default).
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    add_compile_options($<$<CONFIG:Release>:-O3>)
    add_compile_options($<$<CONFIG:Debug>:-g>)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# Windows-specific system libraries required by OpenSSL and std::filesystem
# ---------------------------------------------------------------------------

set(PLATFORM_LIBS "")
if(WIN32)
    list(APPEND PLATFORM_LIBS ws2_32 crypt32 bcrypt)
    # MinGW needs stdc++fs for <filesystem> on older toolchains.
    if(MINGW)
        list(APPEND PLATFORM_LIBS stdc++fs)
    endif()
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------

add_executable(caravault src/main.cpp ${LIB_SOURCES})
target_link_libraries(caravault PRIVATE
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    CLI11::CLI11
    ${PLATFORM_LIBS}
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

file(GLOB_RECURSE UNIT_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
add_executable(unit_tests ${UNIT_TEST_SOURCES} ${LIB_SOURCES})
target_link_libraries(unit_tests PRIVATE
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    GTest::gtest_main
    ${PLATFORM_LIBS}
)

file(GLOB_RECURSE PROPERTY_TEST_SOURCES CONFIGURE_DEPENDS tests/property/*.cpp)
add_executable(property_tests ${PROPERTY_TEST_SOURCES} ${LIB_SOURCES})
target_include_directories(property_tests PRIVATE
    ${rapidcheck_SOURCE_DIR}/include
    ${rapidcheck_SOURCE_DIR}/extras/gtest/include
)
target_link_libraries(property_tests PRIVATE
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    rapidcheck
    GTest::gtest_main
    ${PLATFORM_LIBS}
)
# CLI property tests invoke the caravault binary; ensure it is built first.
add_dependencies(property_tests caravault)
# Pass the absolute path to the caravault binary so CLI tests can find it.
target_compile_definitions(property_tests PRIVATE
    CARAVAULT_BINARY_PATH="$<TARGET_FILE:caravault>"
)

file(GLOB_RECURSE INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS tests/integration/*.cpp)
add_executable(integration_tests ${INTEGRATION_TEST_SOURCES} ${LIB_SOURCES})
target_link_libraries(integration_tests PRIVATE
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    GTest::gtest_main
    ${PLATFORM_LIBS}
)

add_test(NAME UnitTests COMMAND unit_tests)
add_test(NAME PropertyTests COMMAND property_tests)
add_test(NAME IntegrationTests COMMAND integration_tests)

# ---------------------------------------------------------------------------
# Code formatting (optional, requires clang-format)
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS caravault DESTINATION bin)

# ---------------------------------------------------------------------------
# Packaging (CPack)
# ---------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "caravault")
set(CPACK_PACKAGE_VENDOR "Caravault")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline multi-drive file synchronization system")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES TRUE)

if(WIN32)
    # NSIS installer — NSIS must be installed on the build host
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Caravault ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "Caravault")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Caravault")
    set(CPACK_PACKAGE_FILE_NAME "caravault-${PROJECT_VERSION}-windows-x64")
else()
    # .deb and .rpm on Linux
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGE_FILE_NAME "caravault-${PROJECT_VERSION}-linux-x86_64")

    # DEB-specific
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Caravault Maintainers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsqlite3-0, libssl3 | libssl1.1")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/caravault/caravault")

    # RPM-specific
    set(CPACK_RPM_PACKAGE_LICENSE "See LICENSE")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_REQUIRES "sqlite-libs, openssl-libs")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/caravault/caravault")
endif()

include(CPack)
