# Display Configuration

FuelFlux supports multiple display types through build-time configuration:

## Supported Display Types

### 1. CONSOLE (Default)
Console-based display emulation for development and testing without hardware.
- No hardware required
- Full feature testing in terminal
- Default configuration for development builds

### 2. ST7565
128x64 monochrome LCD display (NHD-C12864A1Z-FSW-FBW-HTT)
- Resolution: 128×64 pixels
- Font sizes: Small (12pt), Large (28pt)
- 4-line text display layout
- SPI interface with GPIO control

### 3. ILI9488
480x320 TFT color display (ILI9488-based modules)
- Resolution: 480×320 pixels
- Font sizes: Small (40pt), Large (80pt)
- 4-line text display layout
- SPI interface with GPIO control
- RGB666 color mode

## Build Configuration

### Configure Display Type

Use the `DISPLAY_TYPE` CMake option to select the display:

```bash
# Console emulation (default)
cmake -B build -DDISPLAY_TYPE=CONSOLE

# ST7565 128x64 LCD
cmake -B build -DDISPLAY_TYPE=ST7565

# ILI9488 480x320 TFT
cmake -B build -DDISPLAY_TYPE=ILI9488
```

### Build Examples

```bash
# Build with console emulation
mkdir -p build && cd build
cmake .. -DDISPLAY_TYPE=CONSOLE
make -j$(nproc)

# Build with ST7565 display
mkdir -p build && cd build
cmake .. -DDISPLAY_TYPE=ST7565
make -j$(nproc)

# Build with ILI9488 display
mkdir -p build && cd build
cmake .. -DDISPLAY_TYPE=ILI9488
make -j$(nproc)
```

## Hardware Configuration

### Default Pin Configuration (Orange Pi Zero 2W)

Both ST7565 and ILI9488 use the same default GPIO pins:
- **SPI Device**: `/dev/spidev1.0`
- **GPIO Chip**: `/dev/gpiochip0`
- **DC Pin**: 262 (Data/Command)
- **RST Pin**: 226 (Reset)

### ST7565 Specific Settings
- **SPI Speed**: 8 MHz
- **Display Resolution**: 128×64
- **Small Font**: 12pt
- **Large Font**: 28pt

### ILI9488 Specific Settings
- **SPI Speed**: 32 MHz
- **Display Resolution**: 480×320
- **Small Font**: 40pt
- **Large Font**: 80pt
- **Pixel Format**: RGB666 (18-bit color)

## Display Driver Architecture

### Base Interface (`ILcdDriver`)
Both display drivers implement a common interface to avoid code duplication:

```cpp
class ILcdDriver {
    virtual void reset() = 0;
    virtual void init() = 0;
    virtual void set_framebuffer(const std::vector<uint8_t>& fb) = 0;
    virtual void clear() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};
```

### Display Abstraction Layer
The `RealDisplay` class uses polymorphism to work with either display type:
- Automatically selects appropriate driver based on build configuration
- Configures font sizes and display parameters
- Manages SPI and GPIO initialization

### Shared Components
- **FourLineDisplay**: Text rendering library (supports both resolutions)
- **SpiLinux**: SPI communication layer
- **GpioLine**: GPIO control layer

## Dependencies

### Required for Hardware Displays (ST7565 or ILI9488)
```bash
# Ubuntu/Debian
sudo apt-get install libgpiod-dev libfreetype6-dev

# Required at runtime
sudo apt-get install libgpiod2 libfreetype6
```

### Optional Font Configuration
Default font: `/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf`

Install Ubuntu fonts if not present:
```bash
sudo apt-get install fonts-ubuntu
```

## Reference Implementation

The ILI9488 driver is based on the reference implementation at:
https://github.com/maxirmx/fuelflux.lcd

This reference includes:
- Complete driver implementation for both ST7565 and ILI9488
- Demo applications
- Test cases
- Hardware configuration examples

## Troubleshooting

### Build Errors

**Error: "No display type defined"**
- Ensure DISPLAY_TYPE is set to ST7565, ILI9488, or CONSOLE

**Error: "freetype2 not found"**
```bash
sudo apt-get install libfreetype6-dev
```

**Error: "libgpiod not found"**
```bash
sudo apt-get install libgpiod-dev
```

### Runtime Issues

**Display not initializing**
1. Check SPI device exists: `ls -l /dev/spidev1.0`
2. Check GPIO chip: `ls -l /dev/gpiochip0`
3. Verify GPIO pin numbers with `gpioinfo`
4. Check SPI overlay is enabled in system configuration

**Wrong font size/display layout**
- Verify DISPLAY_TYPE matches your actual hardware
- Rebuild after changing DISPLAY_TYPE

**SPI communication errors**
- Check SPI speed (ST7565: 8MHz, ILI9488: 32MHz)
- Verify wiring and connections
- Check for proper grounding

## Adding New Display Types

To add support for a new display:

1. Create driver class implementing `ILcdDriver` interface
2. Add driver header to `include/nhd/`
3. Add driver implementation to `src/nhd/`
4. Update `CMakeLists.txt` to include new display type
5. Add configuration section in `include/peripherals/display.h`
6. Update initialization in `src/peripherals/display.cpp`
7. Test with all display configurations

## Performance Notes

### ST7565 (128×64)
- Lower resolution, faster refresh
- Suitable for simple text display
- Low power consumption

### ILI9488 (480×320)
- Higher resolution, richer display
- Requires more processing power
- Supports future color/graphics features
- Larger text is more readable

## Future Enhancements

Potential improvements:
- Add RGB color support for ILI9488
- Implement graphics/icon rendering
- Add brightness/contrast controls
- Support additional display types
- Runtime display type detection
