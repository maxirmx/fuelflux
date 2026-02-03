#include "peripherals/display.h"
#include "logger.h"

#ifdef TARGET_REAL_DISPLAY
#include "nhd/four_line_display.h"
#include "nhd/st7565.h"
#include "nhd/spi_linux.h"
#include "peripherals/gpio_gpiod.h"
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
}

RealDisplay::~RealDisplay() {
    shutdown();
}

bool RealDisplay::initialize() {
    try {
        LOG_INFO("Initializing RealDisplay with device: {}", spiDevice_);
        
        // Initialize SPI
        spi_ = std::make_unique<SpiLinux>(spiDevice_);
        spi_->open(8000000, 0);  // 8 MHz, mode 0
        
        // Initialize GPIO lines
        dcLine_ = std::make_unique<GpioLine>(dcPin_, true, false, gpioChip_, "fuelflux-dc");
        rstLine_ = std::make_unique<GpioLine>(rstPin_, true, true, gpioChip_, "fuelflux-rst");
        
        // Initialize LCD controller
        lcd_ = std::make_unique<St7565>(*spi_, *dcLine_, *rstLine_);
        lcd_->reset();
        lcd_->init();
        lcd_->display_on(true);
        
        // Initialize display library
        display_ = std::make_unique<FourLineDisplay>(128, 64, 12, 28);
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
            if (lcd_) {
                lcd_->display_on(false);
            }
            if (display_) {
                display_->uninitialize();
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
        lcd_->display_on(enabled);
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
