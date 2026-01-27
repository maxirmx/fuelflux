# Orange Pi Zero 2W → NHD-C12864A1Z-FSW-FBW-HTT Wiring (SPI1)

This document defines the **recommended wiring** for connecting a  
**Newhaven NHD-C12864A1Z-FSW-FBW-HTT (128×64 LCD)** to an  
**Orange Pi Zero 2W (H616)** using **SPI1 (spidev1.0)**.

This wiring matches the verified GPIO lines on your system (`gpio readall`) and
does **not allocate unnecessary pins**.

---

## LCD Pinout (from NHD-C12864A1Z-FSW-FBW-HTT datasheet)

| LCD Pin | Symbol | Function |
|--------:|--------|----------|
| 1 | SCL | Serial clock input |
| 2 | SI | Serial data (MOSI) |
| 3 | VDD | +3.0…3.3 V supply |
| 4 | A0 | Data/Command select (0=cmd, 1=data) |
| 5 | /RESET | Active‑low reset |
| 6 | /CS | Active‑low chip select |
| 7 | VSS | Ground |
| 8 | H+ | Heater +12 V |
| 9 | H− | Heater ground |
| 10 | LED− | Backlight cathode (GND) |
| 11 | LED+ | Backlight anode (+3.3 V) |
| 12 | NC | Not connected |

---

## Assumptions

- Device-tree overlay enabled: **`spi1-cs0-spidev`**
- Linux device node: **`/dev/spidev1.0`**
- LCD uses **4-wire SPI** (MOSI, SCLK, CS, D/C) + RESET
- Logic voltage: **3.3V**
- GPIO numbering uses **Linux GPIO numbers** (libgpiod)

---

## Power

| LCD Pin | Function | Orange Pi physical pin | Notes |
|--------:|---------|------------------------|------|
| 3 (VDD) | +3.3V | **Pin 1** or **Pin 17** | Use **3.3V only** |
| 7 (VSS) | GND | **6 / 9 / 14 / 20 / 25 / 30 / 34 / 39** | Common ground |

---

## SPI1 bus (ALT4)

| LCD Pin | LCD Signal | Orange Pi pin | Header name | Linux GPIO |
|--------:|------------|---------------|-------------|-----------|
| 1 | SCL | **Pin 23** | `SCLK.1` | **230** |
| 2 | SI (MOSI) | **Pin 19** | `MOSI.1` | **231** |
| 6 | /CS | **Pin 24** | `CE.0` | **229** |
| (opt) | MISO | **Pin 21** | `MISO.1` | **232** |

> Most ST7565-class LCDs do **not** require MISO.

---

## Control signals (verified output-capable)

| LCD Pin | LCD Signal | Orange Pi pin | Header name | Linux GPIO |
|--------:|------------|---------------|-------------|-----------|
| 4 | A0 / D‑C | **Pin 22** | `RXD.2` | **262** |
| 5 | /RESET | **Pin 11** | `TXD.5` | **226** |

Both GPIO **262** and **226** were successfully tested as outputs using
`libgpiod`.

---

## Backlight

### Always on
| LCD Pin | Connection |
|--------:|------------|
| 11 (LED+) | 3.3V |
| 10 (LED−) | GND |

### Dimmable (recommended)
Use an N‑MOSFET low‑side switch:

- LCD 11 (LED+) → 3.3V  
- LCD 10 (LED−) → MOSFET **drain**  
- MOSFET **source** → GND  
- MOSFET **gate** → spare GPIO (via ~100Ω, with ~100k pulldown)

Suggested spare GPIOs (from `gpio readall`, Mode = OFF):
`256, 271, 268, 258, 272, 260, 259`

---

## Heater (HTT)

> Heater must **not** be powered from 3.3V.

- LCD 8 (H+) → external **+12V**
- LCD 9 (H−) → MOSFET **drain**
- MOSFET **source** → GND
- MOSFET **gate** → spare GPIO (via ~100Ω, with pulldown)
- Grounds must be **common**

---

## Software command

```bash
sudo ./build/nhd12864_demo \
  --spidev /dev/spidev1.0 \
  --chip /dev/gpiochip0 \
  --dc 262 \
  --rst 226
```

---

## Notes

- Do **not** assume Raspberry Pi BCM numbering on Orange Pi.
- Always derive GPIO numbers from `gpio readall` or `gpioinfo`.
- SPI speed can be reduced if needed (e.g. 1 MHz) for signal integrity.
