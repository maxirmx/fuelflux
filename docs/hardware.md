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
- `TARGET_REAL_TEMPERATURE_SENSOR` - Use the AHT10 display temperature sensor and heater relay
- `TARGET_REAL_GPS` - Use the NMEA GPS receiver connected over UART

**Platform defaults:**
- **Windows/MSVC**: `KEYBOARD_TYPE=CONSOLE`
- **Windows/MSVC**: `TARGET_REAL_TEMPERATURE_SENSOR=OFF`
- **Windows/MSVC**: `TARGET_REAL_GPS=OFF`
- **All non-MSVC platforms**: `KEYBOARD_TYPE=VID`
- **All non-MSVC platforms**: `TARGET_REAL_TEMPERATURE_SENSOR=ON`
- **All non-MSVC platforms**: `TARGET_REAL_GPS=ON`
- **Production builds**: VID; legacy must be selected explicitly

Override defaults at configure time:

```bash
cmake -DTARGET_REAL_DISPLAY=ON ..
cmake -DTARGET_REAL_CARD_READER=ON ..
cmake -DTARGET_REAL_TEMPERATURE_SENSOR=ON ..
cmake -DTARGET_REAL_GPS=ON ..
cmake -DKEYBOARD_TYPE=VID -DKEYBOARD_MCP_PORT=AUTO ..
```

`TARGET_REAL_KEYBOARD` has been removed. Supplying it, including through a
stale CMake cache, is a configuration error. Delete the build directory or
run CMake with `-U TARGET_REAL_KEYBOARD` before selecting `KEYBOARD_TYPE`.

## GPS receiver (NMEA over UART)

The optional GPS monitor reads NMEA traffic in a background thread and keeps
the last checksum-valid active RMC fix or nonzero-quality GGA fix in memory.
Missing, disconnected, or silent GPS hardware does not prevent the controller
from operating. The monitor logs the outage once and retries after one hour.

Configuration is defined in `include/hardware/hardware_config.h`:

| Setting | Default Value | Description |
|---|---|---|
| `SERIAL_DEVICE` | `/dev/ttyS2` | GPS UART device |
| `BAUD_RATE` | `115200` | NMEA stream baud rate |

The port is configured as raw 8N1 input with hardware and software flow control
disabled. Checksum-valid NMEA traffic without a position fix still confirms
that the receiver is connected. A stream is considered silent after ten
seconds without checksum-valid traffic.

The first reliable position is logged immediately. The latest reliable
position is then logged at most once per hour while the in-memory value
continues to update on every accepted fix. Ensure the service user belongs to
the `dialout` group and that the device tree enables `/dev/ttyS2`.

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
| FONT_PATH | Built-in bitmap | `/usr/share/fonts/...` | ST uses compiled 1-bit glyphs; ILI9488 uses a TTF path |

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

### ST7565 text rendering

The 128×64 display uses the compiled-in `FuelFlux ST Bitmap` font rather than
scaling an outline font at runtime. Normal lines use 6×12 cells and the
emphasized second line uses 14×28 bold cells. With the configured 2-pixel side
margins, their exact capacities are 20 and 8 characters respectively. The
glyph data is derived from Terminus Font 4.49.1 under the SIL Open Font License
1.1; attribution is installed under `share/doc/fuelflux/fuelflux-st-bitmap`.

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
| R1 | `1` | `2` | `3` | `СТАРТ` |
| R2 | `4` | `5` | `6` | `СТОП` |
| R3 | `7` | `8` | `9` | Unused |
| R4 | `RUS/ENG` | `0` | `BACKSPACE` | Unused |

### VID key behavior

| Physical key | Short press | Long press |
|---|---|---|
| `0`-`9` | Digit | Same digit |
| `СТАРТ` | Start/enter (`A`) | Start/enter (`A`) |
| `СТОП` | Stop/cancel (`B`) | Enter calibration when the hold begins and ends in idle; otherwise stop/cancel |
| `BACKSPACE` | Remove the last digit (`#`) | Remove the last digit (`#`) |
| `RUS/ENG` | Ignored | Reinitialize the display (`D`) |

