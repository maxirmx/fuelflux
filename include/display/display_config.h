// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

/**
 * Hardware Display Configuration
 * 
 * This file contains all hardware-specific configuration for display devices.
 * All values are hardcoded to simplify deployment and avoid environment variable complexity.
 */

namespace fuelflux::display {

// Common SPI and GPIO configuration for Orange Pi Zero 2W
namespace hardware {
    constexpr const char* SPI_DEVICE = "/dev/spidev1.0";
    constexpr const char* GPIO_CHIP = "/dev/gpiochip0";
    constexpr const char* FONT_PATH = "/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf";
}

// ST7565 configuration (128x64 monochrome LCD)
namespace st7565 {
    constexpr int DC_PIN = 271;          // Data/Command GPIO line offset
    constexpr int RST_PIN = 256;         // Reset GPIO line offset
    constexpr int WIDTH = 128;
    constexpr int HEIGHT = 64;
    constexpr int LEFT_MARGIN = 2;
    constexpr int RIGHT_MARGIN = 2;
    constexpr int TOP_MARGIN = 0;
    constexpr int SMALL_FONT_SIZE = 12;
    constexpr int LARGE_FONT_SIZE = 28;
    constexpr int SPI_SPEED = 8000000;   // 8 MHz
}

// ILI9488 configuration (480x320 TFT display)
namespace ili9488 {
    constexpr int DC_PIN = 271;          // Data/Command GPIO line offset
    constexpr int RST_PIN = 256;         // Reset GPIO line offset
    constexpr int WIDTH = 480;
    constexpr int HEIGHT = 320;
    constexpr int LEFT_MARGIN = 5;
    constexpr int RIGHT_MARGIN = 5;
    constexpr int TOP_MARGIN = 10;
    constexpr int SMALL_FONT_SIZE = 40;
    constexpr int LARGE_FONT_SIZE = 80;
    constexpr int SPI_SPEED = 32000000;  // 32 MHz
}

} // namespace fuelflux::display
