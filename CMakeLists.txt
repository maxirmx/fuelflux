# Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
# All rights reserved.
# This file is a part of fuelflux application

cmake_minimum_required(VERSION 3.16)
project(FuelFlux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific options
if(MSVC)
    add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4)
    add_compile_options(/utf-8) 
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-psabi)
endif()

# Set default install prefix to a more reasonable location
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

# Include FetchContent module
include(FetchContent)

# Use fmt directly and share it with spdlog to avoid duplicate bundled copies
set(SPDLOG_FMT_EXTERNAL ON)

# Fetch fmt
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.0.2
)

# Fetch nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
)

# Fetch spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.12.0
)

# Fetch cpp-httplib
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
)

# Fetch Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Make dependencies available
FetchContent_MakeAvailable(fmt nlohmann_json spdlog httplib googletest)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/state_machine.cpp
    src/controller.cpp
    src/logger.cpp
    src/peripherals/display.cpp
    src/peripherals/keyboard.cpp
    src/peripherals/card_reader.cpp
    src/peripherals/pump.cpp
    src/peripherals/flow_meter.cpp
    src/console_emulator.cpp
    src/backend.cpp
)

# Headers
set(HEADERS
    include/config.h
    include/state_machine.h
    include/controller.h
    include/logger.h
    include/peripherals/display.h
    include/peripherals/keyboard.h
    include/peripherals/card_reader.h
    include/peripherals/pump.h
    include/peripherals/flow_meter.h
    include/peripherals/peripheral_interface.h
    include/console_emulator.h
    include/backend.h
    include/types.h
)

# Create executable
add_executable(fuelflux ${SOURCES} ${HEADERS})

# Try to find OpenSSL and enable cpp-httplib SSL support (optional)
find_package(OpenSSL)
if(OpenSSL_FOUND)
    message(STATUS "Found OpenSSL: enabling cpp-httplib SSL support")
    target_compile_definitions(fuelflux PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(fuelflux PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
    message(WARNING "OpenSSL not found. HTTPS support will be disabled, builds will succeed but HTTPS backends will be rejected at runtime.")
endif()

# Link libraries
target_link_libraries(fuelflux 
    PRIVATE 
    fmt::fmt
    nlohmann_json::nlohmann_json 
    spdlog::spdlog
    httplib::httplib
)

# Set output directory
set_target_properties(fuelflux PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install target
install(TARGETS fuelflux DESTINATION bin)

# Install configuration files
# The logging.json file will be installed to <install_prefix>/config/
# At runtime, the application looks for config/logging.json relative to working directory
install(FILES config/logging.json DESTINATION config)

# Ensure the config directory is copied next to the built executable for each build
add_custom_command(TARGET fuelflux POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:fuelflux>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/logging.json"
        "$<TARGET_FILE_DIR:fuelflux>/config/logging.json"
    COMMENT "Copy logging.json to output config folder"
)

# Testing
if(MSVC)

    enable_testing()

    # Test source files
    set(TEST_SOURCES
        tests/backend_test.cpp
        tests/controller_test.cpp
    )

    # Library sources (excluding main.cpp)
    set(LIB_SOURCES
        src/state_machine.cpp
        src/controller.cpp
        src/logger.cpp
        src/peripherals/display.cpp
        src/peripherals/keyboard.cpp
        src/peripherals/card_reader.cpp
        src/peripherals/pump.cpp
        src/peripherals/flow_meter.cpp
        src/console_emulator.cpp
        src/backend.cpp
    )

    # Create library from application sources
    add_library(fuelflux_lib STATIC ${LIB_SOURCES} ${HEADERS})

    # Link libraries to library target
    target_link_libraries(fuelflux_lib
        PUBLIC
        fmt::fmt
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        httplib::httplib
    )

    # Handle OpenSSL for library
    if(OpenSSL_FOUND)
        target_compile_definitions(fuelflux_lib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
        target_link_libraries(fuelflux_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()

    # Update main executable to use library
    target_link_libraries(fuelflux PRIVATE fuelflux_lib)

    # Create test executable
    add_executable(fuelflux_tests ${TEST_SOURCES})

    # Link test executable with library and Google Test
    target_link_libraries(fuelflux_tests
        PRIVATE
        fuelflux_lib
        GTest::gtest_main
        GTest::gmock
    )

    # Set output directory for tests
    set_target_properties(fuelflux_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Ensure config directory is available for tests
    add_custom_command(TARGET fuelflux_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:fuelflux_tests>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/logging.json"
            "$<TARGET_FILE_DIR:fuelflux_tests>/config/logging.json"
        COMMENT "Copy logging.json to test output config folder"
    )

    # Discover tests
    include(GoogleTest)
gtest_discover_tests(fuelflux_tests)

endif(MSVC)
