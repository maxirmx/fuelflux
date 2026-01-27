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

# Real hardware peripheral flags: set by default on ARM, unset on Windows
if(MSVC)
    option(TARGET_REAL_DISPLAY "Use real NHD display hardware" OFF)
    option(TARGET_REAL_KEYBOARD "Use real keyboard hardware" OFF)
    option(TARGET_REAL_CARD_READER "Use real card reader hardware" OFF)
    option(TARGET_REAL_PUMP "Use real pump hardware" OFF)
    option(TARGET_REAL_FLOW_METER "Use real flow meter hardware" OFF)
else()
    option(TARGET_REAL_DISPLAY "Use real NHD display hardware" OFF)   # OFF for now
    option(TARGET_REAL_KEYBOARD "Use real keyboard hardware" OFF)
    option(TARGET_REAL_CARD_READER "Use real card reader hardware" ON)
    option(TARGET_REAL_PUMP "Use real pump hardware" OFF)
    option(TARGET_REAL_FLOW_METER "Use real flow meter hardware" OFF)
endif()

if(TARGET_REAL_DISPLAY)
    message(STATUS "TARGET_REAL_DISPLAY enabled - using real display hardware")
    add_compile_definitions(TARGET_REAL_DISPLAY)
else()
    message(STATUS "TARGET_REAL_DISPLAY disabled - using console display emulation")
endif()

if(TARGET_REAL_KEYBOARD)
    message(STATUS "TARGET_REAL_KEYBOARD enabled (placeholder - not yet implemented)")
endif()
if(TARGET_REAL_CARD_READER)
    message(STATUS "TARGET_REAL_CARD_READER enabled - using real NFC card reader")
    add_compile_definitions(TARGET_REAL_CARD_READER)
endif()
if(TARGET_REAL_PUMP)
    message(STATUS "TARGET_REAL_PUMP enabled (placeholder - not yet implemented)")
endif()
if(TARGET_REAL_FLOW_METER)
    message(STATUS "TARGET_REAL_FLOW_METER enabled (placeholder - not yet implemented)")
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

# Make dependencies available
FetchContent_MakeAvailable(fmt nlohmann_json spdlog httplib)

# Conditional dependencies for real hardware
if(TARGET_REAL_DISPLAY OR TARGET_REAL_CARD_READER)
    find_package(PkgConfig REQUIRED)
endif()

if(TARGET_REAL_DISPLAY)
    pkg_check_modules(GPIOD REQUIRED libgpiod)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    message(STATUS "Found libgpiod: ${GPIOD_LIBRARIES}")
    message(STATUS "Found freetype2: ${FREETYPE_LIBRARIES}")
endif()

if(TARGET_REAL_CARD_READER)
    pkg_check_modules(LIBNFC REQUIRED libnfc)
    message(STATUS "Found libnfc: ${LIBNFC_LIBRARIES}")
endif()

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

# Add NHD library sources if TARGET_REAL_DISPLAY is enabled
if(TARGET_REAL_DISPLAY)
    list(APPEND SOURCES
        src/nhd/st7565.cpp
        src/nhd/four_line_display.cpp
        src/nhd/graphics.cpp
        src/nhd/ft_text.cpp
        src/nhd/spi_linux.cpp
        src/nhd/gpio_gpiod.cpp
    )
endif()

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

# Add NHD library headers if TARGET_REAL_DISPLAY is enabled
if(TARGET_REAL_DISPLAY)
    list(APPEND HEADERS
        include/nhd/st7565.h
        include/nhd/four_line_display.h
        include/nhd/graphics.h
        include/nhd/ft_text.h
        include/nhd/spi_linux.h
        include/nhd/gpio_gpiod.h
    )
endif()

# Create executable
add_executable(fuelflux ${SOURCES} ${HEADERS})

# Add include directories for real hardware libraries
if(TARGET_REAL_DISPLAY)
    target_include_directories(fuelflux PRIVATE ${GPIOD_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
endif()

if(TARGET_REAL_CARD_READER)
    target_include_directories(fuelflux PRIVATE ${LIBNFC_INCLUDE_DIRS})
endif()

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

# Link real hardware libraries if TARGET_REAL_DISPLAY is enabled
if(TARGET_REAL_DISPLAY)
    target_link_libraries(fuelflux PRIVATE ${GPIOD_LIBRARIES} ${FREETYPE_LIBRARIES})
endif()

if(TARGET_REAL_CARD_READER)
    target_link_libraries(fuelflux PRIVATE ${LIBNFC_LIBRARIES})
endif()

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

    # Fetch Google Test (MSVC only)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

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
    
    # Add NHD library sources if TARGET_REAL_DISPLAY is enabled
    if(TARGET_REAL_DISPLAY)
        list(APPEND LIB_SOURCES
            src/nhd/st7565.cpp
            src/nhd/four_line_display.cpp
            src/nhd/graphics.cpp
            src/nhd/ft_text.cpp
            src/nhd/spi_linux.cpp
            src/nhd/gpio_gpiod.cpp
        )
    endif()

    # Create library from application sources
    add_library(fuelflux_lib STATIC ${LIB_SOURCES} ${HEADERS})
    
    # Add include directories for real hardware libraries
    if(TARGET_REAL_DISPLAY)
        target_include_directories(fuelflux_lib PUBLIC ${GPIOD_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
    endif()
    if(TARGET_REAL_CARD_READER)
        target_include_directories(fuelflux_lib PUBLIC ${LIBNFC_INCLUDE_DIRS})
    endif()

    # Link libraries to library target
    target_link_libraries(fuelflux_lib
        PUBLIC
        fmt::fmt
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        httplib::httplib
    )
    
    # Link real hardware libraries if TARGET_REAL_DISPLAY is enabled
    if(TARGET_REAL_DISPLAY)
        target_link_libraries(fuelflux_lib PUBLIC ${GPIOD_LIBRARIES} ${FREETYPE_LIBRARIES})
    endif()
    if(TARGET_REAL_CARD_READER)
        target_link_libraries(fuelflux_lib PUBLIC ${LIBNFC_LIBRARIES})
    endif()

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
