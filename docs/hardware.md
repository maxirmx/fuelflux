# Hardware Integration

This guide consolidates wiring and configuration for FuelFlux hardware peripherals.
It covers the real display and NFC card reader implementations, plus the build flags
that toggle hardware support.

## Hardware Configuration

All hardware configuration values are centralized in `include/hardware/hardware_config.h`.
This simplifies deployment by avoiding environment variable complexity. To change hardware
settings, modify the configuration file and rebuild the application.

## Build configuration

### Hardware peripheral selection

- `TARGET_REAL_DISPLAY` - Use real NHD display hardware
- `KEYBOARD_TYPE` - Select `CONSOLE`, `LEGACY`, or `VID`
- `KEYBOARD_MCP_PORT` - Select `AUTO`, `A`, or `B` for a physical keyboard
- `KEYBOARD_LONG_PRESS_MS` - VID long-press threshold; default `1000`
- `TARGET_REAL_CARD_READER` - Use real NFC card reader hardware (PN532 via libnfc)
- `TARGET_REAL_PUMP` - Use real pump hardware (placeholder, not yet implemented)
- `TARGET_REAL_FLOW_METER` - Use real flow meter hardware (placeholder, not yet implemented)

**Platform defaults:**
- **Windows/MSVC**: `KEYBOARD_TYPE=CONSOLE`
- **All non-MSVC platforms**: `KEYBOARD_TYPE=VID`
- **Production builds**: VID; legacy must be selected explicitly

Override defaults at configure time:

```bash
cmake -DTARGET_REAL_DISPLAY=ON ..
cmake -DTARGET_REAL_CARD_READER=ON ..
cmake -DKEYBOARD_TYPE=VID -DKEYBOARD_MCP_PORT=AUTO ..
```

`TARGET_REAL_KEYBOARD` has been removed. Supplying it, including through a
stale CMake cache, is a configuration error. Delete the build directory or
run CMake with `-U TARGET_REAL_KEYBOARD` before selecting `KEYBOARD_TYPE`.

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

Display configuration is defined in `include/display/display_config.h`:

| Setting | ST7565 Value | ILI9488 Value | Description |
|---------|--------------|---------------|-------------|
| SPI_DEVICE | `/dev/spidev1.0` | `/dev/spidev1.0` | SPI device path |
| GPIO_CHIP | `/dev/gpiochip0` | `/dev/gpiochip0` | GPIO chip path |
| DC_PIN | `271` | `271` | GPIO line offset for D/C pin |
| RST_PIN | `256` | `256` | GPIO line offset for RST pin |
| FONT_PATH | `/usr/share/fonts/...` | `/usr/share/fonts/...` | Path to TTF font |

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

Card reader configuration is defined in `include/hardware/hardware_config.h`:

| Setting | Default Value | Description |
|---------|---------------|-------------|
| I2C_DEVICE | `/dev/i2c-3` | I2C device used to build `pn532_i2c:<device>` |

The connection string format is `pn532_i2c:<device>` and is auto-generated from the I2C device path.

### Wiring notes

- Ensure I2C is enabled for the PN532 HAT/module.
- Use 3.3V power, SDA/SCL, and GND connections appropriate to your board.
- Confirm the I2C bus path matches your OS (`/dev/i2c-*`).

## Keyboard (MCP23017 matrix keyboard)

FuelFlux supports three keyboard types:

| `KEYBOARD_TYPE` | Description | Default MCP port |
|---|---|---|
| `CONSOLE` | Console keyboard emulator | Not applicable |
| `VID` | VID 14-key membrane keyboard | B, mirrored |
| `LEGACY` | Off-the-shelf 4x4 matrix keypad | A, direct |

The VID keyboard is the production and non-MSVC default. Select legacy
explicitly with `-DKEYBOARD_TYPE=LEGACY`. The selected MCP23017 port must be
dedicated to the keyboard.

### Configuration

Keyboard configuration is defined in `include/hardware/hardware_config.h`:

| Setting | Default Value | Description |
|---------|---------------|-------------|
| I2C_DEVICE | `/dev/i2c-3` | I2C device path |
| I2C_ADDRESS | `0x20` | MCP23017 I2C address |
| POLL_MS | `5` | Polling interval in milliseconds |
| DEBOUNCE_MS | `20` | Debounce delay in milliseconds |
| RELEASE_MS | `30` | Key release delay in milliseconds |
| SCAN_DELAY_US | `300` | Row settling delay in microseconds |

Build-time settings:

