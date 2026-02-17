// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace fuelflux::display {

/**
 * Abstract Base Class for Four Line Display
 * 
 * This class defines the interface for all display implementations.
 * All public methods are thread-safe and protected by mutex.
 * 
 * Display Layout:
 * - Line 0: Small font
 * - Line 1: Large font (centered content)
 * - Line 2: Small font
 * - Line 3: Small font
 */
class FourLineDisplay {
public:
    virtual ~FourLineDisplay() = default;

    /**
     * Initialize the display
     * @return true on success, false on failure
     */
    virtual bool initialize() = 0;

    /**
     * Shutdown and cleanup resources
     */
    virtual void shutdown() = 0;

    /**
     * Check if display is initialized and connected
     */
    virtual bool isConnected() const = 0;

    /**
     * Set text for a specific line (thread-safe)
     * @param line_id Line identifier (0-3)
     * @param text UTF-8 encoded text to display
     */
    void setLine(unsigned int line_id, const std::string& text);

    /**
     * Get current text for a specific line (thread-safe)
     * @param line_id Line identifier (0-3)
     * @return Current text or empty string if line_id is invalid
     */
    std::string getLine(unsigned int line_id) const;

    /**
     * Clear all lines (thread-safe)
     */
    void clearAll();

    /**
     * Clear a specific line (thread-safe)
     * @param line_id Line identifier (0-3)
     */
    void clearLine(unsigned int line_id);

    /**
     * Update the physical display with current content (thread-safe)
     * This renders the framebuffer and updates the display hardware
     */
    void update();

    /**
     * Control display backlight/power (thread-safe)
     * @param enabled true to enable, false to disable
     */
    void setBacklight(bool enabled);

    /**
     * Get display dimensions
     */
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;

    /**
     * Get the maximum number of characters that can be displayed on a line
     * @param line_id Line identifier (0-3)
     * @return Number of characters, or 0 if line_id is invalid
     */
    virtual unsigned int getMaxLineLength(unsigned int line_id) const = 0;

protected:
    /**
     * Internal method to set line text (not thread-safe, must be called with lock held)
     */
    virtual void setLineInternal(unsigned int line_id, const std::string& text) = 0;

    /**
     * Internal method to get line text (not thread-safe, must be called with lock held)
     */
    virtual std::string getLineInternal(unsigned int line_id) const = 0;

    /**
     * Internal method to clear all lines (not thread-safe, must be called with lock held)
     */
    virtual void clearAllInternal() = 0;

    /**
     * Internal method to update display (not thread-safe, must be called with lock held)
     */
    virtual void updateInternal() = 0;

    /**
     * Internal method to set backlight (not thread-safe, must be called with lock held)
     */
    virtual void setBacklightInternal(bool enabled) = 0;

    mutable std::mutex mutex_;
};

} // namespace fuelflux::display
