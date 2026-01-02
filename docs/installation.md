# FuelFlux Installation

## Installation Process

To install the FuelFlux application, use the following CMake commands:

```bash
# Build the project
cmake -S . -B build
cmake --build build --config Release

# Install to default location (build/install)
cmake --install build --config Release

# Or install to a custom location
cmake --install build --config Release --prefix /path/to/install/directory
```

## Installation Structure

After installation, the directory structure will be:

```
install_directory/
├── bin/
│   └── fuelflux.exe          # Main executable
├── config/
│   └── logging.json          # Logging configuration
└── logs/                     # Created at runtime
    ├── fuelflux.log          # Main log file
    └── fuelflux_error.log    # Error log file
```

## Configuration Files

### logging.json
The logging configuration file is automatically installed to `config/logging.json`. The application will look for this file relative to its working directory. The configuration includes:

- **Console output**: Colored console logging for info level and above
- **File logging**: Rotating log files (10MB max, 5 files) for debug level and above
- **Error logging**: Separate error log files (5MB max, 3 files) for warnings and above
- **Async logging**: High-performance asynchronous logging

### Runtime Behavior
- Log files are created in a `logs/` directory relative to the working directory
- If the configuration file is not found, the application will use default settings
- The logging system supports runtime level adjustments

## Running the Application

1. Navigate to the installation directory
2. Run the executable: `bin/fuelflux.exe`
3. The application will automatically create log files in the `logs/` directory

## Default Install Location

If no custom prefix is specified, the application installs to:
- **Default**: `<build_directory>/install/`
- **Custom**: Use `--prefix` option to specify alternative location

This avoids requiring administrative privileges for installation.