| Setting | Default | Description |
|---|---|---|
| `KEYBOARD_TYPE` | `VID` non-MSVC, `CONSOLE` MSVC | Keyboard implementation |
| `KEYBOARD_MCP_PORT` | `AUTO` | `AUTO`, `A`, or `B`; AUTO selects B for VID and A for legacy |
| `KEYBOARD_LONG_PRESS_MS` | `1000` | VID long-press threshold; positive integer up to `2147483647` ms |

Examples:

```bash
# Production VID keyboard on the default mirrored Port B
cmake -S . -B build -DKEYBOARD_TYPE=VID

# VID keyboard wired to Port A
cmake -S . -B build -DKEYBOARD_TYPE=VID -DKEYBOARD_MCP_PORT=A

# Legacy keypad; it is never selected as the production default
cmake -S . -B build-legacy -DKEYBOARD_TYPE=LEGACY
```

### MCP23017 port mapping

Layouts use logical pins P0 through P7. Port A maps them directly. Port B
reverses them for the mirrored connector:

| Logical pin | Port A | Port B |
|---|---|---|
| P0 | PA0 | PB7 |
| P1 | PA1 | PB6 |
| P2 | PA2 | PB5 |
| P3 | PA3 | PB4 |
| P4 | PA4 | PB3 |
| P5 | PA5 | PB2 |
| P6 | PA6 | PB1 |
| P7 | PA7 | PB0 |

### VID 14-key wiring

The eight Crimpflex contacts are numbered left-to-right when the keyboard is
viewed from the front:

| Contact | Matrix net | Logical pin | Direction |
|---:|---|---|---|
| 1 | C1 | P0 | Input with pull-up |
| 2 | R4 | P1 | Output |
| 3 | C2 | P2 | Input with pull-up |
| 4 | R3 | P3 | Output |
| 5 | C3 | P4 | Input with pull-up |
| 6 | R2 | P5 | Output |
| 7 | C4 | P6 | Input with pull-up |
| 8 | R1 | P7 | Output |

| | C1 | C2 | C3 | C4 |
|---|---|---|---|---|
| R1 | `1` | `2` | `3` | `START` |
| R2 | `4` | `5` | `6` | `STOP` |
| R3 | `7` | `8` | `9` | Unused |
| R4 | `RUS/ENG` | `0` | `BACKSPACE` | Unused |

### VID key behavior

| Physical key | Short press | Long press |
|---|---|---|
| `0`-`9` | Digit | Same digit |
| `START` | Start/enter (`A`) | Start/enter (`A`) |
| `STOP` | Stop/cancel (`B`) | Stop/cancel (`B`) |
| `BACKSPACE` | Remove the last digit (`#`) | Remove the last digit (`#`) |
| `RUS/ENG` | Ignored | Reinitialize the display (`D`) |

In customer volume entry, pressing `START` with an empty or zero volume is
interpreted by the state machine as maximum followed by start. This behavior
is shared by console, legacy, and VID keyboards and does not depend on press
duration.

Short VID presses are reported after confirmed release. Long presses are
reported once as soon as the threshold is reached and produce no event on
release. The exact threshold duration counts as long.

### Legacy 4x4 wiring

- P0-P3: row outputs R1-R4
- P4-P7: column inputs C1-C4 with internal pull-ups

| | C1/P4 | C2/P5 | C3/P6 | C4/P7 |
|---|---|---|---|---|
| R1/P0 | `1` | `2` | `3` | `A` |
| R2/P1 | `4` | `5` | `6` | `B` |
| R3/P2 | `7` | `8` | `9` | `C` (ignored) |
| R4/P3 | `*` | `0` | `#` | `D` |

Legacy keys are emitted after press debounce, without waiting for release.
Both physical scanners report one key at a time; multi-key rollover and
ghosting prevention are not supported.

## Flow Meter (GPIO pulse counting)

The hardware flow meter uses GPIO pulse counting to measure fuel flow.

### Configuration

Flow meter configuration is defined in `include/hardware/hardware_config.h`:

| Setting | Default Value | Description |
|---------|---------------|-------------|
| GPIO_CHIP | `/dev/gpiochip0` | GPIO chip path |
| GPIO_PIN | `267` | GPIO line offset for pulse input |
| TICKS_PER_LITER | `72.0` | Pulse count per liter of fuel |

## Pump (GPIO relay control)

The hardware pump uses GPIO relay control to operate the fuel pump.

### Configuration

Pump configuration is defined in `include/hardware/hardware_config.h`:

| Setting | Default Value | Description |
|---------|---------------|-------------|
| GPIO_CHIP | `/dev/gpiochip0` | GPIO chip path |
| RELAY_PIN | `272` | GPIO line offset for relay control |
| ACTIVE_LOW | `true` | Relay is active-low |

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
