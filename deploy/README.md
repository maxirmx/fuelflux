# FuelFlux Deployment Package

This directory contains files for deploying FuelFlux as a systemd service on Linux.

## Contents

- `fuelflux.service` - Systemd unit file
- `install.sh` - Installation/management script
- `fuelflux.env.example` - Example environment configuration

## Quick Start

```bash
# Copy deployment files to target system
scp -r deploy/* user@target:/tmp/fuelflux-deploy/

# On target system
cd /tmp/fuelflux-deploy
chmod +x install.sh

# Install service (without binary)
sudo ./install.sh install

# Or install with binary
sudo ./install.sh install /path/to/fuelflux
```

## Usage

```bash
# Full installation
sudo ./install.sh install [binary_path]

# Update binary and restart
sudo ./install.sh update /path/to/fuelflux

# Service management
sudo ./install.sh start
sudo ./install.sh stop
sudo ./install.sh restart
sudo ./install.sh status

# Enable/disable autostart
sudo ./install.sh enable
sudo ./install.sh disable

# Uninstall
sudo ./install.sh uninstall
```

## Directory Structure

After installation:

```
/opt/fuelflux/
└── bin/
    └── fuelflux          # Application binary

/etc/fuelflux/
└── fuelflux.env          # Environment configuration

/var/fuelflux/
├── db/
│   ├── fuelflux_storage.db
│   └── fuelflux_cache.db
└── logs/
    └── fuelflux.log
```

## Configuration

Edit `/etc/fuelflux/fuelflux.env` to configure:

- `FUELFLUX_CONTROLLER_ID` - Unique device identifier
- `FUELFLUX_PUMP_GPIO_CHIP` - GPIO chip device
- `FUELFLUX_PUMP_RELAY_PIN` - Relay pin number
- `FUELFLUX_PUMP_ACTIVE_LOW` - Relay active low setting

## Logs

View logs with:

```bash
# Follow live logs
journalctl -u fuelflux -f

# Last 100 lines
journalctl -u fuelflux -n 100

# Logs since boot
journalctl -u fuelflux -b

# Application file logs (if enabled)
tail -f /var/fuelflux/logs/fuelflux.log
```

## Hardware Access

The service runs as user `fuelflux` with access to hardware groups:
- `gpio` - GPIO access
- `dialout` - Serial port access
- `i2c` - I2C bus access
- `spi` - SPI bus access

If a group doesn't exist on your system, the script will skip it.

## Building for Deployment

Build with Unix folder convention for proper paths:

```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DFUELFLUX_UNIX_FOLDER_CONVENTION=ON \
    -DTARGET_REAL_DISPLAY=ON \
    -DTARGET_REAL_CARD_READER=ON \
    -DTARGET_REAL_PUMP=ON \
    -DTARGET_REAL_KEYBOARD=ON

cmake --build build
```
