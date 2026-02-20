// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/ili9488_display.h"
#include "display/ili9488.h"
#include "logger.h"

namespace fuelflux::display {

Ili9488Display::Ili9488Display()
    : HardwareDisplay(
        ili9488::WIDTH,
        ili9488::HEIGHT,
        ili9488::SMALL_FONT_SIZE,
        ili9488::LARGE_FONT_SIZE,
        ili9488::LEFT_MARGIN,
        ili9488::RIGHT_MARGIN,
        ili9488::TOP_MARGIN,
        hardware::SPI_DEVICE,
        ili9488::SPI_SPEED,
        hardware::GPIO_CHIP,
        ili9488::DC_PIN,
        ili9488::RST_PIN,
        hardware::FONT_PATH)
{
    LOG_INFO("Ili9488Display created with default configuration");
}

Ili9488Display::Ili9488Display(const std::string& spiDevice,
                               const std::string& gpioChip,
                               int dcPin,
                               int rstPin,
                               const std::string& fontPath)
    : HardwareDisplay(
        ili9488::WIDTH,
        ili9488::HEIGHT,
        ili9488::SMALL_FONT_SIZE,
        ili9488::LARGE_FONT_SIZE,
        ili9488::LEFT_MARGIN,
        ili9488::RIGHT_MARGIN,
        ili9488::TOP_MARGIN,
        spiDevice,
        ili9488::SPI_SPEED,
        gpioChip,
        dcPin,
        rstPin,
        fontPath)
{
    LOG_INFO("Ili9488Display created with custom configuration");
}

std::unique_ptr<ILcdDriver> Ili9488Display::createLcdDriver() {
    LOG_INFO("Creating ILI9488 LCD driver");
    return std::make_unique<Ili9488>(
        *spi_,
        *dcLine_,
        *rstLine_,
        ili9488::WIDTH,
        ili9488::HEIGHT);
}

} // namespace fuelflux::display
