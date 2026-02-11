[![Build FuelFlux](https://github.com/maxirmx/fuelflux/actions/workflows/build.yml/badge.svg)](https://github.com/maxirmx/fuelflux/actions/workflows/build.yml)
﻿
# FuelFlux Controller Software
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
- **libgpiod** (ARM/Linux only) - GPIO control for real hardware display
- **freetype2** (ARM/Linux only) - Font rendering for real hardware display

Core dependencies are automatically fetched via CMake FetchContent. Hardware dependencies (libgpiod, freetype2) are only required when building with `TARGET_REAL_HARDWARE=ON`.
