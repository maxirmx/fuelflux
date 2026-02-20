// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "display/four_line_display.h"
#include "display/four_line_display_impl.h"
#include <memory>
#include <string>

// Forward declarations
class ILcdDriver;
class SpiLinux;
class GpioLine;

namespace fuelflux::display {

/**
 * Abstract Hardware Display Base Class
 * 
 * Base class for all hardware-backed display implementations.
 * Manages common hardware resources like SPI, GPIO, LCD driver, and framebuffer rendering.
 */
class HardwareDisplay : public FourLineDisplay {
public:
    virtual ~HardwareDisplay() override;

    // FourLineDisplay interface
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;
    int getWidth() const override;
    int getHeight() const override;
    unsigned int getMaxLineLength(unsigned int line_id) const override;

protected:
    /**
     * Constructor for hardware displays
     * @param width Display width in pixels
     * @param height Display height in pixels
     * @param smallFontSize Small font size in pixels
     * @param largeFontSize Large font size in pixels
     * @param leftMargin Left margin in pixels
     * @param rightMargin Right margin in pixels
     * @param topMargin Top margin in pixels
     * @param spiDevice SPI device path (e.g., "/dev/spidev1.0")
     * @param spiSpeed SPI speed in Hz
     * @param gpioChip GPIO chip path (e.g., "/dev/gpiochip0")
     * @param dcPin Data/Command GPIO pin number
     * @param rstPin Reset GPIO pin number
     * @param fontPath Path to TrueType font file
     */
    HardwareDisplay(int width,
                    int height,
                    int smallFontSize,
                    int largeFontSize,
                    int leftMargin,
                    int rightMargin,
                    int topMargin,
                    const std::string& spiDevice,
                    int spiSpeed,
                    const std::string& gpioChip,
                    int dcPin,
                    int rstPin,
                    const std::string& fontPath);

    // FourLineDisplay protected interface
    void setLineInternal(unsigned int line_id, const std::string& text) override;
    std::string getLineInternal(unsigned int line_id) const override;
    void clearAllInternal() override;
    void updateInternal() override;
    void setBacklightInternal(bool enabled) override;

    /**
     * Create the LCD driver instance (must be implemented by derived classes)
     * @return Unique pointer to the LCD driver
     */
    virtual std::unique_ptr<ILcdDriver> createLcdDriver() = 0;

    // Hardware components (accessible by derived classes)
    std::unique_ptr<SpiLinux> spi_;
    std::unique_ptr<GpioLine> dcLine_;
    std::unique_ptr<GpioLine> rstLine_;
    std::unique_ptr<ILcdDriver> lcd_;

private:
    bool isConnected_;
    
    // Display configuration
    int width_;
    int height_;
    int smallFontSize_;
    int largeFontSize_;
    int leftMargin_;
    int rightMargin_;
    int topMargin_;
    std::string spiDevice_;
    int spiSpeed_;
    std::string gpioChip_;
    int dcPin_;
    int rstPin_;
    std::string fontPath_;
    
    // Rendering implementation
    std::unique_ptr<FourLineDisplayImpl> displayImpl_;
};

} // namespace fuelflux::display
