// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <cstdint>
#include <vector>

// Base interface for LCD drivers
// Provides common interface for both ST7565 and ILI9488 displays
class ILcdDriver {
public:
    virtual ~ILcdDriver() = default;
    
    // Reset the display hardware
    virtual void reset() = 0;
    
    // Initialize the display controller
    virtual void init() = 0;
    
    // Set the display framebuffer (monochrome, page-packed format)
    virtual void set_framebuffer(const std::vector<uint8_t>& fb) = 0;
    
    // Clear the display
    virtual void clear() = 0;
    
    // Control display backlight/power (if supported)
    virtual void set_backlight(bool enabled) = 0;
    
    // Get display dimensions
    virtual int width() const = 0;
    virtual int height() const = 0;
};
