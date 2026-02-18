# Display Configuration

FuelFlux supports multiple display types through build-time configuration:

## Supported Display Types

### 1. CONSOLE
Console-based display emulation for development and testing without hardware.
- No hardware required
- Full feature testing in terminal
- Default on Windows/MSVC builds

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
- Default on non-Windows/target hardware builds

## Build Configuration

### Configure Display Type

Use the `DISPLAY_TYPE` CMake option to select the display:

```bash
# Console emulation (default on Windows/MSVC)
cmake -B build -DDISPLAY_TYPE=CONSOLE

# ST7565 128x64 LCD
cmake -B build -DDISPLAY_TYPE=ST7565

# ILI9488 480x320 TFT (default on Linux/target hardware)
cmake -B build -DDISPLAY_TYPE=ILI9488
```

**Platform-specific defaults:**
- **Windows/MSVC builds**: CONSOLE (for development without hardware)
- **Linux/ARM builds**: ILI9488 (for target hardware deployment)

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
- **DC Pin**: 271 (Data/Command)
- **RST Pin**: 256 (Reset)

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

The display system has been refactored into a clean, thread-safe class hierarchy:

### Class Hierarchy

```
FourLineDisplay (abstract base class)
├── ConsoleDisplay (stub for testing/development)
└── HardwareDisplay (abstract hardware base)
    ├── St7565Display (ST7565 128x64 LCD)
    └── Ili9488Display (ILI9488 480x320 TFT)
```

### Thread Safety

All public methods of `FourLineDisplay` are thread-safe and protected by mutex:
- `setLine()` - Set text for a specific line
- `getLine()` - Get current text for a line
- `clearAll()` - Clear all lines
- `clearLine()` - Clear a specific line
- `update()` - Render and update the physical display
- `setBacklight()` - Control backlight/power

This ensures safe concurrent access from multiple threads (e.g., controller event loop, timeout checks, etc.).

### Display Configuration

Display hardware configuration is centralized in `include/display/display_config.h`:

**Common Settings:**
- SPI Device: `/dev/spidev1.0`
- GPIO Chip: `/dev/gpiochip0`
- Font Path: `/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf`

**ST7565 Settings:**
- DC Pin: 271, RST Pin: 256
- Resolution: 128×64
- SPI Speed: 8 MHz
- Font Sizes: 12pt (small), 28pt (large)
- Margins: 2px left/right

**ILI9488 Settings:**
- DC Pin: 271, RST Pin: 256
- Resolution: 480×320
- SPI Speed: 8 MHz
- Font Sizes: 40pt (small), 80pt (large)
- Margins: 5px left/right

Configuration is now hardcoded (no environment variables) for simplified deployment.

### Low-Level Components

**LCD Driver Interface (`ILcdDriver`):**
Both ST7565 and ILI9488 implement this common interface:

```cpp
class ILcdDriver {
    virtual void reset() = 0;
    virtual void init() = 0;
    virtual void set_framebuffer(const std::vector<uint8_t>& fb) = 0;
    virtual void clear() = 0;
    virtual void set_backlight(bool enabled) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};
```

**Shared Components:**
- **FourLineDisplayImpl**: Internal text rendering (FreeType-based)
- **SpiLinux**: SPI communication layer
- **GpioLine**: GPIO control layer
- **MonoGfx**: Monochrome graphics primitives
- **FtText**: Font rendering using FreeType

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

To add support for a new display controller:

1. **Create LCD Driver Class**
   - Implement the `ILcdDriver` interface
   - Add driver header to `include/display/`
   - Add driver implementation to `src/display/`

2. **Create Display Class**
   - Inherit from `HardwareDisplay`
   - Implement `createLcdDriver()` method
   - Add configuration to `include/display/display_config.h`

3. **Update Build System**
   - Add new DISPLAY_TYPE option in `CMakeLists.txt`
   - Add conditional compilation for your driver
   - Include new source files in the build

4. **Update Integration**
   - Update `src/peripherals/display.cpp` to instantiate your new display class
   - Add appropriate `#ifdef` guards

5. **Test**
   - Build with all display configurations
   - Run test suite to ensure no regressions
   - Test on actual hardware if available

Example for a hypothetical SSD1306 display:

```cpp
// include/display/ssd1306_display.h
#pragma once
#include "display/hardware_display.h"

namespace fuelflux::display {
class Ssd1306Display : public HardwareDisplay {
public:
    Ssd1306Display();
protected:
    std::unique_ptr<ILcdDriver> createLcdDriver() override;
};
}

// src/display/ssd1306_display.cpp
#include "display/ssd1306_display.h"
#include "display/ssd1306.h"

namespace fuelflux::display {
Ssd1306Display::Ssd1306Display()
    : HardwareDisplay(
        ssd1306::WIDTH, ssd1306::HEIGHT,
        ssd1306::SMALL_FONT_SIZE, ssd1306::LARGE_FONT_SIZE,
        ssd1306::LEFT_MARGIN, ssd1306::RIGHT_MARGIN,
        hardware::SPI_DEVICE, ssd1306::SPI_SPEED,
        hardware::GPIO_CHIP, ssd1306::DC_PIN,
        ssd1306::RST_PIN, hardware::FONT_PATH) {}

std::unique_ptr<ILcdDriver> Ssd1306Display::createLcdDriver() {
    return std::make_unique<Ssd1306>(*spi_, *dcLine_, *rstLine_,
                                     ssd1306::WIDTH, ssd1306::HEIGHT);
}
}
```

The refactored architecture makes it straightforward to add new displays with minimal code duplication.

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
