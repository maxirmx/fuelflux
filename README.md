<!--
Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
All rights reserved.
This file is a part of fuelflux application
-->

﻿# fuelflux
FuelFlux Controller Software - A modern fuel station management system with comprehensive logging.

## Features

- **State Machine Architecture**: Robust state management for fuel station operations
- **Peripheral Management**: Support for displays, keyboards, card readers, pumps, and flow meters
- **Hardware Display Support**: Native integration with NHD-C12864A1Z-FSW-FBW-HTT LCD display on ARM platforms
- **Backend Integration**: Real API integration for user authentication and transaction reporting
- **Advanced Logging**: Multi-level, multi-sink logging with JSON configuration
- **Console Emulation**: Full console-based testing and simulation environment

## Quick Start

### Build and Run (Development)

Use CMake + Ninja to build and debug in VS Code. Recommended steps (PowerShell):

```powershell
# create build directory and configure
cmake -S . -B build -G "Ninja"

# build the project (Debug config)
cmake --build build --config Debug

# run the executable
.\build\bin\fuelflux.exe
```

### Installation (Production)

For production deployment:

```powershell
# Build release version
cmake -S . -B build
cmake --build build --config Release

# Install to default location (build/install)
cmake --install build --config Release

# Or install to custom location
cmake --install build --config Release --prefix C:\FuelFlux
```

## Project Structure

```
├── src/                    # Source files
├── include/               # Header files  
├── config/               # Configuration files
│   └── logging.json      # Logging configuration
├── docs/                 # Documentation
├── logs/                 # Runtime log files (created automatically)
└── build/                # Build artifacts
    └── install/          # Default installation directory
```

## Development

In VS Code:
- Open the Run and Debug view and select "Debug fuelflux (CMake Build)".
- Start debugging (F5). The preLaunchTask will build the project first.

If you use MSVC toolchain instead of Ninja/gcc, change the `-G "Ninja"` to your generator and adjust `miDebuggerPath` in `.vscode/launch.json` to the Visual Studio debugger (or use `cppvsdbg` type).

## Testing

The project includes a comprehensive test suite using Google Test and Google Mock.

### Running Tests

```bash
# Configure with testing enabled
cmake -S . -B build -DENABLE_TESTING=ON

# Build
cmake --build build

# Run all tests
cd build && ctest --output-on-failure
```

### DNS Resolution Tests

The DNS resolution tests are conditionally compiled based on the `TARGET_SIM800C` flag. To run these tests:

```bash
# Configure with SIM800C target enabled
cmake -S . -B build -DENABLE_TESTING=ON -DTARGET_SIM800C=ON

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

**Note**: When `TARGET_SIM800C` is disabled (default for x86 builds), the DNS resolution tests will be skipped. This is expected behavior since the DNS functionality requires the ppp0 interface binding which is specific to SIM800C hardware deployments.

The DNS tests verify:
- Error handling for empty and invalid hostnames
- Timeout behavior
- Edge cases (special characters, very long hostnames, etc.)
- Concurrent DNS resolution requests

## Documentation

- [State Machine](docs/state_machine.md) - Complete state machine workflow and transitions
- [Hardware Integration](docs/hardware.md) - Display and NFC wiring/configuration
- [Logging System](docs/logging.md) - Detailed logging configuration and usage
- [Installation Guide](docs/installation.md) - Complete installation instructions

## Dependencies

- **C++17** compatible compiler
- **CMake 3.16+** 
- **spdlog** - High performance logging library
- **nlohmann/json** - JSON parsing and configuration
- **libcurl** - HTTP client library
- **c-ares** (ARM/Linux with TARGET_SIM800C only) - DNS resolution library
- **libgpiod** (ARM/Linux only) - GPIO control for real hardware display
- **freetype2** (ARM/Linux only) - Font rendering for real hardware display
- **libnfc** (ARM/Linux only) - NFC card reader support

Core dependencies are automatically fetched via CMake FetchContent. Hardware dependencies (c-ares, libgpiod, freetype2, libnfc) are only required when building with corresponding target flags enabled.

### Installing System Dependencies on Debian/Ubuntu

For TARGET_SIM800C builds (ppp0 interface binding):
```bash
sudo apt-get install libc-ares-dev
```

For real hardware builds:
```bash
sudo apt-get install libgpiod-dev libfreetype-dev libnfc-dev
```
