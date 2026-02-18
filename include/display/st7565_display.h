// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "display/hardware_display.h"
#include "display/display_config.h"

namespace fuelflux::display {

/**
 * ST7565 Display Implementation
 * 
 * Hardware display implementation for ST7565 128x64 monochrome LCD controller.
 * Uses configuration from display_config.h.
 */
class St7565Display : public HardwareDisplay {
public:
    /**
     * Constructor with default configuration from display_config.h
     */
    St7565Display();
    
    /**
     * Constructor with custom configuration
     * @param spiDevice SPI device path
     * @param gpioChip GPIO chip path
     * @param dcPin Data/Command GPIO pin
     * @param rstPin Reset GPIO pin
     * @param fontPath Font file path
     */
    St7565Display(const std::string& spiDevice,
                  const std::string& gpioChip,
                  int dcPin,
                  int rstPin,
                  const std::string& fontPath);
    
    ~St7565Display() override = default;

protected:
    std::unique_ptr<ILcdDriver> createLcdDriver() override;
};

} // namespace fuelflux::display
