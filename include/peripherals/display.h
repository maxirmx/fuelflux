// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"

#ifdef TARGET_REAL_DISPLAY
#include <memory>
#include <string>
#include <mutex>

// Forward declarations for display classes
class FourLineDisplay;
class SpiLinux;
class GpioLine;
class ILcdDriver;

// Default hardware configuration for displays on Orange Pi Zero 2W
namespace display_defaults {
    constexpr const char* SPI_DEVICE = "/dev/spidev1.0";
    constexpr const char* GPIO_CHIP = "/dev/gpiochip0";
    
    // ST7565 configuration (128x64 monochrome LCD)
    namespace st7565 {
        constexpr int DC_PIN = 271;   // Data/Command GPIO line offset
        constexpr int RST_PIN = 256;  // Reset GPIO line offset
        constexpr int WIDTH = 128;
        constexpr int HEIGHT = 64;
        constexpr int SMALL_FONT_SIZE = 12;
        constexpr int LARGE_FONT_SIZE = 28;
    }
    
    // ILI9488 configuration (480x320 TFT display)
    namespace ili9488 {
        constexpr int DC_PIN = 271;   // Data/Command GPIO line offset
        constexpr int RST_PIN = 256;  // Reset GPIO line offset
        constexpr int WIDTH = 480;
        constexpr int HEIGHT = 320;
        constexpr int SMALL_FONT_SIZE = 40;
        constexpr int LARGE_FONT_SIZE = 80;
    }
    
    constexpr const char* FONT_PATH = "/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf";
}
#endif

namespace fuelflux::peripherals {

#ifdef TARGET_REAL_DISPLAY
// Real hardware display implementation using ST7565 or ILI9488
class RealDisplay : public IDisplay {
public:
    RealDisplay(const std::string& spiDevice = display_defaults::SPI_DEVICE,
                const std::string& gpioChip = display_defaults::GPIO_CHIP,
                int dcPin = -1,  // Use -1 for auto-detect based on display type
                int rstPin = -1,  // Use -1 for auto-detect based on display type
                const std::string& fontPath = display_defaults::FONT_PATH);
    ~RealDisplay() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IDisplay implementation
    void showMessage(const DisplayMessage& message) override;
    void clear() override;
    void setBacklight(bool enabled) override;

private:
    bool isConnected_;
    bool backlightEnabled_;
    DisplayMessage currentMessage_;
    mutable std::mutex displayMutex_;

    // Hardware configuration
    std::string spiDevice_;
    std::string gpioChip_;
    int dcPin_;
    int rstPin_;
    std::string fontPath_;
    
    // NHD hardware components (using pimpl pattern)
    std::unique_ptr<SpiLinux> spi_;
    std::unique_ptr<GpioLine> dcLine_;
    std::unique_ptr<GpioLine> rstLine_;
    std::unique_ptr<ILcdDriver> lcd_;  // Polymorphic LCD driver (ST7565 or ILI9488)
    std::unique_ptr<FourLineDisplay> display_;
    
    void updateDisplay();
};
#else
// Stub implementation for non-hardware builds
class HardwareDisplay : public IDisplay {
public:
    HardwareDisplay();
    ~HardwareDisplay() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IDisplay implementation
    void showMessage(const DisplayMessage& message) override;
    void clear() override;
    void setBacklight(bool enabled) override;

private:
    bool isConnected_;
    bool backlightEnabled_;
    DisplayMessage currentMessage_;
};
#endif

} // namespace fuelflux::peripherals
