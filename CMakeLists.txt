cmake_minimum_required(VERSION 3.16)
project(FuelFlux VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/state_machine.cpp
    src/controller.cpp
    src/peripherals/display.cpp
    src/peripherals/keyboard.cpp
    src/peripherals/card_reader.cpp
    src/peripherals/pump.cpp
    src/peripherals/flow_meter.cpp
    src/console_emulator.cpp
    src/cloud_service.cpp
)

# Headers
set(HEADERS
    include/state_machine.h
    include/controller.h
    include/peripherals/display.h
    include/peripherals/keyboard.h
    include/peripherals/card_reader.h
    include/peripherals/pump.h
    include/peripherals/flow_meter.h
    include/peripherals/peripheral_interface.h
    include/console_emulator.h
    include/cloud_service.h
    include/types.h
)

# Create executable
add_executable(fuelflux ${SOURCES} ${HEADERS})

# Set output directory
set_target_properties(fuelflux PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install target
install(TARGETS fuelflux DESTINATION bin)
