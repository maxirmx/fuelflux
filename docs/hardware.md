<!--
Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
All rights reserved.
This file is a part of fuelflux application
-->

# Hardware Integration

This guide consolidates wiring and configuration for FuelFlux hardware peripherals.
It covers the real display and NFC card reader implementations, plus the build flags
that toggle hardware support.

## Build configuration

### Hardware peripheral flags

- `TARGET_REAL_DISPLAY` - Use real NHD display hardware
- `TARGET_REAL_KEYBOARD` - Use real keyboard hardware (placeholder, not yet implemented)
- `TARGET_REAL_CARD_READER` - Use real NFC card reader hardware (PN532 via libnfc)
- `TARGET_REAL_PUMP` - Use real pump hardware (placeholder, not yet implemented)
- `TARGET_REAL_FLOW_METER` - Use real flow meter hardware (placeholder, not yet implemented)

**Platform defaults:**
- **ARM platforms**: `TARGET_REAL_DISPLAY` is enabled by default
- **Windows/MSVC**: all flags are disabled by default
- **Other platforms**: all flags are disabled by default

Override defaults at configure time:

```bash
cmake -DTARGET_REAL_DISPLAY=ON ..
cmake -DTARGET_REAL_CARD_READER=ON ..
```

## Display (NHD-C12864A1Z-FSW-FBW-HTT)

### Specifications

- **Model**: NHD-C12864A1Z-FSW-FBW-HTT
- **Resolution**: 128x64 pixels
- **Controller**: ST7565
- **Interface**: SPI
- **Control Pins**: D/C (Data/Command), RST (Reset)

### Dependencies

When `TARGET_REAL_DISPLAY` is enabled:

- **libgpiod** (>= 1.6.3): GPIO control
- **freetype2** (>= 26.1.20): font rendering

Install on Debian/Ubuntu/Armbian:

```bash
sudo apt-get install libgpiod-dev libfreetype6-dev
```

### Configuration

Set these environment variables to override defaults:

| Environment Variable | Default Value | Description |
|---------------------|---------------|-------------|
| `FUELFLUX_SPI_DEVICE` | `/dev/spidev1.0` | SPI device path |
| `FUELFLUX_GPIO_CHIP` | `/dev/gpiochip0` | GPIO chip path |
| `FUELFLUX_DC_PIN` | `262` | GPIO line offset for D/C pin |
| `FUELFLUX_RST_PIN` | `226` | GPIO line offset for RST pin |
| `FUELFLUX_FONT_PATH` | `/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf` | Path to TTF/OTF font |

Example configuration:

```bash
export FUELFLUX_SPI_DEVICE=/dev/spidev1.0
export FUELFLUX_GPIO_CHIP=/dev/gpiochip0
export FUELFLUX_DC_PIN=262
export FUELFLUX_RST_PIN=226
```

### Wiring (Orange Pi Zero 2W, SPI1)

The wiring below matches the verified GPIO lines on Orange Pi Zero 2W and avoids
extra pins.

**Power:**

| LCD Pin | Function | Orange Pi physical pin | Notes |
|--------:|---------|------------------------|------|
| 3 (VDD) | +3.3V | Pin 1 or Pin 17 | Use 3.3V only |
| 7 (VSS) | GND | 6 / 9 / 14 / 20 / 25 / 30 / 34 / 39 | Common ground |

**SPI1 bus (ALT4):**

| LCD Pin | LCD Signal | Orange Pi pin | Header name | Linux GPIO |
|--------:|------------|---------------|-------------|-----------|
| 1 | SCL | Pin 23 | `SCLK.1` | 230 |
| 2 | SI (MOSI) | Pin 19 | `MOSI.1` | 231 |
| 6 | /CS | Pin 24 | `CE.0` | 229 |

**Control signals:**

| LCD Pin | LCD Signal | Orange Pi pin | Header name | Linux GPIO |
|--------:|------------|---------------|-------------|-----------|
| 4 | A0 / D-C | Pin 22 | `RXD.2` | 262 |
| 5 | /RESET | Pin 11 | `TXD.5` | 226 |

**Backlight:**

- Always on: LED+ → 3.3V, LED− → GND
- Dimmable: use an N-MOSFET low-side switch with a spare GPIO

**Heater (HTT):**

- LCD 8 (H+) → external +12V
- LCD 9 (H−) → MOSFET drain, MOSFET source → GND

### SPI/GPIO notes

- Do not assume Raspberry Pi BCM numbering on Orange Pi.
- Use `gpioinfo` to confirm GPIO line offsets.
- Ensure SPI is enabled and `/dev/spidev*` exists.

## NFC card reader (PN532 via libnfc)

The hardware card reader uses libnfc to communicate with a PN532 NFC module over I2C.

### Dependencies

Install libnfc:

```bash
sudo apt-get install libnfc-dev
```

### Configuration

Set these environment variables to override defaults:

| Environment Variable | Default Value | Description |
|---------------------|---------------|-------------|
| `FUELFLUX_NFC_CONNSTRING` | *(auto)* | Full libnfc connection string (overrides I2C device) |
| `FUELFLUX_NFC_I2C_DEVICE` | `/dev/i2c-3` | I2C device used to build `pn532_i2c:<device>` |

Example configuration:

```bash
export FUELFLUX_NFC_I2C_DEVICE=/dev/i2c-3
# or
export FUELFLUX_NFC_CONNSTRING=pn532_i2c:/dev/i2c-3
```

### Wiring notes

- Ensure I2C is enabled for the PN532 HAT/module.
- Use 3.3V power, SDA/SCL, and GND connections appropriate to your board.
- Confirm the I2C bus path matches your OS (`/dev/i2c-*`).

## Troubleshooting

### Display

- Check SPI device permissions (`/dev/spidev*`).
- Verify GPIO permissions for the configured chip.
- Confirm the font path exists.

### NFC

- Verify I2C is enabled and the PN532 is in I2C mode.
- Check the libnfc connection string and I2C bus path.
- Inspect logs for libnfc initialization errors.

## References

- [NHD Display Demo Project](https://github.com/maxirmx/fuelflux.nhd)
- [NFC Demo Project](https://github.com/maxirmx/fuelflux.nfc)
- [libgpiod Documentation](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about/)
- [FreeType Documentation](https://www.freetype.org/freetype2/docs/documentation.html)
- [libnfc Documentation](https://github.com/nfc-tools/libnfc)