In customer volume entry, pressing `СТАРТ` with an empty or zero volume is
interpreted by the state machine as maximum followed by start. This behavior
is shared by console, legacy, and VID keyboards and does not depend on press
duration.

Short VID presses are reported after confirmed release. Long presses are
reported once as soon as the threshold is reached and produce no event on
release. The exact threshold duration counts as long.

The console emulator uses `B` for a short СТОП press and `L` to simulate a
long СТОП press. The legacy keypad keeps its immediate `B` cancel behavior;
the controller remembers where the press began so a hold outside idle cannot
cancel and then enter calibration.

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

The physical flow-meter input ignores pulses during an initial startup blanking
window. The CMake cache setting `FLOW_METER_STARTUP_BLANKING_MS` controls the
window and defaults to `200` milliseconds. Set it to `0` to disable blanking.
Simulation modes do not apply this window.

## Pump (GPIO relay control)

The hardware pump uses GPIO relay control to operate the fuel pump.

### Configuration

Pump configuration is defined in `include/hardware/hardware_config.h`:

| Setting | Default Value | Description |
|---------|---------------|-------------|
| GPIO_CHIP | `/dev/gpiochip0` | GPIO chip path |
| RELAY_PIN | `272` | GPIO line offset for relay control |
| ACTIVE_LOW | `true` | Relay is active-low |

## Display temperature sensor and heater relay

The optional display temperature monitor uses an AHT10 on I2C bus 2 and
relay channel 2 to control the display heater. Its worker attempts a reading
as soon as it starts and then once per minute. Temperature monitoring failures
are logged but do not prevent the controller from operating.

The last successful temperature remains available through the Controller as
`std::optional<double>`. It is empty until the first successful measurement
and is not cleared by later communication failures.

### Configuration

Temperature and heater configuration is defined in
`include/hardware/hardware_config.h`:

| Setting | Default Value | Description |
|---------|---------------|-------------|
| I2C_DEVICE | `/dev/i2c-2` | AHT10 I2C device path |
| I2C_ADDRESS | `0x38` | AHT10 I2C address |
| GPIO_CHIP | `/dev/gpiochip0` | Heater relay GPIO chip |
| RELAY_PIN | `260` | PI4, physical pin 38, relay channel 2 |
| ACTIVE_LOW | `true` | Heater relay is active-low |
| RELAY_THRESHOLD_CELSIUS | `-20.0` | Heater activation threshold |
| RELAY_CONSUMER | `display-heater` | libgpiod consumer name |

Relay channel mapping on Orange Pi Zero 2W:

| Relay channel | Physical pin | GPIO line |
|----------------|--------------|-----------|
| CH1 | 37 | 272 (PI16) |
| CH2 | 38 | 260 (PI4) |
| CH3 | 40 | 259 (PI3) |

Channel 2 is turned on below `-20.0 C` and off at or above `-20.0 C`.
There is no hysteresis. A failed measurement preserves both the previous
temperature value and the previous relay state. Shutdown makes a best-effort
attempt to turn the relay off.

## Troubleshooting

### Display

- Check SPI device permissions (`/dev/spidev*`).
- Verify GPIO permissions for the configured chip.
- For ILI9488, confirm the font path exists. ST7565 does not load a font file.

### NFC

- Verify I2C is enabled and the PN532 is in I2C mode.
- Check the libnfc connection string and I2C bus path.
- Inspect logs for libnfc initialization errors.

### Temperature sensor

- Confirm the AHT10 appears at address `0x38` on `/dev/i2c-2`.
- Verify GPIO line 260 is not requested by another process.
- Inspect the peripheral log for I2C initialization, measurement, or relay errors.

## References

- [NHD Display Demo Project](https://github.com/maxirmx/fuelflux.nhd)
- [NFC Demo Project](https://github.com/maxirmx/fuelflux.nfc)
- [libgpiod Documentation](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about/)
- [FreeType Documentation](https://www.freetype.org/freetype2/docs/documentation.html)
- [libnfc Documentation](https://github.com/nfc-tools/libnfc)
- [AHT10 Technical Manual](https://altronics.cl/uploads/AHT10.pdf)
- [Orange Pi Zero 2W GPIO pinout](https://www.orangepi.org/orangepiwiki/index.php/Orange_Pi_Zero_2W)
