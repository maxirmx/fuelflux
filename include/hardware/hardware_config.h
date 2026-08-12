// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

/**
 * Hardware Configuration
 * 
 * This file contains all hardware-specific configuration for peripheral devices.
 * All values are hardcoded to simplify deployment and avoid environment variable complexity.
 * 
 * Target platform: Orange Pi Zero 2W
 */

#include <cstdint>

namespace fuelflux::hardware::config {

// Keyboard configuration (MCP23017 I2C GPIO expander with 4x4 matrix keypad)
namespace keyboard {
    constexpr const char* I2C_DEVICE = "/dev/i2c-2";
    constexpr uint8_t I2C_ADDRESS = 0x20;
    constexpr int POLL_MS = 5;          // Polling interval in milliseconds
    constexpr int DEBOUNCE_MS = 20;     // Debounce delay in milliseconds
    constexpr int RELEASE_MS = 30;      // Key release delay in milliseconds
    constexpr int SCAN_DELAY_US = 300;  // Row scan delay in microseconds
}

// NFC Card Reader configuration (PN532 via libnfc)
namespace card_reader {
    constexpr const char* I2C_DEVICE = "/dev/i2c-2";
    // Connection string format: "pn532_i2c:<device>"
    // If empty, will be auto-generated from I2C_DEVICE
    constexpr int POLL_DELAY_MS = 150;    // Delay between NFC target polls in milliseconds
    constexpr int READ_COOLDOWN_MS = 500; // Cooldown after a successful card read in milliseconds
}

// Flow Meter configuration (GPIO pulse counting)
namespace flow_meter {
    constexpr const char* GPIO_CHIP = "/dev/gpiochip0";
    constexpr int GPIO_PIN = 267;
    constexpr double TICKS_PER_LITER = 72.0;
}

// Pump configuration (GPIO relay control)
namespace pump {
    constexpr const char* GPIO_CHIP = "/dev/gpiochip0";
    constexpr int RELAY_PIN = 272;
    constexpr bool ACTIVE_LOW = true;   // Relay is active-low
}

// Display temperature sensor (AHT10) and heater relay configuration
namespace temperature_sensor {
    constexpr const char* I2C_DEVICE = "/dev/i2c-2";
    constexpr uint8_t I2C_ADDRESS = 0x38;
    constexpr const char* GPIO_CHIP = "/dev/gpiochip0";
    constexpr int RELAY_PIN = 260; // PI4, physical pin 38, relay channel 2
    constexpr bool ACTIVE_LOW = true;
    constexpr double RELAY_ON_THRESHOLD_CELSIUS = -20.0;
    constexpr double RELAY_OFF_THRESHOLD_CELSIUS = -17.0;
    constexpr const char* RELAY_CONSUMER = "display-heater";
}

// GPS receiver configuration (NMEA over UART)
namespace gps {
    constexpr const char* SERIAL_DEVICE = "/dev/ttyS2";
    constexpr int BAUD_RATE = 115200;
}

} // namespace fuelflux::hardware::config
