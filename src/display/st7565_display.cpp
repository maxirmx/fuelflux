// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/st7565_display.h"
#include "display/st7565.h"
#include "logger.h"

namespace fuelflux::display {

St7565Display::St7565Display()
    : HardwareDisplay(
        st7565::WIDTH,
        st7565::HEIGHT,
        st7565::SMALL_FONT_SIZE,
        st7565::LARGE_FONT_SIZE,
        st7565::LEFT_MARGIN,
        st7565::RIGHT_MARGIN,
        hardware::SPI_DEVICE,
        st7565::SPI_SPEED,
        hardware::GPIO_CHIP,
        st7565::DC_PIN,
        st7565::RST_PIN,
        hardware::FONT_PATH)
{
    LOG_INFO("St7565Display created with default configuration");
}

St7565Display::St7565Display(const std::string& spiDevice,
                             const std::string& gpioChip,
                             int dcPin,
                             int rstPin,
                             const std::string& fontPath)
    : HardwareDisplay(
        st7565::WIDTH,
        st7565::HEIGHT,
        st7565::SMALL_FONT_SIZE,
        st7565::LARGE_FONT_SIZE,
        st7565::LEFT_MARGIN,
        st7565::RIGHT_MARGIN,
        spiDevice,
        st7565::SPI_SPEED,
        gpioChip,
        dcPin,
        rstPin,
        fontPath)
{
    LOG_INFO("St7565Display created with custom configuration");
}

std::unique_ptr<ILcdDriver> St7565Display::createLcdDriver() {
    LOG_INFO("Creating ST7565 LCD driver");
    return std::make_unique<St7565>(
        *spi_,
        *dcLine_,
        *rstLine_,
        st7565::WIDTH,
        st7565::HEIGHT);
}

} // namespace fuelflux::display
