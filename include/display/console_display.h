// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "display/four_line_display.h"
#include <array>

namespace fuelflux::display {

/**
 * Console Display Implementation
 * 
 * Renders a bounded rectangle display to the console using ANSI escape codes.
 * The display is rendered at fixed screen positions and updates in place
 * without scrolling, matching the original console emulator behavior.
 */
class ConsoleDisplay : public FourLineDisplay {
public:
    ConsoleDisplay();
    ~ConsoleDisplay() override;

    // FourLineDisplay interface
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;
    int getWidth() const override { return 128; }
    int getHeight() const override { return 64; }
    unsigned int getMaxLineLength(unsigned int line_id) const override;

protected:
    void setLineInternal(unsigned int line_id, const std::string& text) override;
    std::string getLineInternal(unsigned int line_id) const override;
    void clearAllInternal() override;
    void updateInternal() override;
    void setBacklightInternal(bool enabled) override;

private:
    bool isConnected_;
    bool backlightEnabled_;
    bool initialized_;
    std::array<std::string, 4> lines_;
};

} // namespace fuelflux::display
