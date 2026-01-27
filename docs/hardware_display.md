# Hardware Display Integration

This document describes the integration of the NHD-C12864A1Z-FSW-FBW-HTT LCD display with the FuelFlux controller.

## Overview

The FuelFlux controller supports two display modes:
1. **Console Emulation** (default on x86/Windows): Displays output in the terminal
2. **Real Hardware Display** (default on ARM): Uses the actual NHD-C12864A1Z-FSW-FBW-HTT LCD

## Display Specifications

- **Model**: NHD-C12864A1Z-FSW-FBW-HTT
- **Resolution**: 128x64 pixels
- **Controller**: ST7565
- **Interface**: SPI
- **Control Pins**: D/C (Data/Command), RST (Reset)

## Build Configuration

### Hardware Peripheral Flags

The FuelFlux controller supports separate compilation flags for each hardware peripheral:

- `TARGET_REAL_DISPLAY` - Use real NHD display hardware
- `TARGET_REAL_KEYBOARD` - Use real keyboard hardware (placeholder, not yet implemented)
- `TARGET_REAL_CARD_READER` - Use real card reader hardware (placeholder, not yet implemented)
- `TARGET_REAL_PUMP` - Use real pump hardware (placeholder, not yet implemented)
- `TARGET_REAL_FLOW_METER` - Use real flow meter hardware (placeholder, not yet implemented)

**Platform Defaults:**
- **ON ARM platforms**: `TARGET_REAL_DISPLAY` is automatically enabled by default
- **On Windows/MSVC**: All flags are automatically disabled by default
- **On other platforms**: All flags are disabled by default (can be enabled manually)

To override the default settings:

```bash
cmake -DTARGET_REAL_DISPLAY=ON ..  # Force real hardware display
cmake -DTARGET_REAL_DISPLAY=OFF .. # Force console display emulation
cmake -DTARGET_REAL_KEYBOARD=ON .. # Enable real keyboard (when implemented)
```

### Dependencies

When `TARGET_REAL_DISPLAY` is enabled, the following system libraries are required:

- **libgpiod** (>= 1.6.3): GPIO control via libgpiod
- **freetype2** (>= 26.1.20): Font rendering

Install on Debian/Ubuntu/Armbian:
```bash
sudo apt-get install libgpiod-dev libfreetype6-dev
```

## Hardware Configuration

The display hardware can be configured via environment variables:

| Environment Variable | Default Value | Description |
|---------------------|---------------|-------------|
| `FUELFLUX_SPI_DEVICE` | `/dev/spidev1.0` | SPI device path |
| `FUELFLUX_GPIO_CHIP` | `/dev/gpiochip0` | GPIO chip path |
| `FUELFLUX_DC_PIN` | `262` | GPIO line offset for D/C pin |
| `FUELFLUX_RST_PIN` | `226` | GPIO line offset for RST pin |
| `FUELFLUX_FONT_PATH` | `/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf` | Path to TTF/OTF font |

### Example Configuration

For Orange Pi Zero 2W (default):
```bash
export FUELFLUX_SPI_DEVICE=/dev/spidev1.0
export FUELFLUX_GPIO_CHIP=/dev/gpiochip0
export FUELFLUX_DC_PIN=262
export FUELFLUX_RST_PIN=226
```

For custom hardware:
```bash
export FUELFLUX_SPI_DEVICE=/dev/spidev0.0
export FUELFLUX_GPIO_CHIP=/dev/gpiochip1
export FUELFLUX_DC_PIN=10
export FUELFLUX_RST_PIN=11
export FUELFLUX_FONT_PATH=/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
```

## Display Layout

The display uses a four-line layout optimized for the 128x64 screen:

- **Line 0**: Small font (12pt) - Status/header
- **Line 1**: Large font (28pt) - Primary information
- **Line 2**: Small font (12pt) - Secondary information
- **Line 3**: Small font (12pt) - Footer/timestamp

## GPIO Pin Mapping

GPIO line offsets are specific to your hardware and differ from physical pin numbers. Use the `gpioinfo` command to identify line offsets:

```bash
sudo gpioinfo
```

Example output:
```
gpiochip0 - 264 lines:
    line   0:      unnamed       unused   input  active-high
    ...
    line 262:      unnamed       unused  output  active-high  # D/C pin
    line 226:      unnamed       unused  output  active-high  # RST pin
```

For detailed GPIO mapping notes, see the [fuelflux.nhd repository](https://github.com/maxirmx/fuelflux.nhd).

## SPI Configuration

Ensure SPI is enabled on your system. For Orange Pi boards:

1. Enable SPI overlay in the system configuration
2. Verify SPI device exists:
   ```bash
   ls -l /dev/spidev*
   ```

## Fonts

The display requires a TrueType or OpenType font. Common font locations:

- Ubuntu: `/usr/share/fonts/truetype/ubuntu/`
- Debian: `/usr/share/fonts/truetype/dejavu/`

Install fonts if needed:
```bash
sudo apt-get install fonts-ubuntu fonts-dejavu
```

## Troubleshooting

### Display Not Initializing

1. Check SPI device permissions:
   ```bash
   sudo chmod 666 /dev/spidev1.0
   # Or add user to spi group
   sudo usermod -a -G spi $USER
   ```

2. Verify GPIO chip permissions:
   ```bash
   sudo chmod 666 /dev/gpiochip0
   # Or add user to gpio group
   sudo usermod -a -G gpio $USER
   ```

3. Check log output for detailed error messages

### Font Not Found

Verify the font exists at the specified path:
```bash
ls -l /usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf
```

Try alternative fonts:
- `/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf`
- `/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf`

### Incorrect Display Output

If the display is mirrored or upside-down:
1. This is controlled in the ST7565 initialization
2. See `src/nhd/st7565.cpp` for SEG/COM direction settings
3. Contact the maintainer for hardware-specific adjustments

## Development

When developing on a non-ARM platform, you can:

1. Build with console emulation (default):
   ```bash
   cmake ..
   cmake --build .
   ```

2. Build with real hardware display enabled (for testing compilation):
   ```bash
   cmake -DTARGET_REAL_DISPLAY=ON ..
   cmake --build .
   ```
   Note: This will compile but won't run without actual hardware.

3. Enable multiple hardware peripherals (when implemented):
   ```bash
   cmake -DTARGET_REAL_DISPLAY=ON -DTARGET_REAL_KEYBOARD=ON ..
   cmake --build .
   ```

## References

- [NHD Display Demo Project](https://github.com/maxirmx/fuelflux.nhd)
- [libgpiod Documentation](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about/)
- [FreeType Documentation](https://www.freetype.org/freetype2/docs/documentation.html)
