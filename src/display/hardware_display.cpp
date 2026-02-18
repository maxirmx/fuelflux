// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/hardware_display.h"
#include "display/lcd_driver.h"
#include "display/spi_linux.h"
#include "hardware/gpio_line.h"
#include "logger.h"
#include <stdexcept>

namespace fuelflux::display {

HardwareDisplay::HardwareDisplay(int width,
                                 int height,
                                 int smallFontSize,
                                 int largeFontSize,
                                 int leftMargin,
                                 int rightMargin,
                                 const std::string& spiDevice,
                                 int spiSpeed,
                                 const std::string& gpioChip,
                                 int dcPin,
                                 int rstPin,
                                 const std::string& fontPath)
    : isConnected_(false)
    , width_(width)
    , height_(height)
    , smallFontSize_(smallFontSize)
    , largeFontSize_(largeFontSize)
    , leftMargin_(leftMargin)
    , rightMargin_(rightMargin)
    , spiDevice_(spiDevice)
    , spiSpeed_(spiSpeed)
    , gpioChip_(gpioChip)
    , dcPin_(dcPin)
    , rstPin_(rstPin)
    , fontPath_(fontPath)
{
}

HardwareDisplay::~HardwareDisplay() {
    shutdown();
}

bool HardwareDisplay::initialize() {
    try {
        LOG_INFO("Initializing HardwareDisplay: {}x{}, SPI: {}, GPIO: DC={} RST={}",
                 width_, height_, spiDevice_, dcPin_, rstPin_);
        
        // Initialize SPI
        spi_ = std::make_unique<SpiLinux>(spiDevice_);
        spi_->open(spiSpeed_, 0);
        LOG_INFO("SPI initialized at {} Hz", spiSpeed_);
        
        // Initialize GPIO lines
        dcLine_ = std::make_unique<GpioLine>(dcPin_, true, false, gpioChip_, "fuelflux-dc");
        rstLine_ = std::make_unique<GpioLine>(rstPin_, true, true, gpioChip_, "fuelflux-rst");
        LOG_INFO("GPIO lines initialized: DC={}, RST={}", dcPin_, rstPin_);
        
        // Create LCD driver (implemented by derived class)
        lcd_ = createLcdDriver();
        if (!lcd_) {
            LOG_ERROR("Failed to create LCD driver");
            return false;
        }
        
        // Reset and initialize LCD controller
        lcd_->reset();
        lcd_->init();
        LOG_INFO("LCD controller initialized");
        
        // Initialize display rendering library
        displayImpl_ = std::make_unique<FourLineDisplayImpl>(
            width_,
            height_,
            smallFontSize_,
            largeFontSize_,
            leftMargin_,
            rightMargin_);
        
        if (!displayImpl_->initialize(fontPath_)) {
            LOG_ERROR("Failed to initialize display rendering with font: {}", fontPath_);
            return false;
        }
        LOG_INFO("Display rendering initialized with font: {}", fontPath_);
        
        isConnected_ = true;
        
        // Clear display
        clearAllInternal();
        updateInternal();
        
        LOG_INFO("HardwareDisplay initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize HardwareDisplay: {}", e.what());
        isConnected_ = false;
        return false;
    }
}

void HardwareDisplay::shutdown() {
    if (isConnected_) {
        LOG_INFO("Shutting down HardwareDisplay");
        try {
            if (displayImpl_) {
                displayImpl_->uninitialize();
            }
            if (lcd_) {
                lcd_->clear();
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Error during HardwareDisplay shutdown: {}", e.what());
        }
        isConnected_ = false;
    }
}

bool HardwareDisplay::isConnected() const {
    return isConnected_;
}

int HardwareDisplay::getWidth() const {
    return width_;
}

int HardwareDisplay::getHeight() const {
    return height_;
}

unsigned int HardwareDisplay::getMaxLineLength(unsigned int line_id) const {
    if (!displayImpl_) {
        return 0;
    }
    return displayImpl_->length(line_id);
}

void HardwareDisplay::setLineInternal(unsigned int line_id, const std::string& text) {
    if (displayImpl_) {
        displayImpl_->puts(line_id, text);
    }
}

std::string HardwareDisplay::getLineInternal(unsigned int line_id) const {
    if (displayImpl_) {
        return displayImpl_->get_text(line_id);
    }
    return "";
}

void HardwareDisplay::clearAllInternal() {
    if (displayImpl_) {
        displayImpl_->clear_all();
    }
}

void HardwareDisplay::updateInternal() {
    if (!displayImpl_ || !lcd_) {
        return;
    }
    
    // Render text to framebuffer
    const auto& framebuffer = displayImpl_->render();
    
    // Send framebuffer to LCD
    lcd_->set_framebuffer(framebuffer);
}

void HardwareDisplay::setBacklightInternal(bool enabled) {
    if (lcd_) {
        lcd_->set_backlight(enabled);
    }
}

} // namespace fuelflux::display
