// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/display.h"
#include "logger.h"

#ifdef TARGET_REAL_DISPLAY
#include "display/four_line_display.h"
#include "display/lcd_driver.h"
#include "display/spi_linux.h"
#include "hardware/gpio_line.h"

#ifdef DISPLAY_ST7565
#include "display/st7565.h"
#endif

#ifdef DISPLAY_ILI9488
#include "display/ili9488.h"
#endif

#include <stdexcept>

namespace fuelflux::peripherals {

RealDisplay::RealDisplay(const std::string& spiDevice,
                         const std::string& gpioChip,
                         int dcPin,
                         int rstPin,
                         const std::string& fontPath)
    : isConnected_(false)
    , backlightEnabled_(true)
    , spiDevice_(spiDevice)
    , gpioChip_(gpioChip)
    , dcPin_(dcPin)
    , rstPin_(rstPin)
    , fontPath_(fontPath)
{
    // Auto-detect pins based on display type if not specified
#ifdef DISPLAY_ST7565
    if (dcPin_ == -1) dcPin_ = display_defaults::st7565::DC_PIN;
    if (rstPin_ == -1) rstPin_ = display_defaults::st7565::RST_PIN;
#endif
#ifdef DISPLAY_ILI9488
    if (dcPin_ == -1) dcPin_ = display_defaults::ili9488::DC_PIN;
    if (rstPin_ == -1) rstPin_ = display_defaults::ili9488::RST_PIN;
#endif
}

RealDisplay::~RealDisplay() {
    shutdown();
}

bool RealDisplay::initialize() {
    try {
        LOG_INFO("Initializing RealDisplay with device: {}", spiDevice_);
        
        // Determine display parameters based on build configuration
        int width, height, smallFont, largeFont;
        int spiSpeed;
        
#ifdef DISPLAY_ST7565
        width = display_defaults::st7565::WIDTH;
        height = display_defaults::st7565::HEIGHT;
        smallFont = display_defaults::st7565::SMALL_FONT_SIZE;
        largeFont = display_defaults::st7565::LARGE_FONT_SIZE;
        spiSpeed = 8000000; // 8 MHz for ST7565
        LOG_INFO("Display type: ST7565 ({}x{})", width, height);
#elif defined(DISPLAY_ILI9488)
        width = display_defaults::ili9488::WIDTH;
        height = display_defaults::ili9488::HEIGHT;
        smallFont = display_defaults::ili9488::SMALL_FONT_SIZE;
        largeFont = display_defaults::ili9488::LARGE_FONT_SIZE;
        spiSpeed = 32000000; // 32 MHz for ILI9488
        LOG_INFO("Display type: ILI9488 ({}x{})", width, height);
#else
#error "No display type defined. Define DISPLAY_ST7565 or DISPLAY_ILI9488"
#endif
        
        // Initialize SPI
        spi_ = std::make_unique<SpiLinux>(spiDevice_);
        spi_->open(spiSpeed, 0);
        
        // Initialize GPIO lines
        dcLine_ = std::make_unique<GpioLine>(dcPin_, true, false, gpioChip_, "fuelflux-dc");
        rstLine_ = std::make_unique<GpioLine>(rstPin_, true, true, gpioChip_, "fuelflux-rst");
        
        // Initialize LCD controller based on display type
#ifdef DISPLAY_ST7565
        lcd_ = std::make_unique<St7565>(*spi_, *dcLine_, *rstLine_, width, height);
#elif defined(DISPLAY_ILI9488)
        lcd_ = std::make_unique<Ili9488>(*spi_, *dcLine_, *rstLine_, width, height);
#endif
        
        lcd_->reset();
        lcd_->init();
        
        // Initialize display library
        display_ = std::make_unique<FourLineDisplay>(width, height, smallFont, largeFont);
        if (!display_->initialize(fontPath_)) {
            LOG_ERROR("Failed to initialize FourLineDisplay with font: {}", fontPath_);
            return false;
        }
        
        isConnected_ = true;
        LOG_INFO("RealDisplay initialized successfully");
        
        // Show initial message
        clear();
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize RealDisplay: {}", e.what());
        isConnected_ = false;
        return false;
    }
}

void RealDisplay::shutdown() {
    if (isConnected_) {
        LOG_INFO("Shutting down RealDisplay");
        try {
            if (display_) {
                display_->uninitialize();
            }
            // Clear display on shutdown
            if (lcd_) {
                lcd_->clear();
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Error during RealDisplay shutdown: {}", e.what());
        }
        isConnected_ = false;
    }
}

bool RealDisplay::isConnected() const {
    return isConnected_;
}

void RealDisplay::showMessage(const DisplayMessage& message) {
    if (!isConnected_ || !display_) {
        return;
    }
    
    try {
        currentMessage_ = message;
        updateDisplay();
    } catch (const std::exception& e) {
        LOG_ERROR("Error showing message on RealDisplay: {}", e.what());
    }
}

void RealDisplay::clear() {
    if (!isConnected_ || !display_) {
        return;
    }
    
    try {
        currentMessage_ = {"", "", "", ""};
        updateDisplay();
    } catch (const std::exception& e) {
        LOG_ERROR("Error clearing RealDisplay: {}", e.what());
    }
}

void RealDisplay::setBacklight(bool enabled) {
    backlightEnabled_ = enabled;
    if (!isConnected_ || !lcd_) {
        return;
    }
    
    try {
        lcd_->set_backlight(enabled);
    } catch (const std::exception& e) {
        LOG_ERROR("Error setting backlight on RealDisplay: {}", e.what());
    }
}

void RealDisplay::updateDisplay() {
    if (!display_ || !lcd_) {
        return;
    }
    
    // Update all four lines
    display_->puts(0, currentMessage_.line1);
    display_->puts(1, currentMessage_.line2);
    display_->puts(2, currentMessage_.line3);
    display_->puts(3, currentMessage_.line4);
    
    // Render and send to LCD
    const auto& fb = display_->render();
    lcd_->set_framebuffer(fb);
}

} // namespace fuelflux::peripherals

#else

// Stub implementation for non-hardware builds
namespace fuelflux::peripherals {

HardwareDisplay::HardwareDisplay()
    : isConnected_(false)
    , backlightEnabled_(true)
{
}

HardwareDisplay::~HardwareDisplay() {
    shutdown();
}

bool HardwareDisplay::initialize() {
    // Stub implementation - does nothing
    isConnected_ = true;
    return true;
}

void HardwareDisplay::shutdown() {
    isConnected_ = false;
}

bool HardwareDisplay::isConnected() const {
    return isConnected_;
}

void HardwareDisplay::showMessage(const DisplayMessage& message) {
    currentMessage_ = message;
    // Stub - no actual display
}

void HardwareDisplay::clear() {
    currentMessage_ = {"", "", "", ""};
    // Stub - no actual display
}

void HardwareDisplay::setBacklight(bool enabled) {
    backlightEnabled_ = enabled;
    // Stub - no actual display
}

} // namespace fuelflux::peripherals

#endif
