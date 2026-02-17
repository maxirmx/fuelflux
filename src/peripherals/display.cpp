// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/display.h"
#include "logger.h"

// Include appropriate display implementation based on build configuration
#ifdef TARGET_REAL_DISPLAY
    #ifdef DISPLAY_ST7565
        #include "display/st7565_display.h"
    #elif defined(DISPLAY_ILI9488)
        #include "display/ili9488_display.h"
    #else
        #error "No display type defined. Define DISPLAY_ST7565 or DISPLAY_ILI9488"
    #endif
#else
    #include "display/console_display.h"
#endif

namespace fuelflux::peripherals {

Display::Display() {
    // Create appropriate display implementation based on build configuration
#ifdef TARGET_REAL_DISPLAY
    #ifdef DISPLAY_ST7565
        LOG_INFO("Creating ST7565 hardware display");
        display_ = std::make_unique<fuelflux::display::St7565Display>();
    #elif defined(DISPLAY_ILI9488)
        LOG_INFO("Creating ILI9488 hardware display");
        display_ = std::make_unique<fuelflux::display::Ili9488Display>();
    #endif
#else
    LOG_INFO("Creating console display (stub implementation)");
    display_ = std::make_unique<fuelflux::display::ConsoleDisplay>();
#endif
}

Display::~Display() {
    shutdown();
}

bool Display::initialize() {
    if (!display_) {
        LOG_ERROR("Display instance not created");
        return false;
    }
    return display_->initialize();
}

void Display::shutdown() {
    if (display_) {
        display_->shutdown();
    }
}

bool Display::isConnected() const {
    if (!display_) {
        return false;
    }
    return display_->isConnected();
}

void Display::showMessage(const DisplayMessage& message) {
    if (!display_ || !display_->isConnected()) {
        return;
    }
    
    try {
        // Set all four lines
        display_->setLine(0, message.line1);
        display_->setLine(1, message.line2);
        display_->setLine(2, message.line3);
        display_->setLine(3, message.line4);
        
        // Update the display
        display_->update();
    } catch (const std::exception& e) {
        LOG_ERROR("Error showing message on display: {}", e.what());
    }
}

void Display::clear() {
    if (!display_ || !display_->isConnected()) {
        return;
    }
    
    try {
        display_->clearAll();
        display_->update();
    } catch (const std::exception& e) {
        LOG_ERROR("Error clearing display: {}", e.what());
    }
}

void Display::setBacklight(bool enabled) {
    if (!display_ || !display_->isConnected()) {
        return;
    }
    
    try {
        display_->setBacklight(enabled);
    } catch (const std::exception& e) {
        LOG_ERROR("Error setting backlight on display: {}", e.what());
    }
}

} // namespace fuelflux::peripherals
