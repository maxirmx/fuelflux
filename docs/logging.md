# FuelFlux Logging System

This document describes the spdlog integration in the FuelFlux application.

## Features

- **Multiple logger categories**: StateMachine, Controller, Peripherals, CloudService, and main application
- **Multiple output sinks**: Console (with colors), rotating file logs, and error-specific logs
- **Configurable levels**: Debug, Info, Warning, Error, Critical
- **Async logging**: High-performance asynchronous logging for minimal impact on application performance
- **JSON configuration**: Easy configuration management via JSON file

## Configuration

The logging system is configured via `config/logging.json`. The configuration includes:

### Sinks
- **console**: Colored console output for info level and above
- **file**: Rotating log files (10MB max, 5 files) for debug level and above
- **error_file**: Separate error log files (5MB max, 3 files) for warnings and above

### Loggers
- **fuelflux**: Main application logger
- **StateMachine**: State machine events and transitions
- **Controller**: Controller operations and peripheral management
- **Peripherals**: Hardware peripheral operations
- **CloudService**: Cloud service communications and transactions

## Usage Examples

### Basic Logging
```cpp
#include "logger.h"

// Initialize logging system (done once in main)
Logger::initialize(); // Uses default config/logging.json path

// Use category-specific loggers
LOG_SM_INFO("State transition from {} to {}", oldState, newState);
LOG_CTRL_ERROR("Failed to initialize pump");
LOG_PERIPH_DEBUG("Received flow update: {} liters", volume);
LOG_CLOUD_WARN("Authorization timeout for user {}", userId);

// Use main application logger
LOG_INFO("Application started successfully");
LOG_ERROR("Critical system error: {}", error_message);
```

### Custom Logger Usage
```cpp
#include "logger.h"

// Get specific loggers
auto stateLogger = Logger::getLogger("StateMachine");
auto ctrlLogger = Logger::getLogger("Controller");

stateLogger->info("Processing event: {}", eventName);
ctrlLogger->warn("Pump disconnected");
```

### Configuration Changes
```cpp
// Change global log level at runtime
Logger::setLevel(spdlog::level::debug); // Enable debug logging

// Check if logging system is ready
if (Logger::isInitialized()) {
    LOG_INFO("Logging system is active");
}
```

## Log Output

### Console Output
```
[2024-01-15 14:30:15.123] [StateMachine] [info] Entering state: 1
[2024-01-15 14:30:15.125] [Controller] [info] Key pressed: A
[2024-01-15 14:30:15.130] [Peripherals] [info] Starting pump...
```

### File Output (logs/fuelflux.log)
```
[2024-01-15 14:30:15.123] [12345] [StateMachine] [info] [src/state_machine.cpp:123] Entering state: 1
[2024-01-15 14:30:15.125] [12346] [Controller] [info] [src/controller.cpp:456] Key pressed: A
[2024-01-15 14:30:15.130] [12347] [Peripherals] [info] [src/peripherals/pump.cpp:89] Starting pump...
```

## Benefits

1. **Performance**: Async logging with minimal blocking
2. **Flexibility**: Multiple output destinations and formats
3. **Maintainability**: Structured logging with categories
4. **Debugging**: Detailed file logs with source locations and thread IDs
5. **Production Ready**: Rotating logs prevent disk space issues
6. **Configurable**: Easy adjustment of log levels without code changes

## Migration from std::cout

The old `std::cout` statements have been replaced with appropriate log levels:

- Informational messages: `LOG_*_INFO()`
- Debug traces: `LOG_*_DEBUG()`
- Warnings: `LOG_*_WARN()`
- Errors: `LOG_*_ERROR()`
- Critical failures: `LOG_*_CRITICAL()` (main logger only)

This provides better control over output verbosity and enables proper log management for production deployments.