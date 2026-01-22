#pragma once

#include "peripheral_interface.h"

#ifdef TARGET_REAL_HARDWARE
#include <memory>
#include <string>

// Forward declarations for NHD classes
class FourLineDisplay;
class SpiLinux;
class GpioLine;
class St7565;
#endif

namespace fuelflux::peripherals {

#ifdef TARGET_REAL_HARDWARE
// Real hardware display implementation using NHD-C12864A1Z-FSW-FBW-HTT
class RealDisplay : public IDisplay {
public:
    RealDisplay(const std::string& spiDevice = "/dev/spidev1.0",
                const std::string& gpioChip = "/dev/gpiochip0",
                int dcPin = 262,
                int rstPin = 226,
                const std::string& fontPath = "/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf");
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
