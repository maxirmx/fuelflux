// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"

#ifdef TARGET_REAL_DISPLAY
#include <memory>
#include <string>

// Forward declarations for NHD classes
class FourLineDisplay;
class SpiLinux;
class GpioLine;
class St7565;

// Default hardware configuration for NHD display on Orange Pi Zero 2W
namespace nhd_defaults {
    constexpr const char* SPI_DEVICE = "/dev/spidev1.0";
    constexpr const char* GPIO_CHIP = "/dev/gpiochip0";
    constexpr int DC_PIN = 262;   // Data/Command GPIO line offset
    constexpr int RST_PIN = 226;  // Reset GPIO line offset
    constexpr const char* FONT_PATH = "/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf";
}
#endif

namespace fuelflux::peripherals {

#ifdef TARGET_REAL_DISPLAY
// Real hardware display implementation using NHD-C12864A1Z-FSW-FBW-HTT
class RealDisplay : public IDisplay {
public:
    RealDisplay(const std::string& spiDevice = nhd_defaults::SPI_DEVICE,
                const std::string& gpioChip = nhd_defaults::GPIO_CHIP,
                int dcPin = nhd_defaults::DC_PIN,
                int rstPin = nhd_defaults::RST_PIN,
                const std::string& fontPath = nhd_defaults::FONT_PATH);
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
    std::unique_ptr<St7565> lcd_;
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